#include "Annotation/AnnotationToolbar.h"
#include "Annotation/AnnotationToolbarGeometry.h"

#include "Annotation/AnnotationTool.h"
#include "Settings/SettingsKeys.h"
#include "Settings/SettingsStore.h"
#include "Ui/DesignTokens.h"
#include "Ui/IconProvider.h"

#include <gtest/gtest.h>

#include <QApplication>
#include <QColor>
#include <QComboBox>
#include <QFont>
#include <QFontComboBox>
#include <QFontDatabase>
#include <QImage>
#include <QRect>
#include <QSize>
#include <QSlider>
#include <QString>
#include <QStringList>
#include <QToolButton>
#include <QWidget>

#include <array>
#include <cstdlib>
#include <filesystem>
#include <format>
#include <memory>
#include <string_view>
#include <vector>

using GyanisScribe::Annotation::AnnotationToolbar;
using GyanisScribe::Annotation::placeToolbar;
using GyanisScribe::Annotation::ToolbarSide;
using GyanisScribe::Annotation::ToolKind;
using GyanisScribe::Core::ErrorCode;
using GyanisScribe::Settings::SettingsStore;
using GyanisScribe::Ui::IconProvider;
using GyanisScribe::Ui::kAnnotationPalette;
using GyanisScribe::Ui::kAnnotationWidths;
using GyanisScribe::Ui::kFontSizes;

namespace
{
    std::unique_ptr<QApplication> application;
    std::unique_ptr<IconProvider> icons;

    /// 工具按钮的中文名，顺序与工具表同解；这一份是照需求第五节抄来的独立期望值
    constexpr std::array<std::string_view, 12> kToolLabels = {"矩形", "椭圆", "箭头", "画笔", "高亮", "马赛克", "文字", "序号", "直线", "高斯模糊", "橡皮擦", "折线"};

    /// 工具按钮该在提示里带上的字母键位，同样是一份独立抄来的期望值：与 kToolTable 第四列交叉核对
    constexpr std::array<std::string_view, 12> kToolShortcuts = {"R", "E", "A", "P", "H", "M", "T", "N", "L", "B", "X", "G"};

    /// 动作按钮的中文名，同样按需求第六节的排列顺序抄一份
    constexpr std::array<std::string_view, 4> kActionLabels = {"复制到剪贴板", "保存为文件", "贴到屏幕上", "识别文字"};

    /// 全 1920x1080、原点的可用区，几何用例的默认舞台
    constexpr QRect kScreen{0, 0, 1920, 1080};

    QString text(std::string_view value)
    {
        return QString::fromUtf8(value.data(), static_cast<qsizetype>(value.size()));
    }

    /**
     * @brief 工具栏控件用例的夹具
     * @details 工具栏要真建控件、真点按钮，因此需要 QApplication 与已经接好的图标供给器。
     */
    class ToolbarFixture : public ::testing::Test
    {
    protected:
        static void SetUpTestSuite()
        {
            // argv 与 argc 的缓冲要有静态存储期：Qt 只存指针不拷贝，
            // 而 QCoreApplication::applicationDirPath() 这类调用是在装置函数返回之后才读它们。
            static int   argumentCount  = 1;
            static char  argumentZero[] = "TestAnnotationToolbar";
            static char *arguments[]    = {argumentZero, nullptr};
            application                 = std::make_unique<QApplication>(argumentCount, arguments);
            icons                       = std::make_unique<IconProvider>();
            // 描边色钉死成深色，不让它跟着本机深浅色走：本机是深色主题时默认描边就是白，
            // 「白色色块有没有画边框」就变成拿白对白判，永远成立
            icons->setTintColor(QColor{20, 20, 20});
        }

        static void TearDownTestSuite()
        {
            icons.reset();
            application.reset();
        }

        void SetUp() override
        {
            // 每条用例一份自己的 INI：ctest 并行时每个用例是一个独立进程，
            // 共用一份配置的话「最近使用颜色」那条写进去会读到别人写的值
            const auto *info = ::testing::UnitTest::GetInstance()->current_test_info();
            m_iniPath        = std::filesystem::temp_directory_path() / std::format("GyanisScribe-Toolbar-{}-{}.ini", info->test_suite_name(), info->name());
            std::error_code ignored;
            std::filesystem::remove(m_iniPath, ignored);
            m_settings = std::make_unique<SettingsStore>(m_iniPath);
        }

        void TearDown() override
        {
            // 先放 store（它会冲刷配置），再删文件
            m_settings.reset();
            std::error_code ignored;
            std::filesystem::remove(m_iniPath, ignored);
        }

        /// 本用例独占的配置文件与 store，工具栏的最近使用颜色就落在这里
        std::filesystem::path          m_iniPath;
        std::unique_ptr<SettingsStore> m_settings;

        /// 工具栏每条信号的落地处，用例按「谁发了几次」判接线是否成环
        struct SignalLog
        {
            std::vector<ToolKind> tools;            ///< toolChanged 收到的工具
            std::vector<QColor>   colors;           ///< colorChanged 收到的颜色
            std::vector<int>      widths;           ///< widthChanged 收到的线宽
            std::vector<int>      fontSizes;        ///< fontSizeChanged 收到的字号
            std::vector<int>      fillStyles;       ///< fillStyleChanged 收到的填充档
            std::vector<int>      lineStyles;       ///< lineStyleChanged 收到的线型档
            std::vector<int>      roundedRects;     ///< roundedRectChanged 收到的开关（1 开 0 关）
            std::vector<int>      doubleHeaded;     ///< doubleHeadedChanged 收到的开关（1 开 0 关）
            std::vector<int>      ellipticals;      ///< ellipticalChanged 收到的开关（1 椭圆 0 矩形）
            std::vector<int>      mosaicBlockSizes; ///< mosaicBlockSizeChanged 收到的粒度
            std::vector<int>      highlightAlphas;  ///< highlightAlphaChanged 收到的透明度
            std::vector<int>      highlightWidths;  ///< highlightWidthFactorChanged 收到的加宽倍数
            std::vector<int>      arrowHeadStyles;  ///< arrowHeadStyleChanged 收到的款式（0 实心、1 空心、2 开口）
            std::vector<double>   arrowHeadScales;  ///< arrowHeadScaleChanged 收到的大小倍数
            std::vector<QString>  fontFamilies;     ///< fontFamilyChanged 收到的字体族
            std::vector<int>      textBolds;        ///< textBoldChanged 收到的开关（1 开 0 关）
            std::vector<int>      textItalics;      ///< textItalicChanged 收到的开关（1 开 0 关）
            std::vector<int>      textOutlines;     ///< textOutlineChanged 收到的开关（1 开 0 关）
            std::vector<int>      textBackgrounds;  ///< textBackgroundChanged 收到的开关（1 开 0 关）
            int                   actions{};        ///< 四个动作按钮的合计触发次数
            int                   undo{};           ///< undoClicked 次数
            int                   redo{};           ///< redoClicked 次数
        };

