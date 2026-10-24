#include "Gui/Ocr/OcrResultWindow.h"

#include "Core/Ocr/OcrTypes.h"
#include "Core/Settings/SettingsStore.h"
#include "Gui/Resources/IconProvider.h"
#include "Gui/Theme/ThemeManager.h"

#include <gtest/gtest.h>

#include <QApplication>
#include <QClipboard>
#include <QComboBox>
#include <QImage>
#include <QLabel>
#include <QTextEdit>

#include <filesystem>
#include <format>
#include <fstream>
#include <memory>
#include <optional>
#include <string>

using GyanisScribe::Core::OcrLine;
using GyanisScribe::Core::OcrResult;
using GyanisScribe::Core::OcrWord;
using GyanisScribe::Core::SettingsStore;
using GyanisScribe::Gui::highlightRectFor;
using GyanisScribe::Gui::IconProvider;
using GyanisScribe::Gui::lineIndexForBlock;
using GyanisScribe::Gui::OcrResultWindow;
using GyanisScribe::Gui::statusLineFor;
using GyanisScribe::Gui::ThemeManager;

namespace
{
    std::unique_ptr<QApplication> application;

    /// 造一个两行的结果：行框按手算给，坐标与下方 800x200 的原图同一系
    OcrResult makeTwoLineResult()
    {
        OcrResult result;
        result.language          = QStringLiteral("zh-Hans");
        result.averageConfidence = 0.8;
        OcrLine firstLine;
        firstLine.words       = {OcrWord{QStringLiteral("你好"), QRect{100, 20, 80, 16}}, OcrWord{QStringLiteral("世界"), QRect{190, 20, 80, 16}}};
        firstLine.boundingBox = QRect{100, 20, 170, 16};
        OcrLine secondLine;
        secondLine.words       = {OcrWord{QStringLiteral("第二行"), QRect{100, 60, 120, 16}}};
        secondLine.boundingBox = QRect{100, 60, 120, 16};
        result.lines           = {firstLine, secondLine};
        return result;
    }

    /**
     * @brief 把导出的文件整份读回来
     * @param path 文件路径
     * @return 文件内容；读不到返回空串（用例里由断言把它抓出来）
     * @note std::filesystem::read_file 不在 C++17 最终稿里，MSVC 的 STL 没有它，这里自己读一次
     */
    std::string readWholeFile(const std::filesystem::path &path)
    {
        std::ifstream input{path, std::ios::binary};
        if (!input)
        {
            return {};
        }
        return std::string{std::istreambuf_iterator<char>{input}, std::istreambuf_iterator<char>{}};
    }

    /// 一张纯色图当原图：窗口只按尺寸算方框，不看像素
    QImage makeImage(const QSize &size)
    {
        QImage image{size, QImage::Format_ARGB32};
        image.fill(QColor{200, 200, 200});
        return image;
    }

    class OcrResultWindowFixture : public ::testing::Test
    {
    protected:
        static void SetUpTestSuite()
        {
            // argv 与 argc 的缓冲要有静态存储期：Qt 只存指针不拷贝，
            // 而 QCoreApplication::applicationDirPath() 这类调用是在装置函数返回之后才读它们。
            static int   argumentCount  = 1;
            static char  argumentZero[] = "TestOcrResultWindow";
            static char *arguments[]    = {argumentZero, nullptr};
            application                 = std::make_unique<QApplication>(argumentCount, arguments);
        }

        static void TearDownTestSuite()
        {
            application.reset();
        }

        void SetUp() override
        {
            // INI 按用例名给独立路径：并行跑时两条用例共写一份配置会互相踩掉主题
            m_iniPath = std::filesystem::temp_directory_path() / std::format("GyanisScribe-OcrWindow-{}.ini", ::testing::UnitTest::GetInstance()->current_test_info()->name());
            std::error_code cleanup;
            std::filesystem::remove(m_iniPath, cleanup);
            m_store = std::make_unique<SettingsStore>(m_iniPath);
            m_theme = std::make_unique<ThemeManager>(*m_store);
            m_icons = std::make_unique<IconProvider>();
        }

        void TearDown() override
        {
            std::error_code cleanup;
            std::filesystem::remove(m_iniPath, cleanup);
        }

        [[nodiscard]] std::unique_ptr<OcrResultWindow> makeWindow(const OcrResult &result) const
        {
            return std::make_unique<OcrResultWindow>(makeImage({800, 200}), result, *m_icons, *m_theme);
        }

        std::unique_ptr<SettingsStore> m_store;   ///< 本用例专属配置，避免并行互踩
        std::unique_ptr<ThemeManager>  m_theme;   ///< 主题管理器，窗口按它取高亮主色
        std::unique_ptr<IconProvider>  m_icons;   ///< 图标供给器
        std::filesystem::path          m_iniPath; ///< 本用例的 INI 路径，收尾时删掉
    };

} // namespace

