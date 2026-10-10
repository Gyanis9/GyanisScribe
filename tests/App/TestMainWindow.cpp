// 主窗口的接线判据：导航行—页内容—配置键三者要互指。
// 逐格核对的写法来自一个具体的坑：设置页的每一行都同时有「界面里的控件」和「另一头的读端」，
// 只测其中一头时，另一头改成别的键也照样全绿。
#include "App/MainWindow.h"

#include "Annotation/AnnotationToolbar.h"
#include "History/HistoryRules.h"
#include "History/HistoryStore.h"
#include "Pin/PinLayout.h"
#include "Record/RecorderConfig.h"
#include "Settings/ISettingsStore.h"
#include "Settings/SettingsKeys.h"
#include "Settings/SettingsStore.h"
#include "Ui/DesignTokens.h"
#include "Ui/IconProvider.h"
#include "Ui/Language.h"
#include "Ui/ThemeManager.h"

#include <gtest/gtest.h>

#include <QApplication>
#include <QCheckBox>
#include <QFile>
#include <QFileInfo>
#include <QComboBox>
#include <QIcon>
#include <QLabel>
#include <QListWidget>
#include <QSpinBox>
#include <QStackedWidget>
#include <QString>
#include <QStringList>
#include <QWidget>

#include <filesystem>
#include <format>
#include <memory>
#include <optional>
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
                Expectation{MainWindowPage::General, {"界面语言", "历史最多留多少条", "历史最久留多少天", "导出配置", "导入配置"}, true},
                Expectation{MainWindowPage::Capture, {"截图目录", "颜色写法"}, true},
                Expectation{MainWindowPage::Annotation, {"默认线宽", "默认字号"}, true},
                Expectation{MainWindowPage::Pin, {"默认不透明度"}, true},
                Expectation{MainWindowPage::Record, {"录屏目录", "默认帧率", "光标高亮"}, true},
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
     * @brief 钉住光标高亮开关写进 Record/CursorHighlight，且默认档与读端同解
     * @details 开关默认必须一致：界面上显示「关」而 RecorderConfig 初值是「开」时，
     *          用户会在没点过任何一格的情况下得到一张被圈过的画面。
     */
    TEST_F(MainWindowFixture, CursorHighlightRowWritesTheSwitchRecordingStartUses)
    {
        makeWindow();
        m_window->open(MainWindowPage::Record);
        auto *check = qobject_cast<QCheckBox *>(controlInCurrentPage("光标高亮"));
        ASSERT_NE(check, nullptr);
        EXPECT_FALSE(check->isChecked()) << "默认要关着：圈住光标会改画面，用户没要求时不该动它";
        EXPECT_FALSE(GyanisScribe::Record::RecorderConfig{}.cursorHighlight) << "读端初值与界面默认档不同解";

        check->setChecked(true);
        closeWindowAndFlush();

        const auto reopened = makeStore();
        EXPECT_TRUE(reopened->boolValue(GyanisScribe::Settings::kRecordCursorHighlight, false));
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

    /**
     * @brief 钉住历史保留策略的两格写进策略读的那两个键，并且 0 这一档在两处都叫「不限」
     * @details 需求第「SettingsDialog」一节点名要能改历史保留策略，而 History::policyFromSettings 那条
     *          负数提示写着「请在设置的通用页改回来」——这一页没这两格时，那句提示指向一个不存在的地方。
     *          0 的读法也要两处同解：界面上摆个「0」而读端当它是「不限」，用户会以为历史一条都留不住。
     */
    TEST_F(MainWindowFixture, HistoryRetentionRowsWriteWhatPolicyReads)
    {
        makeWindow();
        m_window->open(MainWindowPage::General);
        auto *entries = qobject_cast<QSpinBox *>(controlInCurrentPage("历史最多留多少条"));
        auto *days    = qobject_cast<QSpinBox *>(controlInCurrentPage("历史最久留多少天"));
        ASSERT_NE(entries, nullptr);
        ASSERT_NE(days, nullptr);

        EXPECT_EQ(entries->value(), GyanisScribe::History::kDefaultMaximumEntries) << "界面默认档要与策略默认档同解";
        EXPECT_EQ(days->value(), GyanisScribe::History::kDefaultRetentionDays);
        EXPECT_EQ(entries->specialValueText(), QStringLiteral("不限"));
        EXPECT_EQ(days->specialValueText(), QStringLiteral("不限"));
        // 「不限」只在数字框的下限那一格显示：下限不是 0 时用户填不到它，读端那条「0 表示不限」就成空话
        EXPECT_EQ(entries->minimum(), 0);
        EXPECT_EQ(days->minimum(), 0);

        entries->setValue(50);
        days->setValue(0);
        closeWindowAndFlush();

        const auto reopened = makeStore();
        EXPECT_EQ(reopened->intValue(GyanisScribe::Settings::kHistoryMaximumEntries, -1), 50);
        EXPECT_EQ(reopened->intValue(GyanisScribe::Settings::kHistoryRetentionDays, -1), 0);
        const auto policy = GyanisScribe::History::policyFromSettings(*reopened);
        EXPECT_EQ(policy.maximumEntries, 50);
        EXPECT_EQ(policy.retentionDays, 0) << "0 要按「不限」读，而不是「一条都不留」";
    }

    /**
     * @brief 钉住「配置里存的负数」在界面与策略两边给出同一个读数
     * @details 策略把负数按「不限」处理并提示去通用页改；界面若把这格摆成别的读数，
     *          那句提示就永远对不上用户看到的东西。
     */
    TEST_F(MainWindowFixture, NegativeStoredLimitShowsAsUnlimitedOnBothSides)
    {
        ASSERT_TRUE(static_cast<bool>(m_store->setValue(GyanisScribe::Settings::kHistoryMaximumEntries, -1)));

        makeWindow();
        m_window->open(MainWindowPage::General);
        const auto *entries = qobject_cast<QSpinBox *>(controlInCurrentPage("历史最多留多少条"));
        ASSERT_NE(entries, nullptr);
        EXPECT_EQ(entries->value(), 0);
        EXPECT_EQ(entries->text().trimmed(), QStringLiteral("不限"));

        const auto policy = GyanisScribe::History::policyFromSettings(*m_store);
        EXPECT_EQ(policy.maximumEntries, 0) << "界面摆到「不限」而策略读成别的数";
        EXPECT_EQ(m_store->intValue(GyanisScribe::Settings::kHistoryMaximumEntries, -999), -1) << "回落不该顺手把文件里的坏值改掉：它要留着让人查得到";
    }

    /**
     * @brief 钉住「导出→导入」真的把值搬回来，并且逐项发出变更通知
     * @details 需求给 Settings 那一节列了「支持配置导入导出」，而这份能力此前零调用方：接口、实现与
     *          SettingsStore 自己的用例都齐，全仓没有一处按过它。接进界面之后要证的不是它会写文件，
     *          而是「导出的那份能回到同一批键，并且订阅配置的模块真的被叫醒」——后半句只有通知判据抓得到。
     */
    TEST_F(MainWindowFixture, ExportThenImportRoundTripWakesTheSubscribers)
    {
        ASSERT_TRUE(static_cast<bool>(m_store->setValue(GyanisScribe::Settings::kHistoryMaximumEntries, 77)));
        ASSERT_TRUE(static_cast<bool>(m_store->setValue(GyanisScribe::Settings::kOcrLanguage, QStringLiteral("eng"))));
        makeWindow();

        QStringList                                notified;
        QObject                                    receiver;
        std::optional<GyanisScribe::Ui::ToastKind> kind;
        QString                                    toast;
        QObject::connect(m_store.get(), &GyanisScribe::Settings::ISettingsStore::valueChanged, &receiver, [&notified](const QString &key) { notified << key; });
        QObject::connect(m_window.get(), &MainWindow::messageForUser, &receiver,
                         [&toast, &kind](const QString &message, const GyanisScribe::Ui::ToastKind received)
                         {
                             toast = message;
                             kind  = received;
                         });

        const auto packed = QString::fromStdWString((m_directory / "exported.json").wstring());
        m_window->exportSettingsToFile(packed);
        ASSERT_TRUE(QFileInfo::exists(packed)) << "导出没落盘";
        EXPECT_TRUE(notified.isEmpty()) << "导出只是读配置，不该改动任何一项";

        // 先把本地这一项改掉，导入才看得出「值真的回来了」而不是「什么都没动」
        ASSERT_TRUE(static_cast<bool>(m_store->setValue(GyanisScribe::Settings::kHistoryMaximumEntries, 1)));
        notified.clear();

        m_window->importSettingsFromFile(packed);
        EXPECT_EQ(m_store->intValue(GyanisScribe::Settings::kHistoryMaximumEntries, -1), 77) << "导入没把值搬回来";
        EXPECT_EQ(m_store->stringValue(GyanisScribe::Settings::kOcrLanguage, QString{}), QStringLiteral("eng"));
        EXPECT_TRUE(notified.contains(QStringLiteral("History/MaximumEntries"))) << "导入没发变更通知：订阅热键与主题的模块会停在旧值";
        ASSERT_TRUE(kind.has_value()) << "导入没落一条气泡提示";
        EXPECT_EQ(*kind, GyanisScribe::Ui::ToastKind::Success);
        EXPECT_TRUE(toast.contains(QStringLiteral("已导入"))) << toast.toStdString();
        EXPECT_TRUE(toast.contains(QStringLiteral("重启"))) << toast.toStdString() << "提示里要写明哪些改动还要重启";
    }

    /**
     * @brief 钉住「不是本程序导出的那份」会被拒，而且不动已有配置
     * @details 一句「已导入 0 项」与真的失败在界面上长得一样，而后果完全不同：前者会让人觉得备份已生效。
     *          拒绝必须是 Danger 一档、要点名形状问题，且已有的值一格都不许被半份导入改掉。
     */
    TEST_F(MainWindowFixture, ImportRejectsAFileThatIsNotOurExport)
    {
        const auto bogus = QString::fromStdWString((m_directory / "bogus.json").wstring());
        {
            QFile file{bogus};
            ASSERT_TRUE(file.open(QIODevice::WriteOnly));
            file.write("[]"); // 数组而不是对象：不是导出那一份的形状
            file.close();
        }

        ASSERT_TRUE(static_cast<bool>(m_store->setValue(GyanisScribe::Settings::kHistoryMaximumEntries, 42)));
        makeWindow();

        QObject                                    receiver;
        std::optional<GyanisScribe::Ui::ToastKind> kind;
        QString                                    toast;
        QObject::connect(m_window.get(), &MainWindow::messageForUser, &receiver,
                         [&toast, &kind](const QString &message, const GyanisScribe::Ui::ToastKind received)
                         {
                             toast = message;
                             kind  = received;
                         });

        m_window->importSettingsFromFile(bogus);
        ASSERT_TRUE(kind.has_value()) << "坏文件走掉了却没给任何提示";
        EXPECT_EQ(*kind, GyanisScribe::Ui::ToastKind::Danger) << toast.toStdString();
        EXPECT_NE(toast.indexOf(QStringLiteral("JSON")), -1) << "拒绝要指名是形状不对：" << toast.toStdString();
        EXPECT_EQ(m_store->intValue(GyanisScribe::Settings::kHistoryMaximumEntries, -1), 42) << "被拒的导入动了已有配置";
    }

} // namespace