        /// 建一把工具栏，并把它的每条信号都记进 log
        [[nodiscard]] std::unique_ptr<AnnotationToolbar> makeToolbarLogged(SignalLog &log)
        {
            auto toolbar = std::make_unique<AnnotationToolbar>(*m_settings, *icons);
            QObject::connect(toolbar.get(), &AnnotationToolbar::toolChanged, [&log](ToolKind kind) { log.tools.push_back(kind); });
            QObject::connect(toolbar.get(), &AnnotationToolbar::colorChanged, [&log](const QColor &color) { log.colors.push_back(color); });
            QObject::connect(toolbar.get(), &AnnotationToolbar::widthChanged, [&log](int width) { log.widths.push_back(width); });
            QObject::connect(toolbar.get(), &AnnotationToolbar::fontSizeChanged, [&log](int fontSize) { log.fontSizes.push_back(fontSize); });
            QObject::connect(toolbar.get(), &AnnotationToolbar::fillStyleChanged, [&log](int style) { log.fillStyles.push_back(style); });
            QObject::connect(toolbar.get(), &AnnotationToolbar::lineStyleChanged, [&log](int style) { log.lineStyles.push_back(style); });
            QObject::connect(toolbar.get(), &AnnotationToolbar::roundedRectChanged, [&log](bool enabled) { log.roundedRects.push_back(enabled ? 1 : 0); });
            QObject::connect(toolbar.get(), &AnnotationToolbar::doubleHeadedChanged, [&log](bool enabled) { log.doubleHeaded.push_back(enabled ? 1 : 0); });
            QObject::connect(toolbar.get(), &AnnotationToolbar::ellipticalChanged, [&log](bool enabled) { log.ellipticals.push_back(enabled ? 1 : 0); });
            QObject::connect(toolbar.get(), &AnnotationToolbar::mosaicBlockSizeChanged, [&log](int blockSize) { log.mosaicBlockSizes.push_back(blockSize); });
            QObject::connect(toolbar.get(), &AnnotationToolbar::highlightAlphaChanged, [&log](int alpha) { log.highlightAlphas.push_back(alpha); });
            QObject::connect(toolbar.get(), &AnnotationToolbar::highlightWidthFactorChanged, [&log](int factor) { log.highlightWidths.push_back(factor); });
            QObject::connect(toolbar.get(), &AnnotationToolbar::arrowHeadStyleChanged, [&log](int style) { log.arrowHeadStyles.push_back(style); });
            QObject::connect(toolbar.get(), &AnnotationToolbar::arrowHeadScaleChanged, [&log](double scale) { log.arrowHeadScales.push_back(scale); });
            QObject::connect(toolbar.get(), &AnnotationToolbar::fontFamilyChanged, [&log](const QString &family) { log.fontFamilies.push_back(family); });
            QObject::connect(toolbar.get(), &AnnotationToolbar::textBoldChanged, [&log](bool enabled) { log.textBolds.push_back(enabled ? 1 : 0); });
            QObject::connect(toolbar.get(), &AnnotationToolbar::textItalicChanged, [&log](bool enabled) { log.textItalics.push_back(enabled ? 1 : 0); });
            QObject::connect(toolbar.get(), &AnnotationToolbar::textOutlineChanged, [&log](bool enabled) { log.textOutlines.push_back(enabled ? 1 : 0); });
            QObject::connect(toolbar.get(), &AnnotationToolbar::textBackgroundChanged, [&log](bool enabled) { log.textBackgrounds.push_back(enabled ? 1 : 0); });
            QObject::connect(toolbar.get(), &AnnotationToolbar::undoClicked, [&log] { ++log.undo; });
            QObject::connect(toolbar.get(), &AnnotationToolbar::redoClicked, [&log] { ++log.redo; });
            for (auto signal: {&AnnotationToolbar::copyClicked, &AnnotationToolbar::saveClicked, &AnnotationToolbar::pinClicked, &AnnotationToolbar::ocrClicked})
            {
                QObject::connect(toolbar.get(), signal, [&log] { ++log.actions; });
            }
            return toolbar;
        }

        /// 按无障碍名称找按钮：名称是需求的可访问性契约，用它定位比记序号更稳
        [[nodiscard]] static QToolButton *buttonByLabel(const AnnotationToolbar &toolbar, std::string_view label)
        {
            for (auto *button: toolbar.findChildren<QToolButton *>())
            {
                if (button->accessibleName() == text(label))
                {
                    return button;
                }
            }
            return nullptr;
        }
    };


    /**
     * @brief 钉住填充按钮：点一下走一档（无 → 半透明 → 实心 → 无），提示与无障碍名跟着这一档走
     * @details 一枚图标说不清现在是哪一档，所以提示就是它的读数：三档各点一次，提示要三种都对得上。
     */
    TEST_F(ToolbarFixture, FillButtonCyclesAndNamesTheCurrentStyle)
    {
        SignalLog log;
        auto      toolbar = makeToolbarLogged(log);
        auto     *button  = buttonByLabel(*toolbar, "填充");
        ASSERT_NE(button, nullptr) << "工具栏上找不到填充按钮";

        EXPECT_EQ(toolbar->currentFillStyle(), 0) << "起点该是「无」";
        button->click();
        EXPECT_EQ(toolbar->currentFillStyle(), 1);
        EXPECT_EQ(log.fillStyles, std::vector<int>{1}) << "走了一档却没把信号发出去";
        EXPECT_EQ(button->toolTip(), QString::fromUtf8("填充：半透明")) << "提示没跟上当前档";

        button->click();
        EXPECT_EQ(toolbar->currentFillStyle(), 2);
        EXPECT_EQ(button->toolTip(), QString::fromUtf8("填充：实心"));

        button->click();
        EXPECT_EQ(toolbar->currentFillStyle(), 0) << "三档之后该回到「无」";
        EXPECT_EQ(button->accessibleName(), QString::fromUtf8("填充：无")) << "无障碍名没跟上当前档";
    }

