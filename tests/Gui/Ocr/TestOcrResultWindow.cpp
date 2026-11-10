#include "Gui/Ocr/OcrResultWindow.h"

#include "Core/Ocr/OcrTypes.h"
#include "Core/Settings/SettingsStore.h"
#include "Gui/Resources/IconProvider.h"
#include "Gui/Theme/ThemeManager.h"

#include <gtest/gtest.h>

#include <QApplication>
#include <QClipboard>
#include <QComboBox>
#include <QFile>
#include <QIcon>
#include <QImage>
#include <QLabel>
#include <QTemporaryDir>
#include <QTextEdit>
#include <QToolButton>

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

    /// 造一行两词的表格行：词框宽 60、间距 40，与表格检测用例同一口径
    OcrLine makeCellsLine(const int y, const QVector<QString> &texts)
    {
        OcrLine line;
        int     x = 10;
        for (const QString &text: texts)
        {
            const OcrWord word{text, QRect{x, y, 60, 20}};
            line.words.push_back(word);
            line.boundingBox = line.boundingBox.isNull() ? word.boundingBox : line.boundingBox.united(word.boundingBox);
            x += 100;
        }
        return line;
    }

    /// 一行两词“测试文本”：有文字、无表格
    OcrResult makeTextResult()
    {
        OcrResult result;
        result.language          = QStringLiteral("zh-Hans");
        result.averageConfidence = 0.9;
        OcrLine line;
        line.words       = {OcrWord{QStringLiteral("测试"), QRect{100, 20, 40, 16}}, OcrWord{QStringLiteral("文本"), QRect{150, 20, 40, 16}}};
        line.boundingBox = QRect{100, 20, 90, 16};
        result.lines     = {line};
        return result;
    }

    /// 2×2 简单表：A/B 与 1/2 两行
    OcrResult makeTableResult()
    {
        OcrResult result;
        result.language          = QStringLiteral("zh-Hans");
        result.averageConfidence = 0.9;
        result.lines             = {makeCellsLine(10, {QStringLiteral("A"), QStringLiteral("B")}), makeCellsLine(40, {QStringLiteral("1"), QStringLiteral("2")})};
        return result;
    }

    /// 复杂表：首行一个宽 140 的合并表头，下面两行各两词，结构无法按列还原
    OcrResult makeComplexTableResult()
    {
        OcrResult result;
        result.language          = QStringLiteral("zh-Hans");
        result.averageConfidence = 0.9;
        OcrLine header;
        header.words       = {OcrWord{QStringLiteral("合并表头"), QRect{10, 10, 140, 28}}};
        header.boundingBox = QRect{10, 10, 140, 28};
        result.lines       = {header, makeCellsLine(50, {QStringLiteral("A"), QStringLiteral("B")}), makeCellsLine(80, {QStringLiteral("1"), QStringLiteral("2")})};
        return result;
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
    EXPECT_NE(emptySummary.indexOf(QString::fromUtf8("未识别到文字")), -1) << "空结果要说「没字」，不能报 0 行让用户以为识别失败";
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
 * @brief 钉住复制选区时把段落分隔符与软换行都换成普通换行
 * @details QTextCursor::selectedText() 用 U+2029 表示段落分隔、U+2028 表示段内软换行，
 *          直接塞进剪贴板后粘到别的程序里会多出异常字符。复制全部走的是结果文本，本来就没有这两档。
 */
TEST_F(OcrResultWindowFixture, CopySelectionTurnsSeparatorsIntoPlainNewlines)
{
    auto window = makeWindow(makeTwoLineResult());

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
    body->selectAll();

    const auto selected = window->copySelectionToClipboard();
    ASSERT_FALSE(selected.isEmpty()) << "全选之后复制选区却交回空串";
    EXPECT_EQ(selected.indexOf(QChar::ParagraphSeparator), -1) << "复制出去的文本还带着 U+2029";
    EXPECT_EQ(selected.indexOf(QChar::LineSeparator), -1) << "复制出去的文本还带着 U+2028";
    EXPECT_NE(selected.indexOf(QLatin1Char('\n')), -1) << "两行之间没有换行";
    EXPECT_EQ(selected, QString::fromUtf8("你好 世界\n第二行")) << "替换分隔符时把行内容弄丢了";
    EXPECT_EQ(QApplication::clipboard()->text(), selected);
    EXPECT_EQ(QApplication::clipboard()->text().indexOf(QChar::ParagraphSeparator), -1) << "剪贴板里仍留着 U+2029";

    // 软换行落在一个块里，不经过段落分隔：只认 U+2029 的替换在这一档上不起作用
    body->setPlainText(QStringLiteral("你好 世界") + QChar::LineSeparator + QStringLiteral("第二行"));
    body->selectAll();
    const auto withSoftBreak = window->copySelectionToClipboard();
    ASSERT_FALSE(withSoftBreak.isEmpty()) << "带软换行的那一份全选之后复制选区却交回空串";
    EXPECT_EQ(withSoftBreak.indexOf(QChar::LineSeparator), -1) << "复制出去的文本还带着 U+2028";
    EXPECT_EQ(withSoftBreak, QString::fromUtf8("你好 世界\n第二行")) << "软换行没有换成普通换行";
    EXPECT_EQ(QApplication::clipboard()->text().indexOf(QChar::LineSeparator), -1) << "剪贴板里仍留着 U+2028";
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

/**
 * @brief 钉住空结果不给任何内容动作
 * @details 没字就没 Markdown、没表就没 CSV；整行也不该占着位置，否则空结果窗里会多出一排点了没反应的按钮。
 */
TEST_F(OcrResultWindowFixture, HidesContentActionsForEmptyResult)
{
    auto window = makeWindow(OcrResult{});
    EXPECT_FALSE(window->hasMarkdownAction());
    EXPECT_FALSE(window->hasCsvAction());

    auto *contentActions = window->findChild<QWidget *>(QStringLiteral("ocrContentActions"));
    ASSERT_NE(contentActions, nullptr) << "结果窗里没有内容动作行";
    EXPECT_TRUE(contentActions->isHidden()) << "两个动作都不可用时整行应当隐藏";
    auto *copyMarkdown = window->findChild<QToolButton *>(QStringLiteral("ocrCopyMarkdown"));
    ASSERT_NE(copyMarkdown, nullptr) << "结果窗里没有「复制 Markdown」按钮";
    EXPECT_TRUE(copyMarkdown->isHidden());
    auto *exportCsv = window->findChild<QToolButton *>(QStringLiteral("ocrExportCsv"));
    ASSERT_NE(exportCsv, nullptr) << "结果窗里没有「导出表格 CSV」按钮";
    EXPECT_TRUE(exportCsv->isHidden());
}

/**
 * @brief 钉住有文字时给出 Markdown 动作并把同一份内容写进剪贴板
 * @details 复制出口自己生成 Markdown，界面不另拼一份；否则复制到的内容与导出/预览会各说各话。
 */
TEST_F(OcrResultWindowFixture, ShowsMarkdownAndCopiesItForTextResult)
{
    const OcrResult result = makeTextResult();
    auto            window = makeWindow(result);
    EXPECT_TRUE(window->hasMarkdownAction());
    EXPECT_FALSE(window->hasCsvAction());

    auto *copyMarkdown = window->findChild<QToolButton *>(QStringLiteral("ocrCopyMarkdown"));
    ASSERT_NE(copyMarkdown, nullptr) << "结果窗里没有「复制 Markdown」按钮";
    EXPECT_FALSE(copyMarkdown->isHidden()) << "有文字时「复制 Markdown」应当可见";
    auto *exportCsv = window->findChild<QToolButton *>(QStringLiteral("ocrExportCsv"));
    ASSERT_NE(exportCsv, nullptr) << "结果窗里没有「导出表格 CSV」按钮";
    EXPECT_TRUE(exportCsv->isHidden()) << "没有表格时不该出现 CSV 按钮";

    // 用例进程没有事件循环：先前写剪贴板留下的 OLE 消息没人处理，不先泵一次队列，
    // setText 会被外部进程占着的剪贴板卡到超时，读完仍是空的
    QCoreApplication::processEvents();
    const QString markdown = window->copyMarkdownToClipboard();
    EXPECT_FALSE(markdown.isEmpty());
    EXPECT_EQ(QGuiApplication::clipboard()->text(), markdown);
}

/**
 * @brief 钉住简单表才出现 CSV 动作，导出文件带 BOM
 * @details Excel 认 BOM 才按 UTF-8 打开；没有这三个字节时中文表头会整列乱码。
 */
TEST_F(OcrResultWindowFixture, ShowsCsvOnlyForSimpleTableAndExportsBomFile)
{
    auto window = makeWindow(makeTableResult());
    EXPECT_TRUE(window->hasMarkdownAction());
    EXPECT_TRUE(window->hasCsvAction());

    auto *exportCsv = window->findChild<QToolButton *>(QStringLiteral("ocrExportCsv"));
    ASSERT_NE(exportCsv, nullptr) << "结果窗里没有「导出表格 CSV」按钮";
    EXPECT_FALSE(exportCsv->isHidden()) << "检测到简单表时 CSV 按钮应当可见";
    auto *contentActions = window->findChild<QWidget *>(QStringLiteral("ocrContentActions"));
    ASSERT_NE(contentActions, nullptr) << "结果窗里没有内容动作行";
    EXPECT_FALSE(contentActions->isHidden());
    EXPECT_TRUE(window->statusText().contains(QStringLiteral("可导出 CSV"))) << "简单表要在状态行说明可以导出 CSV";

    QTemporaryDir temp;
    ASSERT_TRUE(temp.isValid());
    const auto path = std::filesystem::path(temp.path().toStdString()) / "table.csv";
    ASSERT_TRUE(window->exportCsv(path).has_value());
    QFile file{QString::fromStdWString(path.wstring())};
    ASSERT_TRUE(file.open(QIODevice::ReadOnly));
    const QByteArray bytes = file.readAll();
    EXPECT_TRUE(bytes.startsWith("\xEF\xBB\xBF"));
    EXPECT_TRUE(bytes.contains("A"));
}

/**
 * @brief 钉住复杂表不给 CSV 动作，并在状态行说明原因
 * @details 复杂表按列还原会丢内容，宁可不导出；不写一句说明，用户只会以为功能没做。
 */
TEST_F(OcrResultWindowFixture, HidesCsvAndExplainsComplexTable)
{
    auto window = makeWindow(makeComplexTableResult());
    EXPECT_TRUE(window->hasMarkdownAction());
    EXPECT_FALSE(window->hasCsvAction());
    EXPECT_TRUE(window->statusText().contains(QStringLiteral("复杂表格")));
    auto *contentActions = window->findChild<QWidget *>(QStringLiteral("ocrContentActions"));
    ASSERT_NE(contentActions, nullptr) << "结果窗里没有内容动作行";
    EXPECT_FALSE(contentActions->isHidden()) << "Markdown 动作仍在，内容行不该整行隐藏";
    auto *copyMarkdown = window->findChild<QToolButton *>(QStringLiteral("ocrCopyMarkdown"));
    ASSERT_NE(copyMarkdown, nullptr) << "结果窗里没有「复制 Markdown」按钮";
    EXPECT_FALSE(copyMarkdown->isHidden()) << "复杂表仍有文字可复制成 Markdown";
    auto *exportCsv = window->findChild<QToolButton *>(QStringLiteral("ocrExportCsv"));
    ASSERT_NE(exportCsv, nullptr) << "结果窗里没有「导出表格 CSV」按钮";
    EXPECT_TRUE(exportCsv->isHidden()) << "复杂表不能露出 CSV 按钮";
}

/**
 * @brief 钉住换结果时内容动作跟着刷新
 * @details 重识别与换语言都走 setResult；只刷文本不刷按钮，用户会对着上一张图才有的动作。
 */
TEST_F(OcrResultWindowFixture, SetResultUpdatesContentActions)
{
    auto  window         = makeWindow(OcrResult{});
    auto *contentActions = window->findChild<QWidget *>(QStringLiteral("ocrContentActions"));
    auto *copyMarkdown   = window->findChild<QToolButton *>(QStringLiteral("ocrCopyMarkdown"));
    auto *exportCsv      = window->findChild<QToolButton *>(QStringLiteral("ocrExportCsv"));
    ASSERT_NE(contentActions, nullptr) << "结果窗里没有内容动作行";
    ASSERT_NE(copyMarkdown, nullptr) << "结果窗里没有「复制 Markdown」按钮";
    ASSERT_NE(exportCsv, nullptr) << "结果窗里没有「导出表格 CSV」按钮";
    EXPECT_TRUE(contentActions->isHidden()) << "空结果不该留下内容动作行";
    EXPECT_TRUE(copyMarkdown->isHidden());
    EXPECT_TRUE(exportCsv->isHidden());

    const OcrResult text = makeTextResult();
    window->setResult(makeImage({800, 200}), text);
    EXPECT_TRUE(window->hasMarkdownAction());
    EXPECT_FALSE(window->hasCsvAction());
    EXPECT_FALSE(contentActions->isHidden()) << "有文字时内容动作行应当出现";
    EXPECT_FALSE(copyMarkdown->isHidden()) << "换成有文字的结果后 Markdown 按钮应当出现";
    EXPECT_TRUE(exportCsv->isHidden()) << "没有表格时不该出现 CSV 按钮";
    EXPECT_EQ(window->statusText(), statusLineFor(text, {800, 200})) << "状态行不该留着上一份结果的表格提示";

    window->setResult(makeImage({800, 200}), makeTableResult());
    EXPECT_TRUE(window->hasMarkdownAction());
    EXPECT_TRUE(window->hasCsvAction());
    EXPECT_FALSE(contentActions->isHidden());
    EXPECT_FALSE(copyMarkdown->isHidden());
    EXPECT_FALSE(exportCsv->isHidden()) << "换成简单表后 CSV 按钮应当出现";
    EXPECT_TRUE(window->statusText().contains(QStringLiteral("可导出 CSV"))) << "状态行没跟着新结果刷新";
}

/**
 * @brief 钉住只有空白的识别结果不算有字
 * @details 空白词拼不出 Markdown；按钮若露出来，点下去只会得到「没有认出可复制的文字」。
 */
TEST_F(OcrResultWindowFixture, TreatsWhitespaceOnlyResultAsNoText)
{
    OcrResult blank;
    blank.language = QStringLiteral("zh-Hans");
    OcrLine line;
    line.words       = {OcrWord{QStringLiteral("   "), QRect{100, 20, 30, 16}}};
    line.boundingBox = line.words.front().boundingBox;
    blank.lines      = {line};

    auto window = makeWindow(blank);
    EXPECT_TRUE(blank.text().trimmed().isEmpty()) << "样本本身得是纯空白";
    EXPECT_FALSE(window->hasMarkdownAction());
    EXPECT_TRUE(window->copyMarkdownToClipboard().isEmpty());
    auto *copyMarkdown = window->findChild<QToolButton *>(QStringLiteral("ocrCopyMarkdown"));
    ASSERT_NE(copyMarkdown, nullptr) << "结果窗里没有「复制 Markdown」按钮";
    EXPECT_TRUE(copyMarkdown->isHidden()) << "只有空白时不该露出 Markdown 按钮";
    auto *contentActions = window->findChild<QWidget *>(QStringLiteral("ocrContentActions"));
    ASSERT_NE(contentActions, nullptr) << "结果窗里没有内容动作行";
    EXPECT_TRUE(contentActions->isHidden()) << "没有可用的内容动作时整行应当隐藏";
}

/**
 * @brief 钉住识别结果窗的任务栏图标走品牌入口
 * @details 结果窗是独立顶层窗口，任务栏会单独占一格；挂界面里的 Ocr 图标时，
 *          同一进程的两个窗口会显示成两枚不同的标记。512 只有品牌图集才有。
 */
TEST_F(OcrResultWindowFixture, WindowIconUsesTheBrandMark)
{
    auto       window = makeWindow(makeTwoLineResult());
    const auto icon   = window->windowIcon();
    ASSERT_FALSE(icon.isNull()) << "识别结果窗没有窗口图标";
    EXPECT_TRUE(icon.availableSizes().contains(QSize(512, 512))) << "结果窗没走品牌入口：用的还是界面图标";
}
