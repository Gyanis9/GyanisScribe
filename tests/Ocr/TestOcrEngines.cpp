#include "Base/Paths.h"
#include "Ocr/OcrEngineFactory.h"

#include "Ocr/IOcrEngine.h"
#include "Ocr/OcrTextExporter.h"
#include "Ocr/UnavailableOcrEngine.h"
#if defined(GYANISSCRIBE_HAS_TESSERACT)
#include "Ocr/TesseractOcrEngine.h"
#endif
#if defined(GYANISSCRIBE_HAS_WINRT_OCR)
#include "Ocr/WinRTImageOcrEngine.h"
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

using namespace GyanisScribe::Ocr;
using GyanisScribe::Base::ErrorCode;
using GyanisScribe::Platform::createPlatformIntegration;
using GyanisScribe::Core::SettingsStore;

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

        /// 造一个只放了模型文件的目录
        std::filesystem::path makeModelDirectoryWith(const std::string &modelName)
        {
            const auto directory = m_root / "tessdata";
            std::filesystem::create_directories(directory);
            std::ofstream model(directory / modelName);
            model << "假模型文件，只用来验选型逻辑";
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
    UnavailableOcrEngine engine(QStringLiteral("缺语言包：请把 eng.traineddata 放进设置里指的目录"));
    EXPECT_EQ(engine.name(), QStringLiteral("无可用识别后端"));
    EXPECT_FALSE(engine.isAvailable());
    EXPECT_EQ(engine.supportedLanguages().size(), 0);
    EXPECT_EQ(engine.unavailableReason(), QStringLiteral("缺语言包：请把 eng.traineddata 放进设置里指的目录"));

    const auto recognized = engine.recognize(QImage(20, 20, QImage::Format_ARGB32), QStringLiteral("en"));
    ASSERT_FALSE(recognized.has_value()) << "没有后端却返回空结果，界面会显示「图中没有文字」这种误导话";
    EXPECT_EQ(recognized.error().code, ErrorCode::DependencyMissing);
}

/**
 * @brief 钉住语言包目录的三级查找与「空目录不算有语言包」
 */
TEST_F(OcrFixture, LocatesTessDataBySettingThenReturnsEmptyWhenMissing)
{
    SettingsStore settings(m_iniFile);
    EXPECT_TRUE(locateTessDataDirectory(settings).empty()) << "什么都没放时该交回空路径，让调用方给补法";

    const auto directory = makeModelDirectoryWith("eng.traineddata");
    ASSERT_TRUE(settings.setValue(GyanisScribe::Core::kOcrTessDataDirectory, QVariant(QString::fromLocal8Bit(directory.string().c_str()))).has_value());
    EXPECT_EQ(locateTessDataDirectory(settings), directory) << "设置里指了有模型文件的目录却找不到";

    // 空目录（只有子目录、没有 traineddata）不算
    const auto emptyDirectory = m_root / "empty-tessdata";
    std::filesystem::create_directories(emptyDirectory);
    ASSERT_TRUE(settings.setValue(GyanisScribe::Core::kOcrTessDataDirectory, QVariant(QString::fromLocal8Bit(emptyDirectory.string().c_str()))).has_value());
    EXPECT_TRUE(locateTessDataDirectory(settings).empty()) << "空目录被当成了有语言包，初始化时才在下一层报错";
}

/**
 * @brief 钉住后端选型：先问系统识别，问不到才落回语言包那条路
 * @details 需求 §8 写明 Windows 优先用 Windows.Media.Ocr，所以「没有模型文件」这一格在两种形状下
 *          结论不同：这台机器装了识别语言包时工厂必须交回系统后端（旧断言「没有模型文件就是桩」
 *          在新语义下不成立）；没装才报缺语言包，点名三处查找位置。
 */
TEST_F(OcrFixture, FactoryPrefersSystemOcrAndFallsBackWithActionableReason)
{
    SettingsStore settings(m_iniFile);
    const auto    engine = createOcrEngine(settings, *integration);
    ASSERT_NE(engine, nullptr) << "选型不许交回空指针";

    EXPECT_TRUE(locateTessDataDirectory(settings).empty());

    if (systemOcrAvailable())
    {
        // 系统后端在场就不许再要用户放语言包：这条断言钉住「先问系统」的顺序本身
        EXPECT_TRUE(engine->isAvailable()) << "系统识别可用，工厂却说不可用";
        EXPECT_EQ(engine->name(), QString::fromUtf8(GyanisScribe::Base::uiTextUtf8("Windows 系统识别"))) << "拿到的不是系统后端";
        return;
    }

    EXPECT_FALSE(engine->isAvailable()) << "机器上一个模型文件都没有，却说后端可用";
    const auto reason = engine->unavailableReason();
    EXPECT_NE(reason.indexOf(QStringLiteral("TESSDATA_PREFIX")), -1) << "原因里没提环境变量那一路：实际是 " << reason.toStdString();
    EXPECT_NE(reason.indexOf(QStringLiteral("traineddata")), -1) << "原因里没说该放什么文件";

    const auto recognized = engine->recognize(QImage(32, 32, QImage::Format_ARGB32), QStringLiteral("en"));
    EXPECT_FALSE(recognized.has_value());
}

/**
 * @brief 钉住「语言包在、但这一档没编入后端」那一格：不许把构建配置问题说成用户少放了文件
 * @details 只有既没有 Tesseract、又没有系统识别的那一档走这一格（Tesseract 只进 Release 依赖图，
 *          原因见 conanfile.py；Windows Debug 档现在有系统识别，这一格改由容器那一档跑同一条断言）。
 *          旧的断言是「任何构建都报后端缺席」，新语义是按宏分两种形状：带后端的构建里
 *          同一份语言包必须拿到 Tesseract，那条由下面的用例钉。
 */