    /**
     * @brief 钉住双向箭头开关：点一下开、再点一下关，按下态、提示与信号三者同解
     * @details 与「填充」那种走档按钮不同，这是个保持按下态的开关：按钮的 checked、工具栏读数
     *          与发出去的信号必须同时翻，否则按钮按着、画出来却还是单头。
     */
    TEST_F(ToolbarFixture, DoubleArrowButtonTogglesAndNamesTheState)
    {
        SignalLog log;
        auto      toolbar = makeToolbarLogged(log);
        auto     *button  = buttonByLabel(*toolbar, "双向箭头");
        ASSERT_NE(button, nullptr) << "工具栏上找不到双向箭头按钮";

        EXPECT_FALSE(toolbar->currentDoubleHeaded()) << "起点该是关";
        EXPECT_FALSE(button->isChecked()) << "起点按钮该是弹起态";

        button->click();
        EXPECT_TRUE(toolbar->currentDoubleHeaded());
        EXPECT_EQ(log.doubleHeaded, std::vector<int>{1}) << "开了却没把信号发出去";
        EXPECT_TRUE(button->isChecked()) << "开关开了按钮却没保持按下";
        EXPECT_EQ(button->toolTip(), QString::fromUtf8("双向箭头：开")) << "提示没跟上开关";

        button->click();
        EXPECT_FALSE(toolbar->currentDoubleHeaded());
        EXPECT_EQ(log.doubleHeaded, (std::vector<int>{1, 0}));
        EXPECT_FALSE(button->isChecked()) << "关掉之后按钮还按着";
        EXPECT_EQ(button->accessibleName(), QString::fromUtf8("双向箭头：关")) << "无障碍名没跟上开关";
    }

    /**
     * @brief 钉住马赛克粒度按钮：点一下走一档（中 → 粗 → 细 → 中），提示与无障碍名跟着读数走
     * @details 与填充同一套口径：一枚图标说不清现在是哪一档，提示就是它的读数。
     */
    TEST_F(ToolbarFixture, MosaicGrainButtonCyclesAndNamesTheStep)
    {
        SignalLog log;
        auto      toolbar = makeToolbarLogged(log);
        auto     *button  = buttonByLabel(*toolbar, "马赛克粒度");
        ASSERT_NE(button, nullptr) << "工具栏上找不到马赛克粒度按钮";

        EXPECT_EQ(toolbar->currentMosaicBlockSize(), 12) << "起点该是「中」12";
        button->click();
        EXPECT_EQ(toolbar->currentMosaicBlockSize(), 20);
        EXPECT_EQ(log.mosaicBlockSizes, std::vector<int>{20}) << "走了一档却没把信号发出去";
        EXPECT_EQ(button->toolTip(), QString::fromUtf8("马赛克粒度：粗")) << "提示没跟上当前档";

        button->click();
        EXPECT_EQ(toolbar->currentMosaicBlockSize(), 8);
        EXPECT_EQ(button->toolTip(), QString::fromUtf8("马赛克粒度：细"));

        button->click();
        EXPECT_EQ(toolbar->currentMosaicBlockSize(), 12) << "三档之后该回到「中」";
        EXPECT_EQ(button->accessibleName(), QString::fromUtf8("马赛克粒度：中")) << "无障碍名没跟上当前档";
    }

    /**
     * @brief 钉住圆角开关：保持按下态的开关，提示与信号一起走
     */
    TEST_F(ToolbarFixture, RoundedRectTogglesAndNamesTheState)
    {
        SignalLog log;
        auto      toolbar = makeToolbarLogged(log);
        auto     *button  = buttonByLabel(*toolbar, "圆角");
        ASSERT_NE(button, nullptr) << "工具栏上找不到圆角按钮";

        EXPECT_FALSE(toolbar->currentRoundedRect()) << "起点该是关";
        button->click();
        EXPECT_TRUE(toolbar->currentRoundedRect());
        EXPECT_EQ(log.roundedRects, std::vector<int>{1}) << "开了却没把信号发出去";
        EXPECT_EQ(button->toolTip(), QString::fromUtf8("圆角：开")) << "提示没跟上";

        button->click();
        EXPECT_FALSE(toolbar->currentRoundedRect());
        EXPECT_EQ(log.roundedRects, (std::vector<int>{1, 0}));
        EXPECT_EQ(button->toolTip(), QString::fromUtf8("圆角：关"));
    }

    /**
     * @brief 钉住椭圆形状开关：保持按下态的开关，提示与信号一起走
     */
    TEST_F(ToolbarFixture, EllipticalTogglesAndNamesTheState)
    {
        SignalLog log;
        auto      toolbar = makeToolbarLogged(log);
        auto     *button  = buttonByLabel(*toolbar, "椭圆形状");
        ASSERT_NE(button, nullptr) << "工具栏上找不到椭圆形状按钮";

        EXPECT_FALSE(toolbar->currentElliptical()) << "起点该是矩形档";
        button->click();
        EXPECT_TRUE(toolbar->currentElliptical());
        EXPECT_EQ(log.ellipticals, std::vector<int>{1}) << "开起来却没把信号发出去";
        EXPECT_EQ(button->toolTip(), QString::fromUtf8("椭圆形状：椭圆")) << "提示没跟上";

        button->click();
        EXPECT_FALSE(toolbar->currentElliptical());
        EXPECT_EQ(log.ellipticals, (std::vector<int>{1, 0}));
        EXPECT_EQ(button->accessibleName(), QString::fromUtf8("椭圆形状：矩形")) << "无障碍名没跟上";
    }

    /**
     * @brief 钉住线型按钮的三档循环：起点「实线」（与旧写法同解），信号与提示一起走
     */
    TEST_F(ToolbarFixture, LineStyleButtonCyclesAndNamesTheStep)
    {
        SignalLog log;
        auto      toolbar = makeToolbarLogged(log);
        auto     *button  = buttonByLabel(*toolbar, "线型");
        ASSERT_NE(button, nullptr) << "工具栏上找不到线型按钮";

        EXPECT_EQ(toolbar->currentLineStyle(), 0) << "起点该是「实线」：这一格出现之前只有这一档";
        button->click();
        EXPECT_EQ(toolbar->currentLineStyle(), 1);
        EXPECT_EQ(log.lineStyles, std::vector<int>{1}) << "走了一档却没把信号发出去";
        EXPECT_EQ(button->toolTip(), QString::fromUtf8("线型：虚线")) << "提示没跟上当前档";

        button->click();
        EXPECT_EQ(toolbar->currentLineStyle(), 2);
        EXPECT_EQ(button->toolTip(), QString::fromUtf8("线型：点线"));

        button->click();
        EXPECT_EQ(toolbar->currentLineStyle(), 0) << "三档之后该回到「实线」";
        EXPECT_EQ(button->accessibleName(), QString::fromUtf8("线型：实线")) << "无障碍名没跟上当前档";
    }

