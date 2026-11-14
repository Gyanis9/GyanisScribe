#include "Base/Paths.h"
#include "Plugins/OcrEngines/OcrEngineFactory.h"

#include "Core/Ocr/IOcrEngine.h"
#include "Core/Ocr/OcrTextExporter.h"
#include "Plugins/OcrEngines/PpOcrOnnxEngine.h"
#include "Plugins/OcrEngines/UnavailableOcrEngine.h"
#if defined(GYANISSCRIBE_HAS_WINRT_OCR)
#include "Plugins/OcrEngines/WinRTImageOcrEngine.h"
#endif
#include "Base/Text.h"
#include "Platform/IPlatformIntegration.h"
#include "Core/Settings/SettingsKeys.h"
#include "Core/Settings/SettingsStore.h"

#include <QCoreApplication>
#include <QFont>
#include <QImage>
#include <QJsonArray>
#include <QPainter>
#include <QJsonDocument>
#include <QJsonObject>
#include <QVariant>

#include <gtest/gtest.h>

#include <algorithm>
#include <filesystem>
#include <format>
#include <fstream>
#include <memory>
#include <optional>
#include <string>

using namespace GyanisScribe::Plugins;
using namespace GyanisScribe::Core;
using GyanisScribe::Base::ErrorCode;
using GyanisScribe::Core::SettingsStore;
using GyanisScribe::Platform::createPlatformIntegration;

namespace
{
    std::unique_ptr<QCoreApplication>                             application;
    std::unique_ptr<GyanisScribe::Platform::IPlatformIntegration> integration;

    /**
     * @brief 这一档、这台机器上有没有「Windows 系统识别」
     * @return true 表示工厂会先拿到系统后端，走不到缺语言包与缺后端那两条提示
     * @note 判的是运行期事实（系统装没装识别语言包）。用例要先问它再决定断言哪一种形状，
     *       否则就是拿旧语义断言新分支
     */
    [[nodiscard]] bool systemOcrAvailable()
    {
#if defined(GYANISSCRIBE_HAS_WINRT_OCR)
        return WinRTImageOcrEngine().isAvailable();
#else
        return false;
#endif
    }

    class OcrFixture : public ::testing::Test
    {
    protected:
        static void SetUpTestSuite()
        {
            // argv 与 argc 的缓冲要有静态存储期：Qt 只存指针不拷贝，
            // 而 QCoreApplication::applicationDirPath() 这类调用是在装置函数返回之后才读它们。
            static int   argumentCount  = 1;
            static char  argumentZero[] = "TestOcrEngines";
            static char *arguments[]    = {argumentZero, nullptr};
            application                 = std::make_unique<QCoreApplication>(argumentCount, arguments);
            QCoreApplication::setOrganizationName(QStringLiteral("Gyanis"));
            QCoreApplication::setApplicationName(QStringLiteral("GyanisScribe-Tests"));
            integration = createPlatformIntegration();
        }

        static void TearDownTestSuite()
        {
            integration.reset();
            application.reset();
        }

        void SetUp() override
        {
            const auto caseName = std::string_view(::testing::UnitTest::GetInstance()->current_test_info()->name());
            m_root              = std::filesystem::temp_directory_path() / std::format("GyanisScribe-Ocr-{}", caseName);
            std::error_code ignored;
            std::filesystem::remove_all(m_root, ignored);
            std::filesystem::create_directories(m_root);

            m_iniFile = m_root / "settings.ini";
        }

        /// 造一个三件模型都非空的目录
        std::filesystem::path makePpOcrModelDirectory()
        {
            const auto directory = m_root / "ppocr";
            std::filesystem::create_directories(directory);
            for (const char *fileName: {kPpOcrDetModelFileName, kPpOcrRecModelFileName, kPpOcrDictFileName})
            {
                std::ofstream model(directory / fileName);
                model << "假模型文件，只用来验选型逻辑";
            }
            return directory;
        }

        OcrResult sampleResult()
        {
            OcrResult result;
            result.language          = QStringLiteral("zh-Hans");
            result.averageConfidence = 0.87;
            OcrLine line;
            line.words       = {OcrWord{QStringLiteral("第一"), QRect(1, 2, 10, 12)}, OcrWord{QStringLiteral("行"), QRect(14, 2, 8, 12)}};
            line.boundingBox = QRect(1, 2, 21, 12);
            result.lines     = {line};
            return result;
        }

        std::filesystem::path m_root;
        std::filesystem::path m_iniFile;
    };
} // namespace

/**
 * @brief 钉住 TXT 导出就是结果文本本身
 */
TEST_F(OcrFixture, TxtExportMatchesResultText)
{
    const auto result = sampleResult();
    EXPECT_EQ(toTxt(result), result.text());
    EXPECT_TRUE(toTxt(OcrResult{}).isEmpty());
}

