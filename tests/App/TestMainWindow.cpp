// 主窗口的接线判据：导航行—页内容—配置键三者要互指。
// 逐格核对的写法来自一个具体的坑：设置页的每一行都同时有「界面里的控件」和「另一头的读端」，
// 只测其中一头时，另一头改成别的键也照样全绿。
#include "App/CaptureIntent.h"
#include "App/MainWindow.h"

#include "App/TrayController.h"
#include "Annotation/AnnotationToolbar.h"
#include "History/HistoryRules.h"
#include "History/HistoryStore.h"
#include "Pin/PinLayout.h"
#include "Platform/IPlatformIntegration.h"
#include "Record/RecorderConfig.h"
#include "Core/Text.h"
#include "Settings/ISettingsStore.h"
#include "Settings/SettingsKeys.h"
#include "Settings/SettingsStore.h"
#include "Ui/DesignTokens.h"
#include "Ui/HotkeyEdit.h"
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
#include <QLineEdit>
#include <QListWidget>
#include <QMenu>
#include <QPushButton>
#include <QSpinBox>
#include <QStackedWidget>
#include <QString>
#include <QStringList>
#include <QToolButton>
#include <QWidget>

#include <algorithm>
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
        /// globalHotkeys 默认给真值：这一份装置要测的是「能改」那一格，不能改的那一格由用例自己关掉它
        /// 能力表同时留在 m_capabilities 里：动作条的用例要按同一份表算出应当落点的条目
        void makeWindow(const bool globalHotkeys = true)
        {
            m_integration                = GyanisScribe::Platform::createPlatformIntegration();
            m_capabilities               = GyanisScribe::Platform::PlatformCapabilities{};
            m_capabilities.globalHotkeys = globalHotkeys;
            m_window                     = std::make_unique<MainWindow>(*m_store, *m_theme, *m_icons, *m_historyStore, m_capabilities, *m_integration);
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
        /// 建窗口时那份能力表，动作条的用例按它算出预期条目
        GyanisScribe::Platform::PlatformCapabilities                  m_capabilities;
        std::unique_ptr<GyanisScribe::Platform::IPlatformIntegration> m_integration; ///< 平台动作（开机启动那一格要真对象）
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
                Expectation{MainWindowPage::Capture, {"截完之后", "延时截图", "截图目录", "颜色写法", "自动保存所有截图", "截图音效", "成品加边框", "成品加投影"}, true},
                Expectation{MainWindowPage::Annotation, {"默认线宽", "默认字号"}, true},
                Expectation{MainWindowPage::Pin, {"默认不透明度"}, true},
                Expectation{MainWindowPage::Record, {"录屏目录", "录制范围", "输出格式", "默认帧率", "画面码率", "光标高亮", "麦克风设备", "系统声音设备"}, true},
                Expectation{MainWindowPage::Ocr, {"识别语言", "语言包目录"}, true},
                Expectation{MainWindowPage::Hotkey, {"截图", "录屏", "文字识别", "屏幕取色", "快速保存"}, true},
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
     * @brief 钉住画面码率这一行写进 Record/VideoBitrateKbps，且范围与读端同解
     * @details 0 这一档必须有名字：框里显示一个光秃秃的 0，用户读出来的是「没设码率」，
     *          而它其实是「按质量优先」这一档的取值。
     */
    TEST_F(MainWindowFixture, VideoBitrateRowWritesTheKeyRecordingStartUses)
    {
        makeWindow();
        m_window->open(MainWindowPage::Record);
        auto *spin = qobject_cast<QSpinBox *>(controlInCurrentPage("画面码率"));
        ASSERT_NE(spin, nullptr);
        EXPECT_EQ(spin->value(), GyanisScribe::Record::kDefaultVideoBitrateKbps);
        EXPECT_EQ(spin->minimum(), GyanisScribe::Record::kDefaultVideoBitrateKbps) << "界面给不出 0 就没法表达按质量优先";
        EXPECT_EQ(spin->maximum(), GyanisScribe::Record::kMaximumVideoBitrateKbps) << "上限要与读端拒收的那一格同解，否则界面能给出运行期不收的码率";
        // textFromValue 是 protected，读用户真看见的那一格：把码率改成 0 之后行编辑里应当是这一档的名字
        auto *bitrateEdit = spin->findChild<QLineEdit *>();
        ASSERT_NE(bitrateEdit, nullptr);
        spin->setValue(0);
        EXPECT_EQ(bitrateEdit->text(), QStringLiteral("按质量优先")) << "0 这一档在界面上没有名字，用户读出来的是「没设码率」";

        spin->setValue(4000);
        closeWindowAndFlush();

        const auto reopened = makeStore();
        const auto stored   = reopened->intValue(GyanisScribe::Settings::kRecordVideoBitrate, -1);
        EXPECT_EQ(stored, 4000);
        const auto parsed = GyanisScribe::Record::parseVideoBitrateSetting(stored);
        ASSERT_TRUE(static_cast<bool>(parsed));
        EXPECT_EQ(*parsed, 4000);
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
     * @brief 钉住截图页四枚新开关各自写进它那一个键，默认档与消费端读的时候给的一致
     * @details 自动保存、音效、边框、投影四个键都只在 AppController 那几条路里现读：
     *          键写错一格不会报错，只会表现成「开关拨了没反应」——四处各验一次才分得清。
     */
    TEST_F(MainWindowFixture, CaptureOutputRowsWriteTheirOwnKeys)
    {
        makeWindow();
        m_window->open(MainWindowPage::Capture);

        const auto rows = std::array{std::pair{"自动保存所有截图", GyanisScribe::Settings::kCaptureAutoSave}, std::pair{"截图音效", GyanisScribe::Settings::kCaptureSound},
                                     std::pair{"成品加边框", GyanisScribe::Settings::kOutputBorder}, std::pair{"成品加投影", GyanisScribe::Settings::kOutputShadow}};
        for (const auto &[name, key]: rows)
        {
            auto *check = qobject_cast<QCheckBox *>(controlInCurrentPage(name));
            ASSERT_NE(check, nullptr) << "截图页没有这一格：" << name;
            EXPECT_FALSE(check->isChecked()) << "「" << name << "」的默认档应当是关：它一开就改每一张成品";
            check->setChecked(true);
        }
        closeWindowAndFlush();

        const auto reopened = makeStore();
        for (const auto &[name, key]: rows)
        {
            EXPECT_TRUE(reopened->boolValue(key, false)) << "「" << name << "」没写进 " << key;
        }
    }

    /**
     * @brief 钉住录制范围这一行写的是 Record 词表里的 token，且界面默认档就是读端初值那一档
     * @details 存中文标签的话下次录屏读不出来（parseRecordScope 认的是 FullScreen/Region/Window），
     *          而「录什么」猜错比帧率猜错重得多：用户以为录的是那片选区，实际录了整个桌面。
     */
    TEST_F(MainWindowFixture, RecordScopeRowWritesTheTokenRecordingStartUses)
    {
        makeWindow();
        m_window->open(MainWindowPage::Record);
        auto *combo = qobject_cast<QComboBox *>(controlInCurrentPage("录制范围"));
        ASSERT_NE(combo, nullptr);
        EXPECT_EQ(combo->count(), static_cast<int>(GyanisScribe::Record::kRecordScopeChoices.size())) << "下拉框的行数与词表不同解";
        EXPECT_EQ(combo->currentData().toString(), QString::fromUtf8(std::string{GyanisScribe::Record::recordScopeText(GyanisScribe::Record::RecorderConfig{}.mode)}))
                << "没存过时要显示的那一档，必须就是录屏读端自己回落的那一档";

        const auto regionRow = combo->findData(QStringLiteral("Region"));
        ASSERT_NE(regionRow, -1) << "词表里没有 Region 这一档，设置页选得出却写不进配置";
        combo->setCurrentIndex(regionRow);
        closeWindowAndFlush();

        const auto reopened = makeStore();
        const auto stored   = reopened->stringValue(GyanisScribe::Settings::kRecordScope, QString{});
        EXPECT_EQ(stored, QStringLiteral("Region"));
        const auto parsed = GyanisScribe::Record::parseRecordScope(stored);
        ASSERT_TRUE(static_cast<bool>(parsed));
        EXPECT_EQ(*parsed, GyanisScribe::Record::RecordMode::Region);
    }

    /**
     * @brief 钉住「截完之后」这一行写的是词表里的 token，且界面初值就是读端回落的那一档
     * @details 存中文标签的话下次截图读不出来（parseCaptureIntent 认的是 Annotate/Copy/Save/Pin/Recognize）；
     *          而这一格猜错的分量是「每按一次热键就多出一层窗」或「以为存了其实没存」。
     */
    TEST_F(MainWindowFixture, CaptureAfterActionRowWritesTheTokenTheCaptureUses)
    {
        makeWindow();
        m_window->open(MainWindowPage::Capture);
        auto *combo = qobject_cast<QComboBox *>(controlInCurrentPage("截完之后"));
        ASSERT_NE(combo, nullptr);
        EXPECT_EQ(combo->count(), static_cast<int>(GyanisScribe::App::kCaptureIntentChoices.size())) << "下拉框的行数与词表不同解";
        EXPECT_EQ(combo->currentData().toString(), QString::fromUtf8(std::string{GyanisScribe::App::captureIntentText(GyanisScribe::App::defaultCaptureIntent())}))
                << "没存过时要显示的那一档，必须就是控制器读不出时回落的那一档";

        const auto saveRow = combo->findData(QStringLiteral("Save"));
        ASSERT_NE(saveRow, -1) << "词表里没有 Save 这一档，设置页选得出却写不进配置";
        combo->setCurrentIndex(saveRow);
        closeWindowAndFlush();

        const auto reopened = makeStore();
        const auto stored   = reopened->stringValue(GyanisScribe::Settings::kCaptureAfterAction, QString{});
        EXPECT_EQ(stored, QStringLiteral("Save"));
        const auto parsed = GyanisScribe::App::parseCaptureIntent(stored);
        ASSERT_TRUE(static_cast<bool>(parsed));
        EXPECT_EQ(*parsed, GyanisScribe::App::CaptureIntent::SaveToFile) << "界面选的那一档与控制器实际走的那一步不是同一个";
    }

    /**
     * @brief 钉住取声那两格列不出设备时不改写配置，也保持「不取这一路」的显示
     * @details 设备可能只是临时被别的程序占着。在这里替用户把选择清掉，等于把
     *          「下次插回来就恢复原样」这条退路删了；而真正该报错的地方在开录那一刻。
     */
    TEST_F(MainWindowFixture, AudioDeviceRowKeepsTheStoredSelectionWhenListingFails)
    {
        ASSERT_TRUE(static_cast<bool>(m_store->setValue(GyanisScribe::Settings::kRecordAudioMicrophone, QStringLiteral("@device_cm_{GUID}\\wave_{MIC}"))));

        makeWindow();
        m_window->open(MainWindowPage::Record);
        const auto *combo = qobject_cast<QComboBox *>(controlInCurrentPage("麦克风设备"));
        ASSERT_NE(combo, nullptr);
        EXPECT_EQ(combo->count(), 1) << "这台机器上没有真 ffmpeg 与音频设备，清单就该只有「不取这一路」这一项";
        EXPECT_EQ(combo->currentIndex(), 0) << "列不出设备时下拉框不许停在一个凭空的位置上";
        EXPECT_EQ(m_store->stringValue(GyanisScribe::Settings::kRecordAudioMicrophone, QString()), QStringLiteral("@device_cm_{GUID}\\wave_{MIC}"))
                << "列不出来就把用户选的那一路清掉，是替他做了一个他没见过的原因导致的决定";
    }

    /**
     * @brief 钉住「GIF 带不了声音」时取声那两格是灰的，改回 MP4 又亮
     * @details 灰掉而不是允许选：允许选就会起一份取声的 ffmpeg，录完才发现那种容器没有音轨，
     *          那份 wav 除了占磁盘没有别的用处。判据取自 Record::outputFormatCarriesAudio，与服务端同一句。
     */
    TEST_F(MainWindowFixture, AudioRowsGreyOutForTheFormatThatCannotCarrySound)
    {
        ASSERT_TRUE(static_cast<bool>(m_store->setValue(GyanisScribe::Settings::kRecordFormat, QStringLiteral("Gif"))));

        makeWindow();
        m_window->open(MainWindowPage::Record);
        auto *microphone  = qobject_cast<QComboBox *>(controlInCurrentPage("麦克风设备"));
        auto *systemSound = qobject_cast<QComboBox *>(controlInCurrentPage("系统声音设备"));
        auto *format      = qobject_cast<QComboBox *>(controlInCurrentPage("输出格式"));
        ASSERT_NE(microphone, nullptr);
        ASSERT_NE(systemSound, nullptr);
        ASSERT_NE(format, nullptr);

        EXPECT_FALSE(microphone->isEnabled()) << "存成 GIF 也允许选取声，就是允许起一份没人听的 ffmpeg";
        EXPECT_FALSE(systemSound->isEnabled());

        format->setCurrentIndex(format->findData(QStringLiteral("Mp4")));
        EXPECT_TRUE(microphone->isEnabled()) << "改回 MP4 之后这两格该恢复可选";
        EXPECT_TRUE(systemSound->isEnabled());
    }

    /**
     * @brief 钉住「配置里的录制范围读不出」时界面按初值档显示，而读端仍然拒绝开录
     * @details 两边都不许静默变形：界面不能把坏值当成用户选过的一档顺手写回去，
     *          读端更不能悄悄按整屏开始——录下来的画面与用户想要的完全不是一回事。
     */
    TEST_F(MainWindowFixture, GarbageStoredScopeShowsDefaultButStillBlocksTheStart)
    {
        ASSERT_TRUE(static_cast<bool>(m_store->setValue(GyanisScribe::Settings::kRecordScope, QStringLiteral("region"))));

        makeWindow();
        m_window->open(MainWindowPage::Record);
        const auto *combo = qobject_cast<QComboBox *>(controlInCurrentPage("录制范围"));
        ASSERT_NE(combo, nullptr);
        EXPECT_EQ(combo->currentData().toString(), QStringLiteral("FullScreen")) << "读不出时按初值档显示";
        EXPECT_EQ(m_store->stringValue(GyanisScribe::Settings::kRecordScope, QString{}), QStringLiteral("region")) << "回落不该顺手把文件里的坏值改掉：它要留着让人查得到";

        const auto parsed = GyanisScribe::Record::parseRecordScope(QStringLiteral("region"));
        ASSERT_FALSE(static_cast<bool>(parsed)) << "读端把坏值认成一整档，就是拿整屏去录用户要的选区";
    }

    /**
     * @brief 钉住输出格式这一行写的是词表 token，且坏值只在界面回落、不顶掉盘上那份
     * @details 存「GIF」这种给人看的写法，下次录屏读不出来（parseRecordFormat 认的是 Mp4/Gif）；
     *          两种容器走两条编码路线，格式错了整段成品作废，所以读端是拒绝开始而不是猜一档。
     */
    TEST_F(MainWindowFixture, RecordFormatRowWritesTheTokenRecordingStartUses)
    {
        ASSERT_TRUE(static_cast<bool>(m_store->setValue(GyanisScribe::Settings::kRecordFormat, QStringLiteral("gif"))));

        makeWindow();
        m_window->open(MainWindowPage::Record);
        auto *combo = qobject_cast<QComboBox *>(controlInCurrentPage("输出格式"));
        ASSERT_NE(combo, nullptr);
        EXPECT_EQ(combo->count(), static_cast<int>(GyanisScribe::Record::kRecordFormatChoices.size())) << "下拉框的行数与词表不同解";
        EXPECT_EQ(combo->currentData().toString(), QString::fromUtf8(std::string{GyanisScribe::Record::recordFormatText(GyanisScribe::Record::RecorderConfig{}.format)}))
                << "读不出时按初值档显示";
        EXPECT_EQ(m_store->stringValue(GyanisScribe::Settings::kRecordFormat, QString{}), QStringLiteral("gif")) << "回落不该顺手把文件里的坏值改掉：它要留着让人查得到";
        ASSERT_FALSE(static_cast<bool>(GyanisScribe::Record::parseRecordFormat(QStringLiteral("gif")))) << "读端把坏值认成一档，就是拿错的容器压已经抓完的帧";

        const auto gifRow = combo->findData(QStringLiteral("Gif"));
        ASSERT_NE(gifRow, -1) << "词表里没有 Gif 这一档，设置页选得出却写不进配置";
        combo->setCurrentIndex(gifRow);
        closeWindowAndFlush();

        const auto reopened = makeStore();
        const auto stored   = reopened->stringValue(GyanisScribe::Settings::kRecordFormat, QString{});
        EXPECT_EQ(stored, QStringLiteral("Gif"));
        const auto parsed = GyanisScribe::Record::parseRecordFormat(stored);
        ASSERT_TRUE(static_cast<bool>(parsed));
        EXPECT_EQ(*parsed, GyanisScribe::Record::OutputFormat::Gif);
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


    /**
     * @brief 钉住开机自启那一格与平台状态同解，且这一格不写配置
     * @details 用例只读不点：点下去会写这台机器的启动项，测试不该改宿主的系统设置。
     *          状态的真源是系统里那条启动项，所以断言取的是平台现读的读数，而不是配置里的某一格。
     */
    TEST_F(MainWindowFixture, AutostartRowMirrorsThePlatformWithoutWriting)
    {
        makeWindow();

        QCheckBox *check = nullptr;
        for (auto *candidate: m_window->findChildren<QCheckBox *>())
        {
            if (candidate->accessibleName() == GyanisScribe::Core::uiText("开机时自动启动"))
            {
                check = candidate;
                break;
            }
        }
        ASSERT_NE(check, nullptr) << "通用页该有开机自启那一格";

        const auto state = m_integration->autostartEnabled();
        if (!state)
        {
            EXPECT_FALSE(check->isEnabled()) << "本平台没有这一能力时不该给点";
            EXPECT_EQ(check->toolTip(), QString::fromStdString(state.error().message)) << "原因要摆进提示里，而不是让用户猜";
            return;
        }
        EXPECT_TRUE(check->isEnabled());
        EXPECT_EQ(check->isChecked(), *state) << "勾的初值要与系统里的启动项一致";
    }


    /**
     * @brief 钉住两处下拉的每一项都带着词表里那一枚图标
     * @details 图标只挂在整条下拉上没有区分作用；按项挂才有意义，所以这里逐项核装饰角色，
     *          并且要求每一项都有——只挂前两项会看着像后几项漏了图。
     */
    TEST_F(MainWindowFixture, ChoiceCombosCarryPerItemIcons)
    {
        makeWindow();

        for (const auto &name: {QStringLiteral("录制范围"), QStringLiteral("截完之后")})
        {
            QComboBox *combo = nullptr;
            for (auto *candidate: m_window->findChildren<QComboBox *>())
            {
                if (candidate->accessibleName() == name)
                {
                    combo = candidate;
                    break;
                }
            }
            ASSERT_NE(combo, nullptr) << name.toStdString() << " 这一条下拉没找到";
            ASSERT_GT(combo->count(), 0) << name.toStdString() << " 下拉里一项都没有";

            for (int index = 0; index < combo->count(); ++index)
            {
                EXPECT_FALSE(combo->itemData(index, Qt::DecorationRole).isNull()) << name.toStdString() << " 的第 " << index << " 项没有图标";
            }
        }
    }

} // namespace

/**
 * @brief 钉住「换主题时，设置页的导航行与按钮图标会重取一次」
 * @details 描边色对不对的像素判据在 TestIconProvider 里，这一条钉的是接线：
 *          设置窗是常驻窗口，图标是建窗时按当时的描边色拍的快照，不重取就停在旧色上
 *          （深色档等于看不见）。断言取「图标对象被重新赋过」与「名字留在控件上」两件事。
 */
TEST_F(MainWindowFixture, IconsReResolveWhenTheThemeChangesLive)
{
    ASSERT_TRUE(m_store->setValue(GyanisScribe::Settings::kThemeMode, QStringLiteral("Light")).has_value());
    makeWindow();

    auto *list = nav();
    ASSERT_NE(list, nullptr);
    ASSERT_GE(list->count(), 2);

    QPushButton *exportButton = nullptr;
    for (auto *button: m_window->findChildren<QPushButton *>())
    {
        if (button->accessibleName() == GyanisScribe::Core::uiText("导出配置"))
        {
            exportButton = button;
            break;
        }
    }
    ASSERT_NE(exportButton, nullptr) << "设置页找不到导出按钮，这条判据就没对象了";
    ASSERT_FALSE(exportButton->icon().isNull());

    const auto navKeyBefore    = list->item(0)->icon().cacheKey();
    const auto buttonKeyBefore = exportButton->icon().cacheKey();

    ASSERT_TRUE(m_theme->setTheme(GyanisScribe::Ui::ThemeKind::Dark).has_value());

    EXPECT_NE(list->item(0)->icon().cacheKey(), navKeyBefore) << "换主题后导航行的图标没重取：描边色停在旧档";
    EXPECT_NE(exportButton->icon().cacheKey(), buttonKeyBefore) << "换主题后按钮图标没重取：走的是 setIcon 而不是 applyIcon，名字没记在控件上";
}

/**
 * @brief 钉住主窗口动作条与托盘菜单同源：能力过滤后的每一条动作都有落点，且不许多出窗口自己的那三条
 * @details 表是唯一出处，所以预期按同一份表算；控件按文字找而不是按下标——findChildren 的顺序
 *          不保证等于创建顺序（工具栏那几条用例已经为此踩过一次）。
 */
TEST_F(MainWindowFixture, ActionBarLandsEveryVisibleTrayEntrySomewhere)
{
    using GyanisScribe::App::TrayAction;
    using GyanisScribe::App::visibleTrayMenuEntries;

    makeWindow(true);

    auto *bar = m_window->findChild<QWidget *>(QStringLiteral("actionBar"));
    ASSERT_NE(bar, nullptr) << "动作条没建出来：没有托盘的机器上就一条常设入口都没有";
    auto *more = bar->findChild<QToolButton *>();
    ASSERT_NE(more, nullptr) << "「更多」按钮不见了：次要动作没有落点";
    ASSERT_NE(more->menu(), nullptr) << "「更多」没挂菜单：条目会被静默丢掉";

    const auto buttons     = bar->findChildren<QPushButton *>();
    const auto menuActions = more->menu()->actions();

    int        primaryCount   = 0;
    int        secondaryCount = 0;
    QString    firstPrimaryLabel;
    TrayAction firstPrimaryAction = TrayAction::Quit;

    for (const auto &entry: visibleTrayMenuEntries(m_capabilities))
    {
        const auto action = entry.action;
        if (!action)
        {
            continue; // 表里的分隔线在动作条上没有对应物
        }
        if (*action == TrayAction::OpenSettings || *action == TrayAction::OpenAbout || *action == TrayAction::Quit)
        {
            // 这三条说的就是这扇窗口本身：再摆一次等于给用户两个入口去开同一个东西
            EXPECT_EQ(std::ranges::find(buttons, entry.label, &QPushButton::text), buttons.end()) << "「" << entry.label.toStdString() << "」是窗口自己的事，不该出现在动作条上";
            continue;
        }
        if (entry.hotkeyKey.empty())
        {
            ++secondaryCount;
            EXPECT_NE(std::ranges::find(menuActions, entry.label, &QAction::text), menuActions.end()) << "次要动作「" << entry.label.toStdString() << "」在「更多」里找不到落点";
            continue;
        }
        ++primaryCount;
        const auto found = std::ranges::find(buttons, entry.label, &QPushButton::text);
        ASSERT_NE(found, buttons.end()) << "主操作「" << entry.label.toStdString() << "」没摆成按钮";
        if (firstPrimaryLabel.isEmpty())
        {
            firstPrimaryLabel  = entry.label;
            firstPrimaryAction = *action;
        }
    }

    // 总数也要同解：多出来的按钮或菜单项意味着表和条散开了
    EXPECT_EQ(static_cast<int>(buttons.size()), primaryCount) << "动作条上的按钮数与表里带热键提示的条目数不一致";
    EXPECT_EQ(static_cast<int>(menuActions.size()), secondaryCount) << "「更多」里的条目数与表里其余条目数不一致";
    ASSERT_FALSE(firstPrimaryLabel.isEmpty()) << "表里一条带热键提示的主操作都没有，这条用例就什么都没判";

    // 按下去要出来的就是它那一个动作：接错一格就是「点了截图却开始录屏」
    int        emissions = 0;
    TrayAction emitted   = TrayAction::Quit;
    QObject::connect(m_window.get(), &MainWindow::actionRequested,
                     [&emissions, &emitted](const TrayAction action)
                     {
                         ++emissions;
                         emitted = action;
                     });
    auto *clicked = *std::ranges::find(buttons, firstPrimaryLabel, &QPushButton::text);
    clicked->click();
    EXPECT_EQ(emissions, 1) << "按钮点了却没发信号，或发了不止一次";
    EXPECT_TRUE(emitted == firstPrimaryAction) << "按「" << firstPrimaryLabel.toStdString() << "」发出来的不是它自己的动作";
}

/**
 * @brief 钉住热键页的登记入口跟着平台能力位走：没有登记口的平台上不许装作改了会生效
 * @details 两格都判：能力位为真时每一行都能编辑（少了这一格，把 setEnabled 摘掉也不会红），
 *          为假时所有行都灰掉但仍在——行消失了用户就看不见自己配过什么。
 */
TEST_F(MainWindowFixture, HotkeyEntriesFollowTheGlobalHotkeyCapability)
{
    makeWindow(true);
    m_window->open(MainWindowPage::Hotkey);
    auto edits = m_window->findChildren<GyanisScribe::Ui::HotkeyEdit *>();
    ASSERT_EQ(edits.size(), 8) << "热键页该有截图、录屏、文字识别、屏幕取色、滚动长截图、快速保存、收起/摆回贴图、切换贴图分组八行";
    for (auto *edit: edits)
    {
        EXPECT_TRUE(edit->isEnabled()) << "有登记口的平台上这八行要能改";
    }

    makeWindow(false);
    m_window->open(MainWindowPage::Hotkey);
    edits = m_window->findChildren<GyanisScribe::Ui::HotkeyEdit *>();
    ASSERT_EQ(edits.size(), 8) << "没有登记口也不能把行藏掉：用户配过的组合键要还在眼前";
    for (auto *edit: edits)
    {
        EXPECT_FALSE(edit->isEnabled()) << "这台机器上改完不会生效，入口就该是灰的";
    }
}