    /**
     * @brief 钉住高亮透明度按钮：点一下走一档（中 → 重 → 轻 → 中）
     */
    TEST_F(ToolbarFixture, HighlightAlphaButtonCyclesAndNamesTheStep)
    {
        SignalLog log;
        auto      toolbar = makeToolbarLogged(log);
        auto     *button  = buttonByLabel(*toolbar, "高亮透明度");
        ASSERT_NE(button, nullptr) << "工具栏上找不到高亮透明度按钮";

        EXPECT_EQ(toolbar->currentHighlightAlpha(), 110) << "起点该是「中」110";
        button->click();
        EXPECT_EQ(toolbar->currentHighlightAlpha(), 170);
        EXPECT_EQ(log.highlightAlphas, std::vector<int>{170}) << "走了一档却没把信号发出去";
        EXPECT_EQ(button->toolTip(), QString::fromUtf8("高亮透明度：重")) << "提示没跟上当前档";

        button->click();
        EXPECT_EQ(toolbar->currentHighlightAlpha(), 60);
        EXPECT_EQ(button->toolTip(), QString::fromUtf8("高亮透明度：轻"));

        button->click();
        EXPECT_EQ(toolbar->currentHighlightAlpha(), 110) << "三档之后该回到「中」";
        EXPECT_EQ(button->accessibleName(), QString::fromUtf8("高亮透明度：中")) << "无障碍名没跟上当前档";
    }

    /**
     * @brief 钉住高亮加宽按钮的三档循环：起点「中」=4（与旧写死值同解），信号与提示一起走
     */
    TEST_F(ToolbarFixture, HighlightWidthButtonCyclesAndNamesTheStep)
    {
        SignalLog log;
        auto      toolbar = makeToolbarLogged(log);
        auto     *button  = buttonByLabel(*toolbar, "高亮加宽");
        ASSERT_NE(button, nullptr) << "工具栏上找不到高亮加宽按钮";

        EXPECT_EQ(toolbar->currentHighlightWidthFactor(), 4) << "起点该是「中」4：与这一格出现之前的写死值同解，老图样不受影响";
        button->click();
        EXPECT_EQ(toolbar->currentHighlightWidthFactor(), 8);
        EXPECT_EQ(log.highlightWidths, std::vector<int>{8}) << "走了一档却没把信号发出去";
        EXPECT_EQ(button->toolTip(), QString::fromUtf8("高亮加宽：宽")) << "提示没跟上当前档";

        button->click();
        EXPECT_EQ(toolbar->currentHighlightWidthFactor(), 2);
        button->click();
        EXPECT_EQ(toolbar->currentHighlightWidthFactor(), 4) << "三档之后该回到「中」";
        EXPECT_EQ(button->accessibleName(), QString::fromUtf8("高亮加宽：中")) << "无障碍名没跟上当前档";
    }

    /**
     * @brief 钉住箭头款式按钮的三档循环：起点「实心」（与旧写法同解），信号与提示一起走
     */
    TEST_F(ToolbarFixture, ArrowHeadStyleButtonCyclesAndNamesTheStep)
    {
        SignalLog log;
        auto      toolbar = makeToolbarLogged(log);
        auto     *button  = buttonByLabel(*toolbar, "箭头样式");
        ASSERT_NE(button, nullptr) << "工具栏上找不到箭头样式按钮";

        EXPECT_EQ(toolbar->currentArrowHeadStyle(), 0) << "起点该是「实心」：这一格出现之前只有这一档";
        button->click();
        EXPECT_EQ(toolbar->currentArrowHeadStyle(), 1);
        EXPECT_EQ(log.arrowHeadStyles, std::vector<int>{1}) << "走了一档却没把信号发出去";
        EXPECT_EQ(button->toolTip(), QString::fromUtf8("箭头样式：空心")) << "提示没跟上当前档";

        button->click();
        EXPECT_EQ(toolbar->currentArrowHeadStyle(), 2);
        EXPECT_EQ(button->toolTip(), QString::fromUtf8("箭头样式：开口"));

        button->click();
        EXPECT_EQ(toolbar->currentArrowHeadStyle(), 0) << "三档之后该回到「实心」";
        EXPECT_EQ(button->accessibleName(), QString::fromUtf8("箭头样式：实心")) << "无障碍名没跟上当前档";
    }

    /**
     * @brief 钉住箭头大小按钮的三档循环：起点「中」=3.5（与旧写死值同解），信号与提示一起走
     */
    TEST_F(ToolbarFixture, ArrowHeadScaleButtonCyclesAndNamesTheStep)
    {
        SignalLog log;
        auto      toolbar = makeToolbarLogged(log);
        auto     *button  = buttonByLabel(*toolbar, "箭头大小");
        ASSERT_NE(button, nullptr) << "工具栏上找不到箭头大小按钮";

        EXPECT_EQ(toolbar->currentArrowHeadScale(), 3.5) << "起点该是「中」3.5：与这一格出现之前的写死值同解，老图样不受影响";
        button->click();
        EXPECT_EQ(toolbar->currentArrowHeadScale(), 5.0);
        EXPECT_EQ(log.arrowHeadScales, std::vector<double>{5.0}) << "走了一档却没把信号发出去";
        EXPECT_EQ(button->toolTip(), QString::fromUtf8("箭头大小：大")) << "提示没跟上当前档";

        button->click();
        EXPECT_EQ(toolbar->currentArrowHeadScale(), 2.5);
        EXPECT_EQ(button->toolTip(), QString::fromUtf8("箭头大小：小"));

        button->click();
        EXPECT_EQ(toolbar->currentArrowHeadScale(), 3.5) << "三档之后该回到「中」";
        EXPECT_EQ(button->accessibleName(), QString::fromUtf8("箭头大小：中")) << "无障碍名没跟上当前档";
    }