/**
 * @brief 钉住 JSON 导出的形状：能被读回来，且词框与图片尺寸都带上了
 */
TEST_F(OcrFixture, JsonExportIsReadableAndCarriesBoxesAndImageSize)
{
    const auto text   = toJson(sampleResult(), QSize(400, 200));
    const auto parsed = QJsonDocument::fromJson(text.toUtf8());
    ASSERT_TRUE(parsed.isObject()) << "导出说要给对象，实际读不回对象";

    const auto root = parsed.object();
    EXPECT_EQ(root.value(QStringLiteral("language")).toString(), QStringLiteral("zh-Hans"));
    EXPECT_DOUBLE_EQ(root.value(QStringLiteral("averageConfidence")).toDouble(), 0.87);
    EXPECT_EQ(root.value(QStringLiteral("imageSize")).toObject().value(QStringLiteral("width")).toInt(), 400) << "不带图片尺寸，方框坐标换张图就对不上号";

    const auto lines = root.value(QStringLiteral("lines")).toArray();
    ASSERT_EQ(lines.size(), 1);
    const auto words = lines.at(0).toObject().value(QStringLiteral("words")).toArray();
    ASSERT_EQ(words.size(), 2);
    EXPECT_EQ(words.at(1).toObject().value(QStringLiteral("text")).toString(), QStringLiteral("行"));
    const auto box = words.at(0).toObject().value(QStringLiteral("box")).toObject();
    EXPECT_EQ(box.value(QStringLiteral("x")).toInt(), 1);
    EXPECT_EQ(box.value(QStringLiteral("height")).toInt(), 12);
    EXPECT_EQ(lines.at(0).toObject().value(QStringLiteral("text")).toString(), QStringLiteral("第一 行"));
}

/**
 * @brief 钉住后端没报置信度时 JSON 写 null 而不是 0
 * @details 0 是一个读数；把文件读回来的人分不开「这个后端不报这个量」与「把握接近零」。
 */
TEST_F(OcrFixture, JsonExportWritesNullWhenConfidenceIsAbsent)
{
    OcrResult blind         = sampleResult();
    blind.averageConfidence = std::nullopt;

    const auto parsed = QJsonDocument::fromJson(toJson(blind, QSize(400, 200)).toUtf8());
    ASSERT_TRUE(parsed.isObject());
    EXPECT_TRUE(parsed.object().value(QStringLiteral("averageConfidence")).isNull()) << "没有的读数被写成了一个数";
    EXPECT_EQ(parsed.object().value(QStringLiteral("language")).toString(), QStringLiteral("zh-Hans")) << "其余读数不该跟着少";
}

/**
 * @brief 钉住写文件的成功面与拒绝面（父路径被一个文件占住）
 */
TEST_F(OcrFixture, WriteTextFileCreatesAndRejectsBlockedPath)
{
    const auto target = m_root / "out" / "result.json";
    ASSERT_TRUE(writeTextFile(target, QStringLiteral("{}")).has_value());
    ASSERT_TRUE(std::filesystem::exists(target));
    EXPECT_EQ(std::filesystem::file_size(target), 2U);

    const auto blocker = m_root / "blocker";
    {
        std::ofstream file(blocker);
        file << "占位";
    }
    // 父路径中段被一个文件占住：走到的是建目录那一道拒绝，文案要带上那条路径
    const auto blocked = writeTextFile(blocker / "nested" / "result.txt", QStringLiteral("x"));
    ASSERT_FALSE(blocked.has_value()) << "父路径是个文件却说写成功，用户会去一个不存在的路径找结果";
    EXPECT_EQ(blocked.error().code, ErrorCode::FileWriteFailed);
    EXPECT_NE(blocked.error().message.find("目录创建失败"), std::string::npos) << "要说清是哪一步挡住了";
    EXPECT_NE(blocked.error().message.find("blocker"), std::string::npos) << "文案里要带上出问题的那条路径";

    // 目标本身是一个已存在的目录：建目录那一步过得去，卡在打开文件那一道
    const auto occupied = m_root / "occupied.txt";
    std::filesystem::create_directories(occupied);
    const auto cannotOpen = writeTextFile(occupied, QStringLiteral("x"));
    ASSERT_FALSE(cannotOpen.has_value()) << "同名目录存在却说写成功，结果其实一个字都没落";
    EXPECT_EQ(cannotOpen.error().code, ErrorCode::FileWriteFailed);
    EXPECT_NE(cannotOpen.error().message.find("写不出去"), std::string::npos);
}

/**
 * @brief 钉住「路径里有本地码页装不下的字」时结果仍写进那一条路
 * @details Windows 上 path::string() 按本地 ANSI 码页给字节，装不下就直接抛 std::system_error，
 *          而抛出点在一个承诺返回 std::expected 的函数里——可恢复的错误被升成了崩溃。
 *          目录名刻意带表情符号：只用汉字时 cp936 自洽，这条判据会退化成恒过
 *          （与 SettingsStore 那条同一个写法，改回窄串应当当场红）。
 */