/**
 * @brief 钉住块序号到行下标的换算只在结果范围内成立
 * @details 越界返回 -1（不圈任何行）而不是夹到最后一行：夹住会让用户以为光标所在行就是那一行。
 */
TEST(OcrResultMapping, LineIndexFollowsBlockOnlyInsideResult)
{
    const auto result = makeTwoLineResult();

    EXPECT_EQ(lineIndexForBlock(result, 0), 0);
    EXPECT_EQ(lineIndexForBlock(result, 1), 1);
    EXPECT_EQ(lineIndexForBlock(result, 2), -1) << "行数只有两行，第 3 块不该圈任何方框";
    EXPECT_EQ(lineIndexForBlock(result, -1), -1);

    OcrResult empty;
    EXPECT_EQ(lineIndexForBlock(empty, 0), -1);
}

/**
 * @brief 钉住行方框走的是 Core 那套等比换算
 * @details 期望值手算：800x200 摆进 400x400 → 显示区 (0,150,400,100)，比例 0.5；
 *          图上 (100,20,170,16) 的行框 → (50,160,85,8)。预览与画框若各算一次，这里就会偏。
 */
TEST(OcrResultMapping, HighlightRectUsesTheSharedFit)
{
    const auto result = makeTwoLineResult();

    EXPECT_EQ(highlightRectFor(result, 0, {800, 200}, {400, 400}), QRect(50, 160, 85, 8));
    EXPECT_FALSE(highlightRectFor(result, -1, {800, 200}, {400, 400}).has_value());
    EXPECT_FALSE(highlightRectFor(result, 2, {800, 200}, {400, 400}).has_value());
    EXPECT_FALSE(highlightRectFor(result, 0, {800, 200}, {0, 0}).has_value());

    OcrResult withEmptyBox            = makeTwoLineResult();
    withEmptyBox.lines[0].boundingBox = QRect{};
    EXPECT_FALSE(highlightRectFor(withEmptyBox, 0, {800, 200}, {400, 400}).has_value()) << "后端给不出行框时不该在左上角画一个点";
}

/**
 * @brief 钉住摘要那一行写的是实测的行数、置信度与语言
 * @details 期望值逐字写死，不用被测函数算：置信度按百分数取整、语言取结果里的实际用值，
 *          这两条都是用户判断「认得准不准」的依据。
 */
TEST(OcrResultStatus, SummaryCarriesLinesConfidenceAndLanguage)
{
    const auto summary = statusLineFor(makeTwoLineResult(), {800, 200});
    EXPECT_EQ(summary, QString::fromUtf8("2 行 · 平均置信度 80% · 图像 800×200 · 语言 zh-Hans"));

    const QString emptySummary = statusLineFor(OcrResult{}, {800, 200});
    EXPECT_NE(emptySummary.indexOf(QString::fromUtf8("没认出文字")), -1) << "空结果要说「没字」，不能报 0 行让用户以为识别失败";
}

/**
 * @brief 钉住后端给不出置信度时摘要写「未提供」，并且不打任何百分数
 * @details 系统后端（Windows.Media.Ocr）根本没有逐词置信度这一路读数；打成「0%」会被读成
 *          「这张图认得很差」，用户于是去换图换语言，而那一条路本来不通。
 */
TEST(OcrResultStatus, SummaryNamesTheMissingConfidenceInsteadOfZeroPercent)
{
    OcrResult blind         = makeTwoLineResult();
    blind.averageConfidence = std::nullopt;

    const auto summary = statusLineFor(blind, {800, 200});
    EXPECT_NE(summary.indexOf(QString::fromUtf8("未提供")), -1) << "给不出的读数要直说，实际写着：" << summary.toStdString();
    EXPECT_EQ(summary.indexOf(QStringLiteral("%")), -1) << "还是打出了一个百分数，「没这个量」与「把握为零」在界面上分不开";
    EXPECT_NE(summary.indexOf(QString::fromUtf8("2 行")), -1) << "行数这类给得出的读数不该跟着一起消失";
}

/**
 * @brief 钉住换结果时文本、摘要与语言下拉一起刷新
 * @details 只刷文本不刷摘要，用户看到的就是上一轮的置信度；而程序化改下拉必须不发信号，
 *          否则一次 setResult 会触发又一轮识别。
 */
TEST_F(OcrResultWindowFixture, ResultRefreshUpdatesTextAndStatusTogether)
{
    auto window = makeWindow(makeTwoLineResult());
    EXPECT_EQ(window->statusText(), statusLineFor(makeTwoLineResult(), {800, 200}));

    OcrResult other         = makeTwoLineResult();
    other.language          = QStringLiteral("en");
    other.lines             = {other.lines.first()};
    other.averageConfidence = 0.5;
    window->setResult(makeImage({800, 200}), other);

    EXPECT_EQ(window->statusText(), statusLineFor(other, {800, 200}));
    EXPECT_EQ(window->selectedLanguage(), QStringLiteral("en")) << "语言下拉要跟结果里实际用的那一个";
}