    /**
     * @brief 钉住文字样式三件：字体下拉换族发信号，粗体/斜体是保持按下态的开关
     * @details 字体族从本机字体库里现取一个与当前不同的：写死家族名在别的机器上可能落到同一个
     *          替代字体上，信号那格就验不出东西。
     */
    TEST_F(ToolbarFixture, TextStyleControlsDriveTheSignals)
    {
        SignalLog log;
        auto      toolbar = makeToolbarLogged(log);

        auto *fontCombo = toolbar->findChild<QFontComboBox *>();
        ASSERT_NE(fontCombo, nullptr) << "工具栏上找不到字体下拉";
        const auto families = QFontDatabase::families();
        ASSERT_GE(families.size(), 2) << "这台机器只有一个字体家族，换族无从验起";
        QString other;
        for (const auto &family: families)
        {
            if (family != fontCombo->currentFont().family())
            {
                other = family;
                break;
            }
        }
        ASSERT_FALSE(other.isEmpty());
        fontCombo->setCurrentFont(QFont{other});
        EXPECT_EQ(toolbar->currentFontFamily(), other);
        ASSERT_EQ(log.fontFamilies.size(), 1) << "换族没把信号发出去";
        EXPECT_EQ(log.fontFamilies.back(), other);

        auto *boldButton = buttonByLabel(*toolbar, "粗体");
        ASSERT_NE(boldButton, nullptr) << "工具栏上找不到粗体按钮";
        EXPECT_FALSE(toolbar->currentTextBold()) << "起点该是常规档";
        boldButton->click();
        EXPECT_TRUE(toolbar->currentTextBold());
        EXPECT_EQ(log.textBolds, std::vector<int>{1}) << "开了却没把信号发出去";
        EXPECT_TRUE(boldButton->isChecked()) << "开关开了按钮却没保持按下";
        EXPECT_EQ(boldButton->toolTip(), QString::fromUtf8("粗体：开")) << "提示没跟上开关";
        boldButton->click();
        EXPECT_FALSE(toolbar->currentTextBold());
        EXPECT_EQ(log.textBolds, (std::vector<int>{1, 0}));

        auto *italicButton = buttonByLabel(*toolbar, "斜体");
        ASSERT_NE(italicButton, nullptr) << "工具栏上找不到斜体按钮";
        EXPECT_FALSE(toolbar->currentTextItalic()) << "起点该是常规档";
        italicButton->click();
        EXPECT_TRUE(toolbar->currentTextItalic());
        EXPECT_EQ(log.textItalics, std::vector<int>{1}) << "开了却没把信号发出去";
        EXPECT_EQ(italicButton->toolTip(), QString::fromUtf8("斜体：开")) << "提示没跟上开关";
        italicButton->click();
        EXPECT_FALSE(toolbar->currentTextItalic());
        EXPECT_EQ(italicButton->accessibleName(), QString::fromUtf8("斜体：关")) << "无障碍名没跟上开关";
    }

    /**
     * @brief 钉住描边与背景两个开关：按下态、提示、无障碍名与信号四处同解
     */
    TEST_F(ToolbarFixture, TextEffectTogglesDriveTheSignals)
    {
        SignalLog log;
        auto      toolbar = makeToolbarLogged(log);

        auto *outlineButton = buttonByLabel(*toolbar, "描边");
        ASSERT_NE(outlineButton, nullptr) << "工具栏上找不到描边按钮";
        EXPECT_FALSE(toolbar->currentTextOutline()) << "起点该是关";
        outlineButton->click();
        EXPECT_TRUE(toolbar->currentTextOutline());
        EXPECT_EQ(log.textOutlines, std::vector<int>{1}) << "开了却没把信号发出去";
        EXPECT_EQ(outlineButton->toolTip(), QString::fromUtf8("描边：开")) << "提示没跟上开关";
        outlineButton->click();
        EXPECT_FALSE(toolbar->currentTextOutline());
        EXPECT_EQ(outlineButton->accessibleName(), QString::fromUtf8("描边：关")) << "无障碍名没跟上开关";

        auto *backgroundButton = buttonByLabel(*toolbar, "背景");
        ASSERT_NE(backgroundButton, nullptr) << "工具栏上找不到背景按钮";
        EXPECT_FALSE(toolbar->currentTextBackground()) << "起点该是关";
        backgroundButton->click();
        EXPECT_TRUE(toolbar->currentTextBackground());
        EXPECT_EQ(log.textBackgrounds, std::vector<int>{1}) << "开了却没把信号发出去";
        EXPECT_EQ(backgroundButton->toolTip(), QString::fromUtf8("背景：开")) << "提示没跟上开关";
        backgroundButton->click();
        EXPECT_FALSE(toolbar->currentTextBackground());
        EXPECT_EQ(log.textBackgrounds, (std::vector<int>{1, 0}));
    }

} // namespace

/**
 * @brief 钉住「下方有位置就放下方」
 * @details 四个数字全部手算：摆放差 8 像素就会盖住选区边缘，所以断言的是精确矩形，不是「大概在这附近」。
 */
TEST(ToolbarGeometry, SitsBelowSelectionWhenThereIsRoom)
{
    const QRect selection{100, 100, 200, 100};
    const auto  placed = placeToolbar(kScreen, QSize{300, 40}, selection);
    ASSERT_TRUE(placed.has_value()) << placed.error().message;
    // 选区 bottom = 100+100-1 = 199，加 1 再加间距 8 → 208；横向中心 200 减一半宽 150 → 50
    EXPECT_EQ(placed->frame, QRect(50, 208, 300, 40));
    EXPECT_EQ(placed->side, ToolbarSide::Below);
    EXPECT_GE(placed->frame.top(), selection.bottom() + 2) << "浮放的工具栏不许压住选区";
}

/**
 * @brief 钉住「下方放不下就翻上方」
 * @details 选区贴到屏幕下边时工具栏必须出现在上边，翻面之后仍然完整可见、不碰选区。
 */
TEST(ToolbarGeometry, FlipsAboveWhenBottomEdgeHasNoRoom)
{
    const auto selection = QRect{100, 1000, 200, 60};
    const auto placed    = placeToolbar(kScreen, QSize{300, 40}, selection);
    ASSERT_TRUE(placed.has_value()) << placed.error().message;
    // 下方要从 1068 起、高 40 → 1108 超出 1080；上方 = 1000-8-40 = 952
    EXPECT_EQ(placed->side, ToolbarSide::Above);
    EXPECT_EQ(placed->frame, QRect(50, 952, 300, 40));
    EXPECT_LE(placed->frame.bottom(), selection.top() - 1) << "翻到上方之后仍然压住了选区";
    EXPECT_TRUE(kScreen.contains(placed->frame)) << "摆放结果必须整块落在可用区里";
}

/**
 * @brief 钉住横向夹边与宽度收缩
 * @details 靠右边缘的选区会把居中的工具栏推出屏幕，要夹住；屏幕比工具栏还窄时宽度收到可用区宽，
 *          让界面层按宽度自己重排，而不是裁掉几个按钮。
 */
TEST(ToolbarGeometry, ClampsHorizontallyAndShrinksToNarrowScreen)
{
    const auto rightEdge = placeToolbar(kScreen, QSize{300, 40}, QRect{1750, 300, 160, 100});
    ASSERT_TRUE(rightEdge.has_value()) << rightEdge.error().message;
    EXPECT_EQ(rightEdge->frame.left(), 1620) << "屏幕右边只容到 1620，再往右就出界";
    EXPECT_LE(rightEdge->frame.right(), kScreen.right()) << "工具栏被推出屏幕";

    const QRect smallScreen{0, 0, 100, 600};
    const auto  narrow = placeToolbar(smallScreen, QSize{300, 40}, QRect{10, 100, 60, 40});
    ASSERT_TRUE(narrow.has_value()) << narrow.error().message;
    EXPECT_EQ(narrow->frame.width(), 100) << "宽度应收到可用区宽";
    EXPECT_EQ(narrow->frame.left(), 0);
    EXPECT_TRUE(smallScreen.contains(narrow->frame));
}