TEST_F(OcrFixture, WriteTextFileTakesAPathTheLocalCodepageCannotHold)
{
    const auto exotic = m_root / QString::fromUtf8("识别结果-📍").toStdWString();
    const auto target = exotic / "result.txt";

    const auto written = writeTextFile(target, QStringLiteral("第一行"));
    ASSERT_TRUE(written.has_value()) << written.error().message;
    ASSERT_TRUE(std::filesystem::exists(target)) << "报成功而文件不在这条路上";
    EXPECT_GT(std::filesystem::file_size(target), 0U);
}

/**
 * @brief 钉住桩后端：不可用要说原因给替代做法，识别不能装成功
 */
TEST_F(OcrFixture, UnavailableEngineRejectsWithoutPretending)
{
    UnavailableOcrEngine engine(QStringLiteral("缺模型：请重新安装本程序以恢复识别模型，或在设置的 OCR 页把模型目录指过去"));
    EXPECT_EQ(engine.name(), QStringLiteral("无可用识别后端"));
    EXPECT_FALSE(engine.isAvailable());
    EXPECT_EQ(engine.supportedLanguages().size(), 0);
    EXPECT_EQ(engine.unavailableReason(), QStringLiteral("缺模型：请重新安装本程序以恢复识别模型，或在设置的 OCR 页把模型目录指过去"));

    const UnavailableOcrEngine defaulted;
    EXPECT_NE(defaulted.unavailableReason().indexOf(QStringLiteral("模型目录")), -1) << "默认文案要指向模型目录";
    EXPECT_EQ(defaulted.unavailableReason().indexOf(QStringLiteral("tessdata")), -1) << "默认文案还在让用户放语言包";

    const auto recognized = engine.recognize(QImage(20, 20, QImage::Format_ARGB32), QStringLiteral("en"));
    ASSERT_FALSE(recognized.has_value()) << "没有后端却返回空结果，界面会显示「图中没有文字」这种误导话";
    EXPECT_EQ(recognized.error().code, ErrorCode::DependencyMissing);
}

/**
 * @brief 钉住首选顺序：三件模型齐备时工厂交回 PaddleOCR
 */
TEST_F(OcrFixture, FactoryPrefersPpOcrWhenModelsArePresent)
{
    SettingsStore settings(m_iniFile);
    const auto    directory = makePpOcrModelDirectory();
    ASSERT_TRUE(settings.setValue(GyanisScribe::Core::kOcrModelDirectory, QVariant(GyanisScribe::Base::Paths::toDisplayText(directory))).has_value());

    const auto engine = createOcrEngine(settings, *integration);
    ASSERT_NE(engine, nullptr) << "选型不许交回空指针";
    ASSERT_TRUE(engine->isAvailable()) << engine->unavailableReason().toStdString();
    EXPECT_EQ(engine->name(), QStringLiteral("PaddleOCR")) << "模型齐备时没有优先选 PaddleOCR";
    EXPECT_TRUE(engine->supportedLanguages().contains(QStringLiteral("zh-Hans")));
    EXPECT_TRUE(engine->supportedLanguages().contains(QStringLiteral("en")));
}

/**
 * @brief 钉住回落顺序：没有模型时先给系统识别，系统识别也没有就报 PaddleOCR 缺什么
 * @details 这台机器装没装系统识别语言包是运行期事实：装了就必须交回系统后端；
 *          没装才轮到不可用桩，原因里点名随包的模型目录与缺的文件，且不许再出现 Tesseract 的说法。
 */
TEST_F(OcrFixture, FactoryFallsBackToSystemOcrOrReportsPpOcrReason)
{
    SettingsStore settings(m_iniFile);
    // 安装目录用临时目录隔离：开发树的构建产物目录里摆着随包模型，拿它当「没有模型」的现场不再成立
    const auto installDirectory = m_root / "install";
    ASSERT_TRUE(locatePpOcrModelDirectory(settings, installDirectory).empty()) << "这个用例的前提是设置、安装目录与应用数据目录都没有完整模型";

    const auto engine = createOcrEngine(settings, *integration, installDirectory);
    ASSERT_NE(engine, nullptr) << "选型不许交回空指针";

    if (systemOcrAvailable())
    {
        // 系统后端在场就不许再要用户放模型：这条断言钉住「PP-OCR 之后问系统」的回落本身
        EXPECT_TRUE(engine->isAvailable()) << "系统识别可用，工厂却说不可用";
        EXPECT_EQ(engine->name(), QString::fromUtf8(GyanisScribe::Base::uiTextUtf8("Windows 系统识别"))) << "拿到的不是系统后端";
        return;
    }

    EXPECT_FALSE(engine->isAvailable()) << "机器上一个模型文件都没有，却说后端可用";
    const auto reason = engine->unavailableReason();
    EXPECT_NE(reason.indexOf(QStringLiteral("ppocr")), -1) << "原因里没点名随包的模型目录：实际是 " << reason.toStdString();
    EXPECT_NE(reason.indexOf(QString::fromLatin1(kPpOcrRecModelFileName)), -1) << "原因里没说缺哪个模型文件：" << reason.toStdString();
    EXPECT_EQ(reason.indexOf(QStringLiteral("Tesseract")), -1);
    EXPECT_EQ(reason.indexOf(QStringLiteral("traineddata")), -1);

    const auto recognized = engine->recognize(QImage(32, 32, QImage::Format_ARGB32), QStringLiteral("en"));
    EXPECT_FALSE(recognized.has_value());
}

