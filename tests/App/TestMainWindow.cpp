// 主窗口的接线判据：导航行—页内容—配置键三者要互指。
// 逐格核对的写法来自一个具体的坑：设置页的每一行都同时有「界面里的控件」和「另一头的读端」，
// 只测其中一头时，另一头改成别的键也照样全绿。
#include "App/MainWindow.h"

#include "Annotation/AnnotationToolbar.h"
#include "History/HistoryStore.h"
#include "Pin/PinLayout.h"
#include "Record/RecorderConfig.h"
#include "Settings/SettingsKeys.h"
#include "Settings/SettingsStore.h"
#include "Ui/DesignTokens.h"
#include "Ui/IconProvider.h"
#include "Ui/Language.h"
#include "Ui/ThemeManager.h"

#include <gtest/gtest.h>

#include <QApplication>
#include <QComboBox>
#include <QIcon>
#include <QLabel>
#include <QListWidget>
#include <QSpinBox>
#include <QStackedWidget>
#include <QString>
#include <QWidget>

#include <filesystem>
#include <format>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace
{
    using GyanisScribe::Annotation::AnnotationToolbar;
    using GyanisScribe::App::MainWindow;
    using GyanisScribe::App::MainWindowPage;
    using GyanisScribe::App::pageChoices;
    using GyanisScribe::History::HistoryStore;
    using GyanisScribe::Settings::SettingsStore;
    using GyanisScribe::Ui::AppLanguage;
    using GyanisScribe::Ui::IconProvider;
    using GyanisScribe::Ui::ThemeManager;

    std::unique_ptr<QApplication> application;

    // argv 的缓冲要活在 QApplication 的整个生命周期里：Qt 只留指针不留副本，
    // 放在函数栈上的话之后任何一次 applicationDirPath() 都会读到已释放的内存
    inline static int   kArgumentCount  = 1;
    inline static char  kArgumentZero[] = "TestMainWindow";
    inline static char *kArguments[]    = {kArgumentZero, nullptr};

    QString toQString(const std::string_view text)
    {
        return QString::fromUtf8(text.data(), static_cast<qsizetype>(text.size()));
    }

    /// 用例名给的独立 INI：ctest 逐用例起进程，但同一台机器上并行跑时共写一份配置会互相盖掉
    class MainWindowFixture : public ::testing::Test
    {
    protected:
        static void SetUpTestSuite()
        {
            application = std::make_unique<QApplication>(kArgumentCount, kArguments);
        }

        static void TearDownTestSuite()
        {
            application.reset();
        }

        void SetUp() override
        {
            const auto *info = ::testing::UnitTest::GetInstance()->current_test_info();
            m_directory      = std::filesystem::temp_directory_path() / std::format("GyanisScribeMainWindow.{}_{}", info->test_suite_name(), info->name());
            std::filesystem::remove_all(m_directory);
            std::filesystem::create_directories(m_directory);

            m_icons        = std::make_unique<IconProvider>();
            m_historyStore = std::make_unique<HistoryStore>(m_directory / "history.json");
            m_store        = makeStore();
            m_theme        = std::make_unique<ThemeManager>(*m_store);
        }

        void TearDown() override
        {
            // 窗口先于存储析构：它只借引用，反过来会让 QSettings 在窗口还活着时被销毁
            m_window.reset();
            m_theme.reset();
            m_store.reset();
            m_historyStore.reset();
            m_icons.reset();
            std::filesystem::remove_all(m_directory);
        }

        [[nodiscard]] std::unique_ptr<SettingsStore> makeStore() const
        {
            return std::make_unique<SettingsStore>(m_directory / "settings.ini");
        }

        /// 建窗口：WA_DontShowOnScreen 让 open() 里的 show() 不再真的弹一块窗口到桌面上
        void makeWindow()
        {
            m_window = std::make_unique<MainWindow>(*m_store, *m_theme, *m_icons, *m_historyStore);
            m_window->setAttribute(Qt::WA_DontShowOnScreen);
        }

        /// 关掉窗口并把配置落盘，随后用例换一个 store 实例去读——「读到的是盘上那份」才算写通
        void closeWindowAndFlush()
        {
            m_window.reset();
            m_theme.reset();
            m_store->sync();
            m_store.reset();
        }

        [[nodiscard]] QListWidget *nav() const
        {
            return m_window->findChild<QListWidget *>();
        }

        [[nodiscard]] QStackedWidget *stack() const
        {
            return m_window->findChild<QStackedWidget *>();
        }

        /// 当前那一页里指定无障碍名的控件；找不到就是空指针，调用方据此报「这一页没有它」
        [[nodiscard]] QWidget *controlInCurrentPage(const std::string_view name) const
        {
            auto *page = stack()->currentWidget();
            for (auto *child: page->findChildren<QWidget *>())
            {
                if (child->accessibleName() == toQString(name))
                {
                    return child;
                }
            }
            return nullptr;
        }

        /// 当前那一页的说明行文字；设置页没有它就不算「能自己上手」
        [[nodiscard]] QString introInCurrentPage() const
        {
            auto *page = stack()->currentWidget();
            for (auto *label: page->findChildren<QLabel *>())
            {
                if (label->accessibleName() == QStringLiteral("这一类的说明"))
                {
                    return label->text();
                }
            }
            return {};
        }

        std::filesystem::path          m_directory;
        std::unique_ptr<IconProvider>  m_icons;
        std::unique_ptr<HistoryStore>  m_historyStore;
        std::unique_ptr<SettingsStore> m_store;
        std::unique_ptr<ThemeManager>  m_theme;
        std::unique_ptr<MainWindow>    m_window;
    };

    /**
     * @brief 钉住「导航行、页容器、页内容」三者同序且每页都不是空的
     * @details 只数行数的话，把某一页的搭法接错分支照样全绿——导航与堆叠页都还是十行。
     *          所以每一页都要跳到它然后按无障碍名点一个真控件：错位、漏搭、页面空白三种坏法各自会红。
     */
    TEST_F(MainWindowFixture, NavRowsPagesAndContentsStayInOneOrder)
    {
        makeWindow();
        auto *navRows = nav();
        auto *pages   = stack();
        ASSERT_NE(navRows, nullptr);
        ASSERT_NE(pages, nullptr);

        const auto table = pageChoices();
        ASSERT_EQ(navRows->count(), static_cast<int>(table.size())) << "导航行数与表里的页数不同解";
        EXPECT_EQ(pages->count(), navRows->count()) << "页容器与导航行数不同解：跳到某一页看到的会是另一页";

        for (std::size_t index = 0; index < table.size(); ++index)
        {
            EXPECT_EQ(navRows->item(static_cast<int>(index))->text(), toQString(table[index].label)) << index;
            EXPECT_FALSE(navRows->item(static_cast<int>(index))->icon().isNull()) << "导航行点名的图标画不出来：" << std::string{table[index].icon};
        }

        struct Expectation
        {
            MainWindowPage                page;
            std::vector<std::string_view> controlNames; ///< 这一页必须点得出的控件
            bool                          hasIntro;     ///< 设置页要有说明行
        };

        // 「这一类有什么」的用词取自界面里的无障碍名，不是从配置键推的：读的人要能对着窗口核对
        const std::vector<Expectation> expectations = {
                Expectation{MainWindowPage::General, {"界面语言"}, true},
                Expectation{MainWindowPage::Capture, {"截图目录", "颜色写法"}, true},
                Expectation{MainWindowPage::Annotation, {"默认线宽", "默认字号"}, true},
                Expectation{MainWindowPage::Pin, {"默认不透明度"}, true},
                Expectation{MainWindowPage::Record, {"录屏目录", "默认帧率"}, true},
                Expectation{MainWindowPage::Ocr, {"识别语言", "语言包目录"}, true},
                Expectation{MainWindowPage::Hotkey, {"截图", "录屏", "文字识别", "屏幕取色"}, true},
                Expectation{MainWindowPage::Theme, {"主题"}, true},
                Expectation{MainWindowPage::About, {"智录图标"}, false},
                Expectation{MainWindowPage::History, {"历史列表"}, false},
        };
        ASSERT_EQ(expectations.size(), table.size()) << "新增一页要在这里也登记它该有什么";

        for (std::size_t index = 0; index < expectations.size(); ++index)
        {
            m_window->open(expectations[index].page);
            ASSERT_EQ(pages->currentIndex(), static_cast<int>(index)) << "跳页后页容器停在别处，序号 " << index;
            ASSERT_EQ(navRows->currentRow(), static_cast<int>(index)) << index;

            if (expectations[index].hasIntro)
            {
                EXPECT_FALSE(introInCurrentPage().isEmpty()) << "这一页没有说明行：" << index;
            }
            for (const auto controlName: expectations[index].controlNames)
            {
                EXPECT_NE(controlInCurrentPage(controlName), nullptr) << "第 " << index << " 页里没有「" << std::string{controlName} << "」";
            }
        }
    }

    /**
     * @brief 钉住「不透明度数字框的范围」与读端判据同解，并把写进去的值读回来
     * @details 数字框能给出 0 而读端拒 0，用户就会存下一个自己用不了的值；
     *          反过来写通读通才算这一行不是空控件。
     */
    TEST_F(MainWindowFixture, PinOpacityRowWritesTheKeyPinWindowReads)
    {
        makeWindow();
        m_window->open(MainWindowPage::Pin);
        auto *spin = qobject_cast<QSpinBox *>(controlInCurrentPage("默认不透明度"));
        ASSERT_NE(spin, nullptr);
        EXPECT_EQ(spin->value(), GyanisScribe::Pin::kDefaultOpacityPercent) << "没有存过时要显示完全不透明";
        EXPECT_EQ(spin->minimum(), 1) << "下限要与 parseOpacityPercentSetting 的拒绝面同解";
        EXPECT_EQ(spin->maximum(), 100);

        spin->setValue(40);
        closeWindowAndFlush();

        const auto reopened = makeStore();
        const auto stored   = reopened->intValue(GyanisScribe::Settings::kPinDefaultOpacity, -1);
        EXPECT_EQ(stored, 40);
        const auto parsed = GyanisScribe::Pin::parseOpacityPercentSetting(stored);
        ASSERT_TRUE(static_cast<bool>(parsed));
        EXPECT_DOUBLE_EQ(*parsed, 0.4);
    }

    /**
     * @brief 钉住默认帧率这一行写进 Record/DefaultFrameRate，且范围与读端同解
     */
    TEST_F(MainWindowFixture, FrameRateRowWritesTheKeyRecordingStartUses)
    {
        makeWindow();
        m_window->open(MainWindowPage::Record);
        auto *spin = qobject_cast<QSpinBox *>(controlInCurrentPage("默认帧率"));
        ASSERT_NE(spin, nullptr);
        EXPECT_EQ(spin->value(), GyanisScribe::Record::kDefaultFrameRate);
        EXPECT_EQ(spin->minimum(), 1);
        EXPECT_EQ(spin->maximum(), GyanisScribe::Record::kMaximumFrameRate) << "上限要与读端拒收的那一格同解，否则界面能给出运行期不收的帧率";

        spin->setValue(30);
        closeWindowAndFlush();

        const auto reopened = makeStore();
        const auto stored   = reopened->intValue(GyanisScribe::Settings::kRecordDefaultFrameRate, -1);
        EXPECT_EQ(stored, 30);
        const auto parsed = GyanisScribe::Record::parseFrameRateSetting(stored);
        ASSERT_TRUE(static_cast<bool>(parsed));
        EXPECT_EQ(*parsed, 30);
    }

    /**
     * @brief 钉住标注默认线宽/字号这一行与工具栏的起点互指
     * @details 设置页写的键由工具栏在构造时读：两边对不上就是「改了设置、画出来还是老样子」。
     */
    TEST_F(MainWindowFixture, AnnotationStepRowsWriteWhatTheToolbarStartsFrom)
    {
        makeWindow();
        m_window->open(MainWindowPage::Annotation);
        auto *width = qobject_cast<QComboBox *>(controlInCurrentPage("默认线宽"));
        auto *font  = qobject_cast<QComboBox *>(controlInCurrentPage("默认字号"));
        ASSERT_NE(width, nullptr);
        ASSERT_NE(font, nullptr);

        // 序号 2 与 4 按阶梯表算出来：6 像素与 20 号，界面上选的就是这两个值
        width->setCurrentIndex(2);
        font->setCurrentIndex(4);
        closeWindowAndFlush();

        const auto reopened = makeStore();
        EXPECT_EQ(reopened->intValue(GyanisScribe::Settings::kAnnotationDefaultWidth, -1), 6);
        EXPECT_EQ(reopened->intValue(GyanisScribe::Settings::kAnnotationDefaultFontSize, -1), 20);

        AnnotationToolbar toolbar{*reopened, *m_icons};
        EXPECT_EQ(toolbar.currentWidth(), 6);
        EXPECT_EQ(toolbar.currentFontSize(), 20);
    }

    /**
     * @brief 钉住「没存过时两处显示同一档」
     * @details 设置页与工具栏各有自己的默认档：不一致时用户在设置页看到 2，实际画出来是 4。
     */
    TEST_F(MainWindowFixture, AnnotationDefaultsAgreeWithToolbarWhenNothingIsStored)
    {
        makeWindow();
        m_window->open(MainWindowPage::Annotation);
        const auto *width = qobject_cast<QComboBox *>(controlInCurrentPage("默认线宽"));
        const auto *font  = qobject_cast<QComboBox *>(controlInCurrentPage("默认字号"));
        ASSERT_NE(width, nullptr);
        ASSERT_NE(font, nullptr);

        AnnotationToolbar toolbar{*m_store, *m_icons};
        EXPECT_EQ(width->currentData().toInt(), toolbar.currentWidth());
        EXPECT_EQ(font->currentData().toInt(), toolbar.currentFontSize());
        EXPECT_EQ(toolbar.currentWidth(), GyanisScribe::Ui::kAnnotationWidths[GyanisScribe::Ui::kDefaultAnnotationWidthStep]);
    }

    /**
     * @brief 钉住「阶梯里没有的坏值」在两个入口都回默认档
     * @details 就近取一档会让用户以为存的那档生效了；两处若一处取整、一处回默认，界面上就看不到矛盾，
     *          而画出来的是第三档。
     */
    TEST_F(MainWindowFixture, OffLadderAnnotationWidthFallsBackInBothEntries)
    {
        // 坏值直接写进夹具那份存储：同一个文件同时开两份 QSettings 时，读到的是缓存还是盘上的那份并不可靠
        ASSERT_TRUE(static_cast<bool>(m_store->setValue(GyanisScribe::Settings::kAnnotationDefaultWidth, 5)));

        makeWindow();
        m_window->open(MainWindowPage::Annotation);
        const auto *width = qobject_cast<QComboBox *>(controlInCurrentPage("默认线宽"));
        ASSERT_NE(width, nullptr);
        EXPECT_EQ(width->currentData().toInt(), GyanisScribe::Ui::kAnnotationWidths[GyanisScribe::Ui::kDefaultAnnotationWidthStep]);

        AnnotationToolbar toolbar{*m_store, *m_icons};
        EXPECT_EQ(toolbar.currentWidth(), width->currentData().toInt()) << "设置页与工具栏对同一个坏值给出了两档";
        // 坏值要留在配置里让人看得见：界面回落不该顺手把文件改掉
        EXPECT_EQ(m_store->intValue(GyanisScribe::Settings::kAnnotationDefaultWidth, -1), 5);
    }

    /**
     * @brief 钉住界面语言这一行写的是 Ui/Language 词表里的 token
     * @details 存中文标签的话下次启动读不出来（parseAppLanguage 认 zh/en），
     *          语言切换就成了「看着生效、重启回到原样」。
     */
    TEST_F(MainWindowFixture, LanguageRowWritesTheTokenStartupParses)
    {
        makeWindow();
        m_window->open(MainWindowPage::General);
        auto *combo = qobject_cast<QComboBox *>(controlInCurrentPage("界面语言"));
        ASSERT_NE(combo, nullptr);
        combo->setCurrentIndex(1);
        closeWindowAndFlush();

        const auto reopened = makeStore();
        const auto stored   = reopened->stringValue(GyanisScribe::Settings::kUiLanguage, QString{});
        EXPECT_EQ(stored, QStringLiteral("en"));
        const auto parsed = GyanisScribe::Ui::parseAppLanguage(stored.toStdString());
        ASSERT_TRUE(static_cast<bool>(parsed));
        EXPECT_EQ(*parsed, AppLanguage::English);
    }

} // namespace