/**
 * @brief 钉住「两侧都放不下时贴更大的一边并夹回屏内」
 * @details 屏幕比「选区加两侧间距」还矮时两个方向都不成立；这时可以压住选区边缘，
 *          但绝不能缺一角——缺角的那几个按钮就点不到了。
 */
TEST(ToolbarGeometry, ClampsIntoScreenWhenNeitherSideFits)
{
    const QRect screen{0, 0, 800, 200};
    const auto  placed = placeToolbar(screen, QSize{300, 90}, QRect{100, 80, 200, 40});
    ASSERT_TRUE(placed.has_value()) << placed.error().message;
    // 下方 128 起、上方 -18，两边都放不下；两个方向的余量都是 72，取平手的下方
    EXPECT_EQ(placed->side, ToolbarSide::Below);
    EXPECT_EQ(placed->frame.top(), 110) << "纵坐标应被夹到可用区下边界内缩一个高度：199+1-90";
    EXPECT_EQ(placed->frame.height(), 90);
    EXPECT_TRUE(screen.contains(placed->frame)) << "夹完之后仍然要整块可见";
}

/**
 * @brief 钉住三个退化入参各自被拒
 * @details 尺寸、选区、可用区为 0 时后面的算术照样能跑，跑出来的是一个看不见的位置；
 *          这类输入必须当场报错而不是往下传，且两类故障的ErrorCode不同。
 */
TEST(ToolbarGeometry, RejectsDegenerateInputs)
{
    const auto badToolbar = placeToolbar(kScreen, QSize{0, 40}, QRect{10, 10, 20, 20});
    ASSERT_FALSE(badToolbar.has_value());
    EXPECT_EQ(badToolbar.error().code, ErrorCode::InvalidArgument);
    EXPECT_NE(badToolbar.error().message.find("sizeHint"), std::string::npos) << badToolbar.error().message;

    const auto badSelection = placeToolbar(kScreen, QSize{300, 40}, QRect{10, 10, 0, 0});
    ASSERT_FALSE(badSelection.has_value());
    EXPECT_EQ(badSelection.error().code, ErrorCode::InvalidArgument);

    const auto badScreen = placeToolbar(QRect{}, QSize{300, 40}, QRect{10, 10, 20, 20});
    ASSERT_FALSE(badScreen.has_value());
    EXPECT_EQ(badScreen.error().code, ErrorCode::ScreenUnavailable) << "屏幕没了不是参数不合法，调用方的处理完全不同";
}

/**
 * @brief 钉住工具栏的控件齐全且都带无障碍名称
 * @details 需求要那几件工具、八色加自定义、撤销重做与四个动作，并要每个图标按钮都有 tooltip
 *          与无障碍名称。这条按控件树逐个核，不看布局画得好不好。
 *          tooltip 比无障碍名多一段键位提示：读屏要的是工具本名，眼睛要的是「按哪个键也能切过去」。
 */
TEST_F(ToolbarFixture, ExposesEveryRequiredControlWithAccessibleNames)
{
    SignalLog log;
    auto      toolbar = makeToolbarLogged(log);

    ASSERT_EQ(AnnotationToolbar::toolButtonCount(), kToolLabels.size()) << "工具栏的按钮数要与这份独立写下的标签表同解";
    ASSERT_EQ(kToolLabels.size(), kToolShortcuts.size()) << "两份独立清单长度不同，交叉核对就没做了";
    ASSERT_EQ(toolbar->colorButtonCount(), static_cast<int>(kAnnotationPalette.size()) + 1) << "配置是空的：预设八格加自定义一枚";

    for (std::size_t index = 0; index < kToolLabels.size(); ++index)
    {
        auto *button = toolbar->toolButtonAt(index);
        ASSERT_NE(button, nullptr) << index;
        // 中文名与抄来的清单同解：图标画得出来但名字叫「按钮 3」，读屏的用户还是用不了
        EXPECT_EQ(button->accessibleName(), text(kToolLabels[index])) << index;
        EXPECT_EQ(button->toolTip(), text(kToolLabels[index]) + QStringLiteral(" (") + text(kToolShortcuts[index]) + QStringLiteral(")")) << index;
        EXPECT_TRUE(button->isCheckable()) << index;
        EXPECT_FALSE(button->icon().isNull()) << index << " 没取到图标，图标名拼错或资源没进 qrc";
    }

    // 控件树里不允许有任何没名字的交互控件：这条同时兜住以后加按钮忘了写 tooltip 的情况
    for (auto *button: toolbar->findChildren<QToolButton *>())
    {
        EXPECT_FALSE(button->accessibleName().isEmpty()) << "有个按钮没写无障碍名称";
        EXPECT_FALSE(button->toolTip().isEmpty()) << "有个按钮没写 tooltip";
    }
    for (auto *slider: toolbar->findChildren<QSlider *>())
    {
        EXPECT_FALSE(slider->accessibleName().isEmpty());
    }
    for (auto *combo: toolbar->findChildren<QComboBox *>())
    {
        EXPECT_FALSE(combo->accessibleName().isEmpty());
    }

    for (const auto label: kActionLabels)
    {
        EXPECT_NE(buttonByLabel(*toolbar, label), nullptr) << label;
    }
    EXPECT_NE(buttonByLabel(*toolbar, "撤销（Ctrl+Z）"), nullptr);
    EXPECT_NE(buttonByLabel(*toolbar, "重做（Ctrl+Y）"), nullptr);
}

/**
 * @brief 钉住色板里那格白色仍然画得出边界
 * @details 色块只有填充没有描边时，白色那一格在白底界面上是一片空，用户会当成禁用位；
 *          描边取主题的图标色，所以判据是「图标色真的出现在这一格的位图里」。
 */