/**
 * @brief 钉住候选顺序：设置项 → 程序目录/ppocr → 应用数据目录/ppocr
 * @details 软件装完即用：模型在安装包的 ppocr 子目录里。随包那份排在应用数据目录之后，
 *          就等于装了包还要用户自己去放文件。
 */
TEST_F(OcrFixture, PpOcrModelCandidatesPutBundledDirectoryBeforeUserData)
{
    SettingsStore settings(m_iniFile);

    const auto installDirectory    = m_root / "install";
    const auto configuredDirectory = m_root / "configured";
    ASSERT_TRUE(settings.setValue(GyanisScribe::Core::kOcrModelDirectory, QVariant(GyanisScribe::Base::Paths::toDisplayText(configuredDirectory))).has_value());

    const auto candidates = ppOcrModelCandidateDirectories(settings, installDirectory);
    ASSERT_EQ(candidates.size(), 3U) << "候选应恰好是设置项、程序目录与应用数据目录三处";
    EXPECT_EQ(candidates.at(0), configuredDirectory) << "设置项指的那份没排在最前";
    EXPECT_EQ(candidates.at(1), installDirectory / "ppocr") << "随包的模型在程序目录下的 ppocr 里";
    EXPECT_EQ(candidates.at(2), GyanisScribe::Base::Paths::applicationDataDirectory() / "ppocr");

    const auto bundledDirectory = candidates.at(1);
    std::filesystem::create_directories(bundledDirectory);
    for (const char *fileName: {kPpOcrDetModelFileName, kPpOcrRecModelFileName, kPpOcrDictFileName})
    {
        std::ofstream(bundledDirectory / fileName) << "假模型文件，只用来验查找顺序";
    }
    EXPECT_EQ(firstDirectoryWithModels(candidates), bundledDirectory) << "只有随包那份放着完整模型时，选出来的必须是它";
}

/**
 * @brief 钉住缺件判定：只放检测模型的目录不算可用，原因按文件名点名缺的那两件
 */
TEST_F(OcrFixture, ReportsMissingPpOcrFilesByName)
{
    const auto directory = m_root / "partial";
    std::filesystem::create_directories(directory);
    std::ofstream(directory / kPpOcrDetModelFileName) << "只有检测模型";

    SettingsStore settings(m_iniFile);
    ASSERT_TRUE(settings.setValue(GyanisScribe::Core::kOcrModelDirectory, QVariant(GyanisScribe::Base::Paths::toDisplayText(directory))).has_value());
    // 安装目录用临时目录隔离：构建产物目录里有随包模型，不能让本用例的前提跟着构建布局走
    const auto installDirectory = m_root / "install";
    EXPECT_TRUE(locatePpOcrModelDirectory(settings, installDirectory).empty()) << "缺两件模型文件的目录被当成了可用模型目录";

    PpOcrOnnxEngine engine(directory);
    EXPECT_FALSE(engine.isAvailable()) << "缺两件模型文件却说后端可用";
    const auto reason = engine.unavailableReason();
    EXPECT_NE(reason.indexOf(QString::fromLatin1(kPpOcrRecModelFileName)), -1) << "原因里没点名缺的识别模型：" << reason.toStdString();
    EXPECT_NE(reason.indexOf(QString::fromLatin1(kPpOcrDictFileName)), -1) << "原因里没点名缺的字表：" << reason.toStdString();
}

/**
 * @brief 钉住对外语言标签词表与默认值同解
 */
TEST_F(OcrFixture, LanguageTagsIncludeTheDefault)
{
    const auto tags = supportedOcrLanguageTags();
    EXPECT_TRUE(tags.contains(QString::fromUtf8(kDefaultOcrLanguage))) << "默认语言不在词表里，设置页的初始值会显示成一个不存在的选项";
    EXPECT_TRUE(tags.contains(QStringLiteral("en")));
}