/**
 * @brief 钉住复制出口写的是结果全文而不是选区
 * @details 两个按钮共用一个文本框，若「复制全部」读的是选区，用户按下去拿到的是上一次选中的片段。
 */
TEST_F(OcrResultWindowFixture, CopyAllWritesWholeResultAndSelectionNeedsASel)
{
    const auto result = makeTwoLineResult();
    auto       window = makeWindow(result);

    EXPECT_EQ(window->copyAllToClipboard(), result.text());
    EXPECT_EQ(QApplication::clipboard()->text(), result.text());

    EXPECT_EQ(window->copySelectionToClipboard(), QString{}) << "没有选区时不该把剪贴板清空或写进整段";
}

/**
 * @brief 钉住导出的两份文件内容来自导出器而不是界面拼的
 * @details 路径按用例名给独立文件；JSON 里必须带图片尺寸，否则方框坐标换一张图就对不上号。
 */
TEST_F(OcrResultWindowFixture, ExportsWriteExporterOutputVerbatim)
{
    const auto result = makeTwoLineResult();
    auto       window = makeWindow(result);
    const auto base   = std::filesystem::temp_directory_path() / std::format("GyanisScribe-OcrWindow-{}.txt", ::testing::UnitTest::GetInstance()->current_test_info()->name());
    const auto json   = std::filesystem::temp_directory_path() / std::format("GyanisScribe-OcrWindow-{}.json", ::testing::UnitTest::GetInstance()->current_test_info()->name());

    std::error_code cleanup;
    std::filesystem::remove(base, cleanup);
    std::filesystem::remove(json, cleanup);

    ASSERT_TRUE(static_cast<bool>(window->exportTxt(base)));
    ASSERT_TRUE(static_cast<bool>(window->exportJson(json)));

    const auto written = QString::fromUtf8(readWholeFile(base).c_str(), static_cast<qsizetype>(readWholeFile(base).size()));
    EXPECT_EQ(written, QStringLiteral("你好 世界\n第二行"));

    const auto jsonBytes   = readWholeFile(json);
    const auto writtenJson = QString::fromUtf8(jsonBytes.c_str(), static_cast<qsizetype>(jsonBytes.size()));
    EXPECT_NE(writtenJson.indexOf(QStringLiteral("800")), -1) << "JSON 要存图片尺寸供方框核对";
    EXPECT_NE(writtenJson.indexOf(QStringLiteral("zh-Hans")), -1);

    std::filesystem::remove(base, cleanup);
    std::filesystem::remove(json, cleanup);
}

/**
 * @brief 钉住语言下拉的选择会立刻要求重认
 * @details 换语言后还要用户再点一次「重新识别」是多余的第三步；信号必须发的是下拉里那句标签。
 */
TEST_F(OcrResultWindowFixture, LanguageChangeAsksControllerToRecognizeAgain)
{
    auto    window  = makeWindow(makeTwoLineResult());
    int     emitted = 0;
    QString emittedLanguage;
    QObject::connect(window.get(), &OcrResultWindow::recognizeRequested, window.get(),
                     [&emitted, &emittedLanguage](const QString &language)
                     {
                         ++emitted;
                         emittedLanguage = language;
                     });

    window->findChild<QComboBox *>()->setCurrentText(QStringLiteral("en"));

    EXPECT_EQ(emitted, 1) << "选完语言就该要求重认一次，不该等用户再点按钮";
    EXPECT_EQ(emittedLanguage, QStringLiteral("en")) << "信号带的必须是下拉里刚选的那个标签";
}

/**
 * @brief 钉住 OCR 摘要锁等宽、正文不加
 * @details 摘要里的行数、置信度、尺寸、语言是一组读数，比例字体会让这几个数随结果跳动；
 *          正文是用户要复制走的内容，锁等宽会改变字形观感，不属于排版策略的覆盖面。
 */
TEST_F(OcrResultWindowFixture, StatusUsesTabularFiguresButTheBodyDoesNot)
{
    auto window = makeWindow(makeTwoLineResult());

    QLabel *status = nullptr;
    for (auto *label: window->findChildren<QLabel *>())
    {
        if (label->accessibleName() == QStringLiteral("识别摘要"))
        {
            status = label;
            break;
        }
    }
    ASSERT_NE(status, nullptr) << "结果窗里没有「识别摘要」标签";

    QTextEdit *body = nullptr;
    for (auto *edit: window->findChildren<QTextEdit *>())
    {
        if (edit->accessibleName() == QStringLiteral("识别文本"))
        {
            body = edit;
            break;
        }
    }
    ASSERT_NE(body, nullptr) << "结果窗里没有「识别文本」正文";
#if QT_VERSION >= QT_VERSION_CHECK(6, 7, 0)
    EXPECT_EQ(status->font().featureValue(QFont::Tag("tnum")), 1u) << "OCR 摘要没锁等宽，计数与尺寸会跳";
    EXPECT_EQ(body->font().featureValue(QFont::Tag("tnum")), 0u) << "正文被误加了等宽数字，阅读字形变了";
#endif
}