#if !defined(GYANISSCRIBE_HAS_TESSERACT)
TEST_F(OcrFixture, ReportsMissingBackendSeparatelyFromMissingLanguagePacks)
{
    if (systemOcrAvailable())
    {
        // 工厂先问系统识别并且拿到了它，根本走不到「后端没编入」这一格；跳过而不是改断言，
        // 这一格在容器那一档（不编 WinRT）仍然是硬覆盖
        GTEST_SKIP() << "这台机器有系统识别，工厂拿不到「后端没编入」那一格";
    }

    const auto    directory = makeModelDirectoryWith("eng.traineddata");
    SettingsStore settings(m_iniFile);
    ASSERT_TRUE(settings.setValue(GyanisScribe::Core::kOcrTessDataDirectory, QVariant(QString::fromLocal8Bit(directory.string().c_str()))).has_value());

    const auto engine = createOcrEngine(settings, *integration);
    ASSERT_FALSE(engine->isAvailable());
    const auto reason = engine->unavailableReason();
    EXPECT_NE(reason.indexOf(QStringLiteral("没有编入任何识别后端")), -1) << "说的是缺语言包，实际语言包就在：" << reason.toStdString();
    EXPECT_NE(reason.indexOf(QStringLiteral("OcrBackends")), -1) << "要指到接入办法那篇文档";
    EXPECT_EQ(reason.indexOf(QStringLiteral("traineddata）放进")), -1) << "不该再让用户去放本已存在的文件";
}
#else
/**
 * @brief 钉住带后端的构建里，语言包在位就拿到真后端
 * @details 模型文件是这里现造的空文件，所以识别一定失败——失败要报「初始化语言失败」，
 *          不许把坏模型当成「认出来是空结果」悄悄放过去。
 */
TEST_F(OcrFixture, PrefersTesseractWhenLanguagePacksArePresent)
{
    const auto    directory = makeModelDirectoryWith("eng.traineddata");
    SettingsStore settings(m_iniFile);
    ASSERT_TRUE(settings.setValue(GyanisScribe::Core::kOcrTessDataDirectory, QVariant(QString::fromLocal8Bit(directory.string().c_str()))).has_value());

    // 系统识别在场时工厂不会把 Tesseract 交出来（那条顺序归上面那条用例钉），所以这里按能力取
    // 要验的那份后端：两种形状断的是同一件事——语言包在位时这条路是通的
    const auto engine = systemOcrAvailable() ? std::make_unique<TesseractOcrEngine>(directory) : createOcrEngine(settings, *integration);
    ASSERT_TRUE(engine->isAvailable()) << "语言包已经在位却仍报不可用：选型没接上 Tesseract";
    EXPECT_EQ(engine->name(), QStringLiteral("Tesseract")) << "拿到的不是真后端";
    EXPECT_TRUE(engine->supportedLanguages().contains(QStringLiteral("en")));
    EXPECT_TRUE(engine->unavailableReason().isEmpty());

    QImage image(64, 24, QImage::Format_ARGB32);
    image.fill(Qt::white);
    const auto recognized = engine->recognize(image, QStringLiteral("en"));
    ASSERT_FALSE(recognized.has_value()) << "空文件冒充的模型被当成认出来了";
    EXPECT_NE(recognized.error().message.find("初始化语言"), std::string::npos) << recognized.error().message;
    EXPECT_EQ(recognized.error().code, GyanisScribe::Base::ErrorCode::InternalError);
}
#endif

/**
 * @brief 钉住对外语言标签词表与默认值同解
 */
TEST_F(OcrFixture, LanguageTagsIncludeTheDefault)
{
    const auto tags = supportedOcrLanguageTags();
    EXPECT_TRUE(tags.contains(QString::fromUtf8(kDefaultOcrLanguage))) << "默认语言不在词表里，设置页的初始值会显示成一个不存在的选项";
    EXPECT_TRUE(tags.contains(QStringLiteral("en")));
}

/**
 * @brief 钉住语言包的查找顺序把「随包那份」排在用户数据目录之前
 * @details 软件装完即用：语言包在安装包里，位置是 <程序目录>/tessdata。
 *          排在应用数据目录之后，就等于装了包还要用户自己去放文件。
 */
TEST_F(OcrFixture, TessDataCandidatesPutTheBundledDirectoryBeforeUserData)
{
    SettingsStore settings(m_iniFile);

    const auto installDirectory = m_root / "install";
    const auto bundledDirectory = installDirectory / "tessdata";
    std::filesystem::create_directories(bundledDirectory);
    std::ofstream(bundledDirectory / "eng.traineddata") << "假模型文件，只用来验查找顺序";

    const auto candidates = tessDataCandidateDirectories(settings, installDirectory);
    const auto indexOf    = [&candidates](const std::filesystem::path &path)
    {
        const auto found = std::ranges::find(candidates, path);
        return found == candidates.end() ? -1 : static_cast<int>(std::ranges::distance(candidates.begin(), found));
    };

    const auto userDataDirectory = GyanisScribe::Base::Paths::applicationDataDirectory() / "tessdata";
    ASSERT_GE(indexOf(bundledDirectory), 0) << "候选里没有程序目录下的 tessdata";
    ASSERT_GE(indexOf(userDataDirectory), 0) << "候选里没有应用数据目录下的 tessdata";
    EXPECT_LT(indexOf(bundledDirectory), indexOf(userDataDirectory)) << "随包那份排在用户数据目录之后，装完还得自己放语言包";

    EXPECT_EQ(firstDirectoryWithLanguagePacks(candidates), bundledDirectory) << "只有随包那份放着模型时，选出来的必须是它";
}