TEST_F(ToolbarFixture, WhiteSwatchKeepsAVisibleOutline)
{
    SignalLog log;
    auto      toolbar = makeToolbarLogged(log);

    auto *whiteSwatch = buttonByLabel(*toolbar, "颜色 8");
    ASSERT_NE(whiteSwatch, nullptr);
    const auto bitmap = whiteSwatch->icon().pixmap(QSize{20, 20}).toImage().convertToFormat(QImage::Format_ARGB32);
    ASSERT_EQ(bitmap.width(), 20);

    const auto outline = icons->tintColor();
    const auto fill    = kAnnotationPalette[kAnnotationPalette.size() - 1];
    int        outlinePixels{};
    int        fillPixels{};
    for (int y = 0; y < bitmap.height(); ++y)
    {
        const auto *line = reinterpret_cast<const QRgb *>(bitmap.constScanLine(y));
        for (int x = 0; x < bitmap.width(); ++x)
        {
            if (qAlpha(line[x]) == 0)
            {
                continue;
            }
            const auto differenceTo = [pixel = QColor{line[x]}](const QColor &color)
            { return std::abs(pixel.red() - color.red()) + std::abs(pixel.green() - color.green()) + std::abs(pixel.blue() - color.blue()); };
            // 抗锯齿让边缘是混色，判「接近」而不是判相等
            if (differenceTo(outline) <= 8)
            {
                ++outlinePixels;
            }
            if (differenceTo(fill) <= 8)
            {
                ++fillPixels;
            }
        }
    }
    EXPECT_GT(outlinePixels, 0) << "白底上一格没有描边的白色色块等于看不见，用户会以为那一格是禁用的";
    EXPECT_GT(fillPixels, 0) << "色块没填上色板里的颜色";
}

/**
 * @brief 钉住初值落在定义过的阶梯上
 * @details 默认工具矩形、默认色是色板第一格、线宽 4、字号 14；这四值一漂，界面就会出现需求没定义的
 *          3.5 线宽或 15 号字。撤销/重做初值必须是禁用的。
 */
TEST_F(ToolbarFixture, StartsOnDefinedLadderValues)
{
    SignalLog log;
    auto      toolbar = makeToolbarLogged(log);

    EXPECT_EQ(toolbar->currentColor(), kAnnotationPalette[0]);
    EXPECT_EQ(toolbar->currentWidth(), 4) << "默认线宽应是阶梯里的那一档";
    EXPECT_EQ(toolbar->currentFontSize(), 14);
    EXPECT_TRUE(toolbar->toolButtonAt(0)->isChecked()) << "默认工具应是矩形";
    EXPECT_FALSE(buttonByLabel(*toolbar, "撤销（Ctrl+Z）")->isEnabled()) << "刚建好没有历史，亮着的撤销点了没反应";
    EXPECT_FALSE(buttonByLabel(*toolbar, "重做（Ctrl+Y）")->isEnabled());

    // 字号下拉的取值必须恰好是那六个数，一个不多一个少。字体下拉是后来加进来的同类控件
    // （QFontComboBox 继承 QComboBox）：按无障碍名挑出字号那一个再数，别把系统字体名也算进去
    QComboBox *sizeCombo = nullptr;
    for (auto *combo: toolbar->findChildren<QComboBox *>())
    {
        if (combo->accessibleName() == QString::fromUtf8("字号"))
        {
            sizeCombo = combo;
            break;
        }
    }
    ASSERT_NE(sizeCombo, nullptr) << "工具栏上找不到字号下拉";
    std::vector<int> listedSizes;
    for (int index = 0; index < sizeCombo->count(); ++index)
    {
        listedSizes.push_back(sizeCombo->itemData(index).toInt());
    }
    EXPECT_EQ(listedSizes, std::vector<int>(kFontSizes.begin(), kFontSizes.end())) << "字号下拉列出了需求之外的档位";

    auto *fontCombo = toolbar->findChild<QFontComboBox *>();
    ASSERT_NE(fontCombo, nullptr) << "工具栏上找不到字体下拉";
    EXPECT_GT(fontCombo->count(), 0) << "字体下拉一个家族都没列出来";
}

/**
 * @brief 钉住点击工具按钮发信号、保持互斥、不重复发
 * @details 「点第 4 个按钮收到 Pen」这一条抓住登记表顺序与 ToolKind 顺序错位；
 *          重复点同一个工具不再二次发信号，否则宿主每次点击都要问一遍引擎并刷一轮按钮。
 */
TEST_F(ToolbarFixture, ClickingToolEmitsOnceAndKeepsExclusive)
{
    SignalLog log;
    auto      toolbar = makeToolbarLogged(log);

    toolbar->toolButtonAt(3)->click();
    ASSERT_EQ(log.tools.size(), 1u);
    EXPECT_EQ(log.tools[0], ToolKind::Pen);
    EXPECT_TRUE(toolbar->toolButtonAt(3)->isChecked());
    EXPECT_FALSE(toolbar->toolButtonAt(0)->isChecked()) << "两个工具同时亮着，用户看不出自己在用哪个";

    toolbar->toolButtonAt(3)->click();
    EXPECT_EQ(log.tools.size(), 1u) << "重复点已经在用的工具又发了一次信号";
}

/**
 * @brief 钉住宿主回灌工具态时不回发信号
 * @details setActiveTool 是「引擎→界面」的方向，若它再把 toolChanged 发出去就成回环；
 *          这条专门抓那种两边互相触发、界面卡死的现场。回灌也算「当前工具变了」，
 *          所以回灌之后再点同一个按钮同样不该重发。
 */
TEST_F(ToolbarFixture, SetActiveToolSyncsButtonsWithoutEchoing)
{
    SignalLog log;
    auto      toolbar = makeToolbarLogged(log);

    toolbar->setActiveTool(ToolKind::Mosaic);
    EXPECT_TRUE(toolbar->toolButtonAt(5)->isChecked());
    EXPECT_FALSE(toolbar->toolButtonAt(0)->isChecked());
    EXPECT_TRUE(log.tools.empty()) << "回灌工具态又发了一次 toolChanged，宿主会跟着再切一遍引擎";

    // 回灌过的工具已经是当前工具，再点它不该发第二次；点另一个才发
    toolbar->toolButtonAt(5)->click();
    EXPECT_TRUE(log.tools.empty()) << "当前工具已经是它，重复点不该再通知宿主";
    toolbar->toolButtonAt(6)->click();
    ASSERT_EQ(log.tools.size(), 1u);
    EXPECT_EQ(log.tools[0], ToolKind::Text);

    // 不存在的工具序号不该把按钮状态带跑
    toolbar->setActiveTool(static_cast<ToolKind>(99));
    EXPECT_TRUE(toolbar->toolButtonAt(6)->isChecked());
}

/**
 * @brief 钉住滑块档位与线宽表同解
 * @details 滑块只走 0..3，每一步映射到 2/4/6/8；映射错位会让界面显示的宽度和实际画的宽度不一致。
 */
TEST_F(ToolbarFixture, SliderStepsMapToLadderWidths)
{
    SignalLog log;
    auto      toolbar = makeToolbarLogged(log);

    auto *slider = toolbar->findChild<QSlider *>();
    ASSERT_NE(slider, nullptr);
    EXPECT_EQ(slider->maximum(), static_cast<int>(kAnnotationWidths.size()) - 1);

    for (int step = 0; step <= slider->maximum(); ++step)
    {
        slider->setValue(step);
    }
    EXPECT_EQ(log.widths, std::vector<int>(kAnnotationWidths.begin(), kAnnotationWidths.end())) << "滑块档位与线宽表不同解";
    EXPECT_EQ(toolbar->currentWidth(), kAnnotationWidths[kAnnotationWidths.size() - 1]);
}

/**
 * @brief 钉住色块与动作按钮各归各位
 * @details 点色块发 colorChanged 且只亮一个；点四个动作各发一次对应信号，
 *          混在一起的话「复制」就会去存文件。
 */
TEST_F(ToolbarFixture, ColorSwatchesAndActionButtonsEmitTheirOwnSignals)
{
    SignalLog log;
    auto      toolbar = makeToolbarLogged(log);

    // 第二格色块按名称取：findChildren 的顺序不保证等于创建顺序，记序号会点错格
    auto *secondSwatch = buttonByLabel(*toolbar, "颜色 2");
    ASSERT_NE(secondSwatch, nullptr);
    secondSwatch->click();
    ASSERT_EQ(log.colors.size(), 1u);
    EXPECT_EQ(log.colors[0], kAnnotationPalette[1]) << "色块顺序与色板顺序不同解";
    EXPECT_EQ(toolbar->currentColor(), kAnnotationPalette[1]);

    for (const auto label: kActionLabels)
    {
        auto *button = buttonByLabel(*toolbar, label);
        ASSERT_NE(button, nullptr) << label;
        button->click();
    }
    EXPECT_EQ(log.actions, 4) << "四个动作按钮的信号没各自分开落地";
    EXPECT_EQ(log.undo, 0);
    EXPECT_EQ(log.redo, 0);

    toolbar->setUndoRedoAvailable(true, false);
    EXPECT_TRUE(buttonByLabel(*toolbar, "撤销（Ctrl+Z）")->isEnabled());
    EXPECT_FALSE(buttonByLabel(*toolbar, "重做（Ctrl+Y）")->isEnabled());
    buttonByLabel(*toolbar, "撤销（Ctrl+Z）")->click();
    EXPECT_EQ(log.undo, 1);
}

/**
 * @brief 钉住「最近使用颜色」从配置回到色板，点了就发那条颜色
 * @details 清单本身的算法在 TestRecentColors 里钉；这条端到端验的是接线——
 *          存好配置后重开一份 store，等价于用户重启程序再回来。
 *          「自定义取色之后写入配置」那一半没在这里验：QColorDialog 是无头的，
 *          点不到它（界面的硬限制），算法侧的序列化与读回已经成对钉住。
 */
TEST_F(ToolbarFixture, RecentColorsFromSettingsBecomeSwatches)
{
    ASSERT_TRUE(m_settings->setValue(GyanisScribe::Settings::kAnnotationRecentColors, QStringList{QStringLiteral("#FF0000"), QStringLiteral("#0078FF")}).has_value());
    m_settings->sync();
    m_settings = std::make_unique<SettingsStore>(m_iniPath);

    SignalLog log;
    auto      toolbar = makeToolbarLogged(log);

    ASSERT_EQ(toolbar->colorButtonCount(), static_cast<int>(kAnnotationPalette.size()) + 3) << "预设八格 + 最近两格 + 自定义一枚";

    auto *second = buttonByLabel(*toolbar, "最近使用 2");
    ASSERT_NE(second, nullptr) << "第二格没按清单排出来，顺序就不再是「最近」";
    second->click();
    ASSERT_EQ(log.colors.size(), 1U);
    EXPECT_EQ(log.colors[0], QColor(0, 0x78, 0xFF)) << "按下的是第二格，发出来的却是别的颜色";
}

/**
 * @brief 钉住「默认线宽与字号」读写两头都接在配置上
 * @details 只测读端的话，控件调档没写回配置也能绿；这里调完再重开一份 store，
 *          让「下一次启动是不是从这里起」这一格真的被断到。
 */
TEST_F(ToolbarFixture, AdjustedWidthAndFontSizeAreRememberedAcrossRestart)
{
    {
        SignalLog log;
        auto      toolbar = makeToolbarLogged(log);

        auto *slider = toolbar->findChild<QSlider *>();
        ASSERT_NE(slider, nullptr);
        slider->setValue(3); // 阶梯最后一档 = 8

        auto *combo = toolbar->findChild<QComboBox *>();
        ASSERT_NE(combo, nullptr);
        combo->setCurrentIndex(5); // 字号阶梯最后一档 = 24
        m_settings->sync();
    }

    m_settings = std::make_unique<SettingsStore>(m_iniPath);
    EXPECT_EQ(m_settings->intValue(GyanisScribe::Settings::kAnnotationDefaultWidth, -1), 8) << "调过线宽没写进配置";
    EXPECT_EQ(m_settings->intValue(GyanisScribe::Settings::kAnnotationDefaultFontSize, -1), 24) << "调过字号没写进配置";

    SignalLog log;
    auto      second = makeToolbarLogged(log);
    EXPECT_EQ(second->currentWidth(), 8) << "配置存着 8，起点却不是 8";
    EXPECT_EQ(second->currentFontSize(), 24);
}

/**
 * @brief 钉住阶梯外的存量值回默认档，且不被悄悄改掉
 * @details 就近取一档会让用户以为存的 5 生效了；把 5 改写成 4 更糟——配置文件里那条
 *          本来是别处（手改或旧版本）留下的，静默改掉等于抹掉现场。
 */
TEST_F(ToolbarFixture, OffLadderStoredDefaultsFallBackWithoutRewriting)
{
    ASSERT_TRUE(m_settings->setValue(GyanisScribe::Settings::kAnnotationDefaultWidth, 5).has_value());
    ASSERT_TRUE(m_settings->setValue(GyanisScribe::Settings::kAnnotationDefaultFontSize, 15).has_value());
    m_settings->sync();

    SignalLog log;
    auto      toolbar = makeToolbarLogged(log);
    EXPECT_EQ(toolbar->currentWidth(), 4) << "阶梯外的 5 应当回默认档 4";
    EXPECT_EQ(toolbar->currentFontSize(), 14) << "字号同理";

    m_settings->sync();
    EXPECT_EQ(m_settings->intValue(GyanisScribe::Settings::kAnnotationDefaultWidth, -1), 5) << "构造期把坏值改写了";
    EXPECT_EQ(m_settings->intValue(GyanisScribe::Settings::kAnnotationDefaultFontSize, -1), 15);
}
