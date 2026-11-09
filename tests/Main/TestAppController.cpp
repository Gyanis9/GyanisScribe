// 「把各模块接起来」那一层的直测：延时截图的路由与成品进历史。
// 这一段此前一个实例都建不出来（构造函数自己造设置、抓帧、托盘这些真模块，没有注入口），
// 「截取当前窗口绕过延时」就是这么漏下来的；这一批给它一套与 TestModuleFlow 同形状的替身，
// 从 AppControllerParts 注进去，把「谁在什么时刻被叫到、成品记在哪一份清单上」钉住。

#include "App/CaptureCoordinator.h"
#include "App/CustomHotkeys.h"
#include "App/PrivacyCoordinator.h"
#include "App/RewindCoordinator.h"
#include "Core/Ocr/OcrTypes.h"
#include "Gui/Annotation/AnnotationWindow.h"
#include "Gui/Capture/CaptureOverlay.h"
#include "App/TrayController.h"
#include "Gui/Pick/ColorPickerOverlay.h"
#include "Gui/Pin/PinWindow.h"
#include "Gui/Record/RecordingHud.h"
#include "Gui/Rewind/RewindPickerWindow.h"
#include "Main/AppController.h"

#include "App/CaptureIntent.h"
#include "Base/Text.h"
#include "Core/Capture/IScreenCaptureSource.h"
#include "Core/Capture/RegionHistory.h"
#include "Base/Paths.h"
#include "Base/Types.h"
#include "Core/History/HistoryRules.h"
#include "Core/History/HistoryStore.h"
#include "Core/History/HistoryTypes.h"
#include "Core/Pin/PinLayout.h"
#include "Gui/Pin/PinManager.h"
#include "Platform/IPlatformIntegration.h"
#include "Platform/PlatformCapabilities.h"
#include "Core/Settings/SettingsKeys.h"
#include "Core/Settings/SettingsStore.h"

#include <gtest/gtest.h>

#include <QApplication>
#include <QClipboard>
#include <QColor>
#include <QCoreApplication>
#include <QDateTime>
#include <QDialog>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QGuiApplication>
#include <QImage>
#include <QKeyEvent>
#include <QLabel>
#include <QMouseEvent>
#include <QPushButton>
#include <QRect>
#include <QScreen>
#include <QSize>
#include <QString>
#include <QTimer>
#include <QToolButton>
#include <QVector>

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <format>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <system_error>
#include <thread>
#include <type_traits>
#include <utility>
#include <vector>

namespace GyanisScribe
{
    namespace
    {
        int                           argumentCount = 1;
        std::unique_ptr<QApplication> application;

        /// argv 缓冲要有静态存储期：QApplication 只存指针不拷贝
        inline static char argumentZero[] = "TestAppController";
        char              *arguments[]    = {argumentZero, nullptr};

        constexpr quintptr kForegroundWindowHandle = 0x51CE; ///< 替身每次交回的这一枚句柄
        constexpr int      kFrameWidth             = 64;
        constexpr int      kFrameHeight            = 48;

        /// 与任何真屏都不重名的屏名：交付会走到「屏不在」的兜底，不开标注窗也不记账
        constexpr char kGhostScreenName[] = "GhostScreen";

        /**
         * @brief 替掉真实抓屏：每一路各交回同一帧，并把「被叫了几次、叫的是什么」留住
         * @details 计数是这几条判据的落脚点：延时那一档要判「到点之前一次都没被叫」，
         *          换入口那一档要判「叫的是哪一路」。
         */
        class CountingScreenSource final : public GyanisScribe::Core::IScreenCaptureSource
        {
        public:
            explicit CountingScreenSource(Base::CapturedImage frame) : m_frame(std::move(frame))
            {
            }

            [[nodiscard]] Base::Expected<Base::CapturedImage> grabScreen(const QString &screenName) const override
            {
                if (onGrabScreen)
                {
                    // 抓的那一刻屏上摆着什么由用例自己看：F5 那条判据就落在「标注窗藏没藏」上
                    onGrabScreen();
                }
                ++screenCalls;
                requestedScreen = screenName;
                if (m_nextScreenError)
                {
                    // 只失败这一次：失败路径的用例要在同一轮里看到窗被放回原处
                    const auto error = std::move(*m_nextScreenError);
                    m_nextScreenError.reset();
                    return std::unexpected(error);
                }
                return m_frame;
            }

            /// 让下一次整屏抓帧带上这条原因失败一次：F5 的失败路径要看到窗与画布原样留着
            void failNextScreenGrab(Base::Error error) const
            {
                m_nextScreenError = std::move(error);
            }

            /// 换掉之后每一路交回的那一帧：F5 那一格要在同一轮里把底图从红换成绿
            void setFrame(Base::CapturedImage frame)
            {
                m_frame = std::move(frame);
            }

            [[nodiscard]] Base::Expected<Base::CapturedImage> grabWindow(const quintptr handle) const override
            {
                ++windowCalls;
                requestedWindow = handle;
                return m_frame;
            }

            mutable int           screenCalls = 0;     ///< 整屏这一路被叫了几次
            mutable int           windowCalls = 0;     ///< 窗口这一路被叫了几次
            mutable QString       requestedScreen;     ///< 最近一次叫的屏名
            mutable quintptr      requestedWindow = 0; ///< 最近一次叫的窗口句柄
            std::function<void()> onGrabScreen;        ///< 每次整屏抓帧前叫一声，让用例记录当时的状态

        private:
            Base::CapturedImage                m_frame;           ///< 与生产同形状的那一帧假屏
            mutable std::optional<Base::Error> m_nextScreenError; ///< 下一次整屏抓帧要交回的原因；空即照常成功
        };

        /**
         * @brief 假热键后端：记录登记与整轮清场次数，并可按组合拒绝一次登记
         * @details 拒绝按 Qt 的修饰键与键值比对，与设置文本解析出来的组合一一对应；
         *          让某一组被拒即可把重登推到「恢复旧组合」那条路上。
         */
        class HotkeyBackendStub final : public Platform::IHotkeyBackend
        {
        public:
            /**
             * @brief 登记一个热键，落在预先点名的拒绝组合上就交回失败
             * @param identifier 本后端内的编号
             * @param modifiers Qt 修饰键集合
             * @param key 按键本体
             * @return 成功返回空载荷；拒绝组合返回「已被占用」的中文原因
             */
            [[nodiscard]] Base::Expected<void> registerHotkey(const int identifier, const Qt::KeyboardModifiers modifiers, const Qt::Key key) override
            {
                ++registerCalls;
                if (m_hasRejectedCombination && modifiers == m_rejectedModifiers && key == m_rejectedKey)
                {
                    return std::unexpected(Base::Error{Base::ErrorCode::Busy, "替身：这一组已被别的程序占用"});
                }
                m_registered.insert_or_assign(identifier, std::pair{modifiers, key});
                return {};
            }

            /**
             * @brief 注销一个热键
             * @param identifier 登记时用过的编号
             * @return 总是成功；没登记过的编号视作已不在册
             */
            [[nodiscard]] Base::Expected<void> unregisterHotkey(const int identifier) override
            {
                m_registered.erase(identifier);
                return {};
            }

            /// 整轮清场：次数是「重登了几轮」的落脚点
            void unregisterAll() override
            {
                ++unregisterAllCalls;
                m_registered.clear();
            }

            /// 点名下一批登记里要拒绝的组合
            void rejectCombination(const Qt::KeyboardModifiers modifiers, const Qt::Key key)
            {
                m_hasRejectedCombination = true;
                m_rejectedModifiers      = modifiers;
                m_rejectedKey            = key;
            }

            /// 在册登记的条数
            [[nodiscard]] int registeredCount() const
            {
                return static_cast<int>(m_registered.size());
            }

            /**
             * @brief 某一组组合此刻是否在册
             * @param modifiers Qt 修饰键集合
             * @param key 按键本体
             * @return 在册为 true
             */
            [[nodiscard]] bool isRegistered(const Qt::KeyboardModifiers modifiers, const Qt::Key key) const
            {
                return std::ranges::any_of(m_registered, [modifiers, key](const auto &entry) { return entry.second.first == modifiers && entry.second.second == key; });
            }

            int registerCalls      = 0; ///< 登记调用总次数（含被拒的）
            int unregisterAllCalls = 0; ///< 整轮清场次数

        private:
            std::map<int, std::pair<Qt::KeyboardModifiers, Qt::Key>> m_registered;                               ///< 编号 → 在册组合
            bool                                                     m_hasRejectedCombination = false;           ///< 是否点名了要拒绝的组合
            Qt::KeyboardModifiers                                    m_rejectedModifiers      = Qt::NoModifier;  ///< 被拒组合的修饰键
            Qt::Key                                                  m_rejectedKey            = Qt::Key_unknown; ///< 被拒组合的按键
        };

        /**
         * @brief 只演「前台窗口就是这一扇」的平台集成替身
         * @details 能力位全假：界面按「本平台给不出」筛菜单，与本组判据无关；
         *          前台窗口查询次数是「到点才取」那条判据的落脚点。
         */
        class ForegroundWindowStub final : public Platform::IPlatformIntegration
        {
        public:
            explicit ForegroundWindowStub(const quintptr handle) : m_handle(handle)
            {
            }

            [[nodiscard]] Platform::PlatformCapabilities capabilities() const noexcept override
            {
                Platform::PlatformCapabilities caps;
                caps.sessionNotifications = sessionNotifications;
                return caps;
            }

            [[nodiscard]] QString displayName() const override
            {
                return QStringLiteral("直测替身");
            }

            /**
             * @brief 交出一枚可观察的假热键后端
             * @return 新后端；同一替身只建一次，调用方经 hotkeyBackend 读它的计数
             */
            [[nodiscard]] std::unique_ptr<Platform::IHotkeyBackend> createHotkeyBackend() override
            {
                auto backend  = std::make_unique<HotkeyBackendStub>();
                hotkeyBackend = backend.get();
                return backend;
            }

            [[nodiscard]] Base::Expected<Platform::ForegroundWindow> foregroundWindow() const override
            {
                ++foregroundQueries;
                return Platform::ForegroundWindow{m_handle, QStringLiteral("替身窗口")};
            }

            [[nodiscard]] Base::Expected<bool> autostartEnabled() const override
            {
                return std::unexpected(Base::Error{Base::ErrorCode::PlatformUnsupported, "替身不演开机启动这一项"});
            }

            [[nodiscard]] Base::Expected<void> setAutostart(const bool enabled) override
            {
                static_cast<void>(enabled);
                return std::unexpected(Base::Error{Base::ErrorCode::PlatformUnsupported, "替身不演开机启动这一项"});
            }

            [[nodiscard]] Base::Expected<std::vector<Platform::ScreenWindow>> screenWindows() const override
            {
                return std::unexpected(Base::Error{Base::ErrorCode::PlatformUnsupported, "替身不演窗口清单这一项"});
            }

            [[nodiscard]] Base::Expected<std::vector<QRect>> elementChainAtPoint(const QPoint &globalLogicalPoint, const std::vector<quintptr> &excludeWindowHandles) const override
            {
                static_cast<void>(globalLogicalPoint);
                static_cast<void>(excludeWindowHandles);
                return std::unexpected(Base::Error{Base::ErrorCode::PlatformUnsupported, "替身不演界面元素这一项"});
            }

            mutable int        foregroundQueries = 0;       ///< 前台窗口被问了几次
            HotkeyBackendStub *hotkeyBackend     = nullptr; ///< 建出来的假热键后端；第一次重登之前为空
            bool               sessionNotifications = false; ///< 锁屏通知这一位；回溯协调器只在它为真时启用

        private:
            quintptr m_handle; ///< 每次交回同一枚句柄
        };

        /**
         * @brief 直测装置：替身把设置、平台、抓帧与历史清单全换成用例自己的那一份
         * @details 每一位成员字段就是一个开关：先摆好字段再 makeController()，
         *          和用例读起来更近（「这一格设了什么」一眼看得见）。
         */
        class AppControllerFixture : public ::testing::Test
        {
        protected:
            static void SetUpTestSuite()
            {
                // 控制器要建主窗口、托盘、气泡这些真模块：跑到离屏平台上，测试不在桌面上闪，
                // 与容器里那一条用同一个读数环境（env.sh 里同样 export QT_QPA_PLATFORM=offscreen）
                qputenv("QT_QPA_PLATFORM", "offscreen");
                application = std::make_unique<QApplication>(argumentCount, arguments);
                QCoreApplication::setOrganizationName(QStringLiteral("Gyanis"));
                QCoreApplication::setApplicationName(QStringLiteral("GyanisScribe-Tests"));
            }

            static void TearDownTestSuite()
            {
                application.reset();
            }

            void SetUp() override
            {
                const auto *info = ::testing::UnitTest::GetInstance()->current_test_info();
                m_root           = std::filesystem::temp_directory_path() / std::format("GyanisScribe-AppController.{}_{}", info->test_suite_name(), info->name());
                std::filesystem::remove_all(m_root);
                std::filesystem::create_directories(m_root);

                // 贴图布局与未保存归档读的是真实的（按测试应用名分开的）缓存目录：
                // 进来先清，别把上一轮留下的摆进这一轮
                std::error_code ignored;
                std::filesystem::remove_all(Base::Paths::cacheDirectory() / "Pins", ignored);
                std::filesystem::remove_all(Base::Paths::unsavedCaptureDirectory(), ignored);

                ASSERT_NE(QGuiApplication::primaryScreen(), nullptr) << "离屏平台该有一块屏，帧的屏名要对上它";
                m_realScreenName = QGuiApplication::primaryScreen()->name();

                // 剪贴板是进程外的共享资源：上一格用例留下的图会让「没写进剪贴板」判成写进去了
                QGuiApplication::clipboard()->clear();
            }

            void TearDown() override
            {
                m_controller.reset();
                std::error_code ignored;
                std::filesystem::remove_all(m_root, ignored);
                std::filesystem::remove_all(Base::Paths::cacheDirectory() / "Pins", ignored);
                std::filesystem::remove_all(Base::Paths::unsavedCaptureDirectory(), ignored);
            }

            /// 造一帧与生产同形状的假屏：逻辑与物理尺寸一致（倍比 1.0），屏名与选区由用例点名
            [[nodiscard]] static Base::CapturedImage makeFrame(const QString &screenName, const QRect &logicalRegion = {},
                                                               const QColor &fillColor = QColor(36, 87, 166))
            {
                QImage image(kFrameWidth, kFrameHeight, QImage::Format_ARGB32_Premultiplied);
                image.fill(fillColor);

                Base::CapturedImage frame;
                frame.image            = image;
                frame.logicalSize      = QSize(kFrameWidth, kFrameHeight);
                frame.devicePixelRatio = 1.0;
                frame.screenName       = screenName;
                frame.logicalRegion    = logicalRegion;
                return frame;
            }

            /// 用替身把控制器支起来：延时、去向与「未保存也进历史」都从上面那几格字段读
            /// @param restorePins 这一档要不要把上次的贴图摆回来；默认与常驻启动一致
            void makeController(const bool restorePins = true)
            {
                auto settings = std::make_unique<Core::SettingsStore>(m_root / "settings.ini");
                ASSERT_TRUE(settings->setValue(Core::kCaptureDelaySeconds, m_delaySeconds).has_value());
                ASSERT_TRUE(settings->setValue(Core::kPathScreenshot, Base::Paths::toDisplayText(m_root / "shots")).has_value());
                ASSERT_TRUE(settings->setValue(Core::kHistoryKeepUnsaved, m_keepUnsaved).has_value());
                ASSERT_TRUE(settings->setValue(Core::kCaptureAutoSave, m_autoSave).has_value());
                if (m_afterAction != std::nullopt)
                {
                    ASSERT_TRUE(settings->setValue(Core::kCaptureAfterAction, QString::fromUtf8(std::string{App::captureIntentText(*m_afterAction)}.c_str())).has_value());
                }
                if (m_recordingDirectory != std::nullopt)
                {
                    ASSERT_TRUE(settings->setValue(Core::kPathRecording, Base::Paths::toDisplayText(*m_recordingDirectory)).has_value());
                }
                if (m_recordingCountdown != std::nullopt)
                {
                    ASSERT_TRUE(settings->setValue(Core::kRecordCountdownSeconds, *m_recordingCountdown).has_value());
                }
                if (!m_customHotkeyText.isEmpty())
                {
                    // 预置一条自定义绑定：启动登记收进去之后，下一轮改动才有「旧组合」可恢复
                    ASSERT_TRUE(settings->setValue(App::customHotkeyKey("CaptureFullScreen").toStdString(), m_customHotkeyText).has_value());
                }
                if (m_rewindEnabled)
                {
                    // 控制器构造里就会按这一格启动回溯协调器：写晚了 start() 只会读到关闭
                    ASSERT_TRUE(settings->setValue(Core::kRewindEnabled, true).has_value());
                }
                m_settings = settings.get();

                auto source      = std::make_unique<CountingScreenSource>(makeFrame(m_frameScreenName, m_frameRegion, m_frameColor));
                auto integration = std::make_unique<ForegroundWindowStub>(kForegroundWindowHandle);
                // 回溯协调器在控制器构造里就 start()：能力位与开关必须先落在这份替身上，构造之后再补已经晚了
                integration->sessionNotifications = m_sessionNotifications;
                m_source                          = source.get();
                m_integration                     = integration.get();

                // 预置一份摆得回来的布局：借管理器的公开保存路写，免得「没摆回来」的判据对着空配置空转
                if (m_seedPinLayout)
                {
                    Gui::PinManager seeding(*m_settings, *m_integration);
                    QImage          image(8, 8, QImage::Format_ARGB32);
                    image.fill(Qt::green);
                    seeding.addPin(image, QPoint(40, 40));
                    ASSERT_TRUE(seeding.saveLayout().has_value()) << "布局没写下去，用例前提就没了";
                }

                Main::AppControllerParts parts;
                parts.settings           = std::move(settings);
                parts.integration        = std::move(integration);
                parts.captureSource      = std::move(source);
                parts.historyStoragePath = m_root / "history.json";
                m_controller             = std::make_unique<Main::AppController>(std::move(parts), restorePins);
            }


            /// 现在桌上有几扇这种顶层窗：接线断了时这一格就是 0
            template<typename Widget>
            [[nodiscard]] static int countTopLevelWindows()
            {
                int count = 0;
                for (auto *widget: QApplication::topLevelWidgets())
                {
                    if (qobject_cast<Widget *>(widget) != nullptr)
                    {
                        ++count;
                    }
                }
                return count;
            }

            /// 屏幕上此刻可见的那些气泡窗：提示是顶层空窗 + 无障碍名「提示」+ ToolTip 窗型（Toast 给自动化留的名字）
            [[nodiscard]] static std::vector<QWidget *> visibleToastBubbles()
            {
                std::vector<QWidget *> bubbles;
                for (auto *widget: QApplication::topLevelWidgets())
                {
                    const bool isToast =
                            widget->isVisible() && widget->accessibleName() == GyanisScribe::Base::uiText("提示") && (widget->windowFlags() & Qt::WindowType_Mask) == Qt::ToolTip;
                    if (isToast)
                    {
                        bubbles.push_back(widget);
                    }
                }
                return bubbles;
            }

            /// 屏幕上新冒出来的那一条提示的正文；没有新气泡时为空
            [[nodiscard]] static QString newToastText(const std::vector<QWidget *> &before)
            {
                for (auto *bubble: visibleToastBubbles())
                {
                    if (std::ranges::find(before, bubble) != before.end())
                    {
                        continue;
                    }
                    for (auto *label: bubble->findChildren<QLabel *>())
                    {
                        if (label->accessibleName() != GyanisScribe::Base::uiText("提示图标"))
                        {
                            return label->text();
                        }
                    }
                }
                return {};
            }

            /// 此刻可见气泡里那一条提示的正文：气泡会复用同一扇窗，只能读当前文本
            [[nodiscard]] static QString visibleToastText()
            {
                for (auto *bubble: visibleToastBubbles())
                {
                    for (auto *label: bubble->findChildren<QLabel *>())
                    {
                        if (label->accessibleName() != GyanisScribe::Base::uiText("提示图标"))
                        {
                            return label->text();
                        }
                    }
                }
                return {};
            }

            /// 控制器建出来的那扇主窗口：没有托盘的环境里它会自己现身（离屏平台上托盘装不上）
            [[nodiscard]] static GyanisScribe::Main::MainWindow *mainWindowOnDesk()
            {
                for (auto *widget: QApplication::topLevelWidgets())
                {
                    if (auto *window = qobject_cast<GyanisScribe::Main::MainWindow *>(widget))
                    {
                        return window;
                    }
                }
                return nullptr;
            }

            /// 这一轮铺出来的框选遮罩：顶层窗里按类型认可见的那一张（收掉的旧遮罩还在等 deleteLater）
            [[nodiscard]] static GyanisScribe::Gui::CaptureOverlay *visibleCaptureOverlay()
            {
                for (auto *widget: QApplication::topLevelWidgets())
                {
                    if (auto *overlay = qobject_cast<GyanisScribe::Gui::CaptureOverlay *>(widget); overlay != nullptr && overlay->isVisible())
                    {
                        return overlay;
                    }
                }
                return nullptr;
            }

            /// 动作条上文案是这一句的那个按钮：那些按钮与托盘菜单同一张表，文案从表里取
            [[nodiscard]] static QPushButton *actionBarButton(QWidget &window, const QString &label)
            {
                for (auto *button: window.findChildren<QPushButton *>())
                {
                    if (button->text() == label)
                    {
                        return button;
                    }
                }
                return nullptr;
            }

            /// 读回注入的那份历史清单；条目里指向的成品文件由 load() 自己检查存在性
            [[nodiscard]] Base::Expected<QVector<Core::HistoryEntry>> readHistory() const
            {
                const Core::HistoryStore store(m_root / "history.json");
                const auto               loaded = store.load(Core::policyFromSettings(*m_settings), QDateTime::currentDateTime());
                if (!loaded)
                {
                    return Base::Expected<QVector<Core::HistoryEntry>>{std::unexpected{loaded.error()}};
                }
                return loaded->entries;
            }

            /// 钉住成品落在哪个目录：拿两边的等价文件系统状态判，不抄字符串比前缀
            static void expectUnderDirectory(const std::filesystem::path &file, const std::filesystem::path &directory)
            {
                EXPECT_TRUE(std::filesystem::exists(file)) << "记录指着的成品文件不在盘上：" << file.string();
                std::error_code same;
                const bool      equivalent = std::filesystem::equivalent(file.parent_path(), directory, same);
                EXPECT_FALSE(same) << same.message();
                EXPECT_TRUE(equivalent) << "成品没落在判据点名的目录里：" << file.string();
            }

            /// 等一个条件成立，最长等 budgetMs：延时那一档用的是真定时器，只能转着事件循环真等
            [[nodiscard]] static bool waitUntil(const std::function<bool()> &condition, const int budgetMs = 5000)
            {
                QElapsedTimer clock;
                clock.start();
                while (!condition() && clock.elapsed() < budgetMs)
                {
                    QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
                    std::this_thread::sleep_for(std::chrono::milliseconds(5));
                }
                return condition();
            }

            /// 把事件循环空转这段时间：用来判「这段时间里不该发生的事确实没发生」
            static void pumpFor(const int milliseconds)
            {
                QElapsedTimer clock;
                clock.start();
                while (clock.elapsed() < milliseconds)
                {
                    QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
                    std::this_thread::sleep_for(std::chrono::milliseconds(5));
                }
            }

            std::filesystem::path                m_root;                                                    ///< 这一格用例自己的临时目录
            Core::SettingsStore                 *m_settings    = nullptr;                                   ///< 被控制器接管的配置存储（读回历史策略用）
            CountingScreenSource                *m_source      = nullptr;                                   ///< 被抓帧服务接管的替身
            ForegroundWindowStub                *m_integration = nullptr;                                   ///< 被平台那一侧接管的替身
            std::unique_ptr<Main::AppController> m_controller;                                              ///< 被测对象
            QString                              m_realScreenName;                                          ///< 离屏平台那块真屏的名字
            int                                  m_delaySeconds    = 0;                                     ///< 延时截图那一格
            QString                              m_frameScreenName = QString::fromLatin1(kGhostScreenName); ///< 假帧带的屏名
            QColor                               m_frameColor      = QColor(36, 87, 166);                  ///< 假帧铺的底色
            QRect                                m_frameRegion;                                             ///< 假帧记的屏内选区，F5 重抓按它走
            std::optional<App::CaptureIntent>    m_afterAction;                                             ///< 「截完之后」那一格；空 = 不写
            bool                                 m_keepUnsaved   = false;                                   ///< 「未保存也进历史」那一格
            bool                                 m_autoSave      = false;                                   ///< 「自动保存」那一格
            bool                                 m_seedPinLayout = false;                                   ///< 造控制器前先写一份贴图布局
            std::optional<std::filesystem::path> m_recordingDirectory;                                      ///< 录屏成品目录；非空就写 kPathRecording（只有录屏那一组设它）
            std::optional<int>                   m_recordingCountdown;                                      ///< 录屏倒计时秒数；不设就按生产默认（3 秒）走
            QString                              m_customHotkeyText;                                        ///< 预置的自定义绑定；空 = 不写这一格
            bool                                 m_rewindEnabled        = false;                            ///< 回溯开关；真就在造控制器前写进设置
            bool                                 m_sessionNotifications = false;                            ///< 平台是否承诺锁屏通知；回溯协调器只在它为真时启用
        };
    } // namespace

    /**
     * @brief 钉住「父对象不会悄悄变成 restorePins」
     * @details 构造首参从 parent 换成 bool 之后，AppController(parent) 会把非空指针读成 true、
     *          空指针读成 false；两个删除的重载让这种写法在编译期就报错，常驻装配仍走默认参数。
     */
    TEST(AppControllerConstruction, ParentPointerCannotLandInRestorePins)
    {
        EXPECT_FALSE((std::is_constructible_v<Main::AppController, QObject *>)) << "parent 指针还能落进 restorePins";
        EXPECT_FALSE((std::is_constructible_v<Main::AppController, Main::AppControllerParts, QObject *>)) << "parts 档的 parent 还能落进 restorePins";
        EXPECT_TRUE((std::is_constructible_v<Main::AppController, bool>)) << "按档位构造的那一条被删重载挡住了";
        EXPECT_TRUE(std::is_default_constructible_v<Main::AppController>) << "默认的常驻装配被挡住了";
    }

    /**
     * @brief 钉住「0 秒就是不延时」：按下那一刻帧已经抓完，不等任何一次事件循环
     * @details 「0 秒」若走成「定时器 0 毫秒」也还是异步的，调用返回时这条就不成立了；
     *          把「就地开抓」与「排个定时器」分开，是这一档存在的全部意义。
     */
    TEST_F(AppControllerFixture, ZeroDelayStartsTheCaptureRightAway)
    {
        makeController();
        m_controller->capture().startFullScreen(App::CaptureIntent::CopyToClipboard);

        EXPECT_EQ(m_source->screenCalls, 1) << "0 秒档该在按下那一刻抓完";
        EXPECT_EQ(m_source->windowCalls, 0);
    }

    /**
     * @brief 钉住截图流程收场：取消报一次，标注窗关报一次，重复关闭不再报
     * @details 回溯协调器靠 captureFlowFinished 恢复抓帧：标注窗还开着就报收场，缓冲会在用户标注时又抓起来；
     *          同一轮报两次，恢复动作会重复执行。判据走真遮罩、真标注窗与真信号，不碰内部标志。
     */
    TEST_F(AppControllerFixture, CaptureFlowFinishedIsReportedOncePerFlow)
    {
        m_frameScreenName = m_realScreenName;
        m_frameRegion     = QRect{0, 0, kFrameWidth, kFrameHeight};
        makeController();

        int   finished = 0;
        auto &capture  = m_controller->capture();
        QObject::connect(&capture, &App::CaptureCoordinator::captureFlowFinished, &capture, [&finished] { ++finished; });

        // 第一轮：取消框选，收场一次
        capture.startInteractive(App::CaptureIntent::Annotate);
        auto *overlay = visibleCaptureOverlay();
        ASSERT_NE(overlay, nullptr) << "开始框选之后没铺出遮罩";
        QKeyEvent escape{QEvent::KeyPress, Qt::Key_Escape, Qt::KeyboardModifiers{}};
        QApplication::sendEvent(overlay, &escape);
        EXPECT_EQ(finished, 1) << "取消框选没报收场，回溯会停在 CaptureFlow";

        // 第二轮：框一块进标注窗；关窗前不报，关窗报一次，再关不再报
        capture.startInteractive(App::CaptureIntent::Annotate);
        overlay = visibleCaptureOverlay();
        ASSERT_NE(overlay, nullptr) << "第二轮没铺出遮罩";
        const auto global = overlay->geometry().topLeft();
        QMouseEvent press{QEvent::MouseButtonPress, QPointF{5, 5}, QPointF{global + QPoint{5, 5}}, Qt::LeftButton, Qt::LeftButton, Qt::KeyboardModifiers{}};
        QApplication::sendEvent(overlay, &press);
        QMouseEvent move{QEvent::MouseMove, QPointF{14, 14}, QPointF{global + QPoint{14, 14}}, Qt::NoButton, Qt::LeftButton, Qt::KeyboardModifiers{}};
        QApplication::sendEvent(overlay, &move);
        QMouseEvent release{QEvent::MouseButtonRelease, QPointF{14, 14}, QPointF{global + QPoint{14, 14}}, Qt::LeftButton, Qt::NoButton, Qt::KeyboardModifiers{}};
        QApplication::sendEvent(overlay, &release);

        auto &window = capture.annotationWindow();
        ASSERT_TRUE(window.isVisible()) << "标注那一档没把窗开起来";
        EXPECT_EQ(finished, 1) << "标注窗还开着就报了收场，回溯会在用户标注时恢复抓帧";

        QApplication::sendEvent(&window, &escape);
        EXPECT_EQ(finished, 2) << "标注窗关了却没报收场，回溯会一直停在 CaptureFlow";
        QApplication::sendEvent(&window, &escape);
        EXPECT_EQ(finished, 2) << "同一轮流程报了两次收场，恢复动作会重复执行";
    }

    /**
     * @brief 钉住回溯协调器由控制器持有并启动，且热键分支的门禁读的是截图流程标志
     * @details 热键那一跳的触发口在控制器内部，本装置发不出一次 HotkeyAction::Rewind：
     *          这一格钉的是它能碰到的三段——协调器确实由控制器构造（访问器拿得到、默认关闭时
     *          停在 Disabled 并按关闭档回话）、开关与锁屏能力在构造前摆好后 start() 能把缓冲带起来、
     *          以及热键分支用来挡人的 capture().isCaptureFlowActive() 在框选起落之间翻转。
     *          剩下的「热键真的调到 showPicker」由编译期接线保证，触发口留给热键层。
     */
    TEST_F(AppControllerFixture, RewindHotkeyPathIsWiredAndGated)
    {
        makeController();
        auto &rewind = m_controller->rewindCoordinatorForTesting();
        EXPECT_EQ(rewind.statusForTesting(), Core::RewindStatus::Disabled) << "默认关闭时回溯协调器不该抓帧：start() 没认设置";

        // 关闭档按回溯：协调器把「去设置开启」说给用户，而不是开一个空选择器
        const auto before = visibleToastBubbles();
        rewind.showPicker();
        EXPECT_TRUE(waitUntil([&] { return newToastText(before).contains(QStringLiteral("回溯截图未开启")); }))
                << "关闭档的 showPicker 没把开启路径说给用户：控制器拿到的不是真协调器";

        // 热键分支的门禁：框选起头时读过为真，取消收场后复位
        auto &capture = m_controller->capture();
        EXPECT_FALSE(capture.isCaptureFlowActive()) << "还没起截图，流程标志就不是清的";
        capture.startInteractive(App::CaptureIntent::Annotate);
        auto *overlay = visibleCaptureOverlay();
        ASSERT_NE(overlay, nullptr) << "框选没铺出遮罩，流程门禁无从判起";
        EXPECT_TRUE(capture.isCaptureFlowActive()) << "框选在跑时流程标志是假的：回溯热键会叠一层遮罩";
        QKeyEvent escape{QEvent::KeyPress, Qt::Key_Escape, Qt::KeyboardModifiers{}};
        QApplication::sendEvent(overlay, &escape);
        EXPECT_FALSE(capture.isCaptureFlowActive()) << "取消之后流程标志没复位：热键会一直挡人";

        // 开关与能力位在构造前摆好：控制器里的 start() 该把缓冲带起来
        m_controller.reset();
        m_rewindEnabled        = true;
        m_sessionNotifications = true;
        makeController();
        EXPECT_EQ(m_controller->rewindCoordinatorForTesting().statusForTesting(), Core::RewindStatus::Buffering) << "开启档下控制器没把回溯缓冲启动起来：start() 没接进构造";
        EXPECT_TRUE(m_controller->rewindCoordinatorForTesting().configForTesting().enabled) << "协调器读到的配置没跟着设置走";
    }

    /**
     * @brief 钉住整屏截图留下的标注窗也挡住回溯热键
     * @details 整屏、上次选区这些入口不开 captureFlowActive，默认动作却是打开标注窗：
     *          只看流程标志的门禁会把选择器叠到用户正在标注的那一窗上，缓冲随后把选择器自己抓进下一帧。
     *          这一格走热键分派的同一个出口：先开整屏标注再按 Rewind，选择器不许出现、提示要是那一句；
     *          关窗后再按一次，选择器该正常打开，证明上面那条不是被「选择器根本打不开」兜住的假绿。
     */
    TEST_F(AppControllerFixture, RewindHotkeyIsBlockedByAVisibleAnnotationWindow)
    {
        m_frameScreenName      = m_realScreenName;
        m_rewindEnabled        = true;
        m_sessionNotifications = true;
        makeController();

        auto &capture = m_controller->capture();
        auto &window  = capture.annotationWindow();
        capture.startFullScreen(App::CaptureIntent::Annotate);
        ASSERT_TRUE(window.isVisible()) << "整屏截图该把标注窗开起来";
        ASSERT_FALSE(capture.isCaptureFlowActive()) << "整屏这一路本来就不立流程标志：本用例钉的正是标志之外的那一半";

        // 缓冲里先有帧：门禁若漏了，选择器当场就有东西可选，不会因为空缓冲躲过去
        auto &rewind = m_controller->rewindCoordinatorForTesting();
        rewind.captureTickForTesting();
        ASSERT_TRUE(waitUntil([&rewind] { return rewind.bufferStatsForTesting().sampleCount > 0; }, 5000))
                << "回溯缓冲没抓到帧：后面的判据分不清是被门禁挡下还是没帧可开";

        const auto before = visibleToastBubbles();
        m_controller->handleHotkeyActionForTesting(Core::HotkeyAction::Rewind);
        EXPECT_TRUE(waitUntil([&] { return newToastText(before).contains(QStringLiteral("先完成当前截图或标注")); }))
                << "标注窗开着时回溯热键没被挡回，也没说清出路";
        pumpFor(400); // 编码在途时选择器会等一小会儿再开：给漏掉的门禁留出这一段
        EXPECT_EQ(countTopLevelWindows<Gui::RewindPickerWindow>(), 0) << "标注窗还开着就叠上了回溯选择器";

        // 关窗之后再按：同一个出口该把选择器打开，本用例的前提与接线都还活着
        QKeyEvent escape{QEvent::KeyPress, Qt::Key_Escape, Qt::KeyboardModifiers{}};
        QApplication::sendEvent(&window, &escape);
        ASSERT_FALSE(window.isVisible()) << "标注窗没关掉，后一条判据无从判起";
        m_controller->handleHotkeyActionForTesting(Core::HotkeyAction::Rewind);
        EXPECT_TRUE(waitUntil([] { return countTopLevelWindows<Gui::RewindPickerWindow>() > 0; }, 3000))
                << "标注窗关掉之后回溯热键仍打不开选择器：这条出口本身没接到协调器";
    }

    /**
     * @brief 钉住延时排不进去的那一次不会把截图流程标志留在 true
     * @details 标志若在 schedule 之前就立起来，第二次按键被「上一次延时截图还在等」回绝时，
     *          这一轮根本没有流程，却会一直把回溯热键挡回去。标志跟着到点那一步立，
     *          本格按两次、等到点、再取消，把「被回绝」与「真跑起来」两头都判住。
     */
    TEST_F(AppControllerFixture, ScheduleBailDoesNotLeaveTheCaptureFlowFlagRaised)
    {
        m_frameScreenName = m_realScreenName;
        m_frameRegion     = QRect{0, 0, kFrameWidth, kFrameHeight};
        m_delaySeconds    = 1;
        makeController();

        auto &capture = m_controller->capture();
        capture.startInteractive(App::CaptureIntent::Annotate);
        EXPECT_FALSE(capture.isCaptureFlowActive()) << "延时还没到就立了流程标志：被回绝的第二次按键会把它永久留在 true";

        capture.startLong(); // 上一枚定时器还在等：这一次该被回绝
        EXPECT_FALSE(capture.isCaptureFlowActive()) << "被回绝的那一次把流程标志留在了 true：回溯热键会一直被挡";

        ASSERT_TRUE(waitUntil([] { return visibleCaptureOverlay() != nullptr; }, 5000)) << "延时到点了也没开始框选";
        EXPECT_TRUE(capture.isCaptureFlowActive()) << "到点真开始框选时流程标志没立起来";

        QKeyEvent escape{QEvent::KeyPress, Qt::Key_Escape, Qt::KeyboardModifiers{}};
        QApplication::sendEvent(visibleCaptureOverlay(), &escape);
        EXPECT_FALSE(capture.isCaptureFlowActive()) << "取消之后流程标志没复位";
    }

    /**
     * @brief 钉住延时截图倒计时期间回溯热键也被挡回
     * @details 延时还没到点时框选流程标志仍是 false，只看流程标志的门禁会把选择器叠到
     *          几秒后铺出来的遮罩上。这一格先让缓冲里有帧（门禁若漏了，选择器当场打得开），
     *          再起一轮延时截图并按回溯热键：提示要是那一句、选择器不许出现；
     *          到点铺出遮罩后流程标志接手，取消收场，证明延时谓词不是永久为真。
     */
    TEST_F(AppControllerFixture, RewindHotkeyIsBlockedWhileCaptureDelayPending)
    {
        m_frameScreenName      = m_realScreenName;
        m_frameRegion          = QRect{0, 0, kFrameWidth, kFrameHeight};
        m_delaySeconds         = 1;
        m_rewindEnabled        = true;
        m_sessionNotifications = true;
        makeController();

        auto &capture = m_controller->capture();
        EXPECT_FALSE(capture.isDelayPending()) << "还没起延时截图，延时谓词就不是清的";

        auto &rewind = m_controller->rewindCoordinatorForTesting();
        rewind.captureTickForTesting();
        ASSERT_TRUE(waitUntil([&rewind] { return rewind.bufferStatsForTesting().sampleCount > 0; }, 5000))
                << "回溯缓冲没抓到帧：后面的判据分不清是被门禁挡下还是没帧可开";

        capture.startInteractive(App::CaptureIntent::Annotate);
        EXPECT_TRUE(capture.isDelayPending()) << "延时已经排上，谓词却没立起来";
        EXPECT_FALSE(capture.isCaptureFlowActive()) << "本用例钉的正是流程标志之外的那一段";

        m_controller->handleHotkeyActionForTesting(Core::HotkeyAction::Rewind);
        EXPECT_TRUE(waitUntil([&] { return visibleToastText().contains(QStringLiteral("先完成当前截图或标注")); }))
                << "延时倒计时里按回溯热键没被挡回，也没说清出路";
        pumpFor(200); // 编码在途时选择器会等一小会儿再开：给漏掉的门禁留出这一段
        EXPECT_EQ(countTopLevelWindows<Gui::RewindPickerWindow>(), 0) << "延时截图还没到点就叠上了回溯选择器";

        ASSERT_TRUE(waitUntil([] { return visibleCaptureOverlay() != nullptr; }, 5000)) << "延时到点了也没开始框选";
        EXPECT_FALSE(capture.isDelayPending()) << "到点开始抓帧后延时谓词还挂着：热键会被一直挡住";
        EXPECT_TRUE(capture.isCaptureFlowActive()) << "到点真开始框选时流程标志没立起来";

        QKeyEvent escape{QEvent::KeyPress, Qt::Key_Escape, Qt::KeyboardModifiers{}};
        QApplication::sendEvent(visibleCaptureOverlay(), &escape);
        EXPECT_FALSE(capture.isCaptureFlowActive()) << "取消之后流程标志没复位";
    }

    /**
     * @brief 钉住回车复制这一交付出口也结束回溯流程：收场一次，缓冲从暂停回到抓帧
     * @details 标注窗的四个交付出口会收起窗口但不走 closeAnnotation；少发 closed() 时
     *          captureFlowFinished 就永远不来，回溯缓冲停在 CaptureFlow，之后的热键都被
     *          「先完成当前截图」挡回。这一格用的是控制器亲手建的那份回溯协调器：
     *          开关与锁屏能力都在构造前摆好，构造函数里的 start() 就该把缓冲带起来；
     *          四根线也全由控制器接，用例只保留观测。
     */
    TEST_F(AppControllerFixture, EnterCopyDeliveryEndsTheRewindCaptureFlow)
    {
        auto *screen = QGuiApplication::primaryScreen();
        ASSERT_NE(screen, nullptr) << "离屏平台该有一块屏";

        m_frameScreenName = screen->name();
        m_frameRegion     = screen->geometry();
        m_afterAction     = App::CaptureIntent::Annotate;
        // 回溯协调器只在平台承诺锁屏通知且设置为开启时启用：两位都要在构造控制器之前摆好
        m_rewindEnabled        = true;
        m_sessionNotifications = true;
        makeController();

        // 回溯抓整屏：选帧后的布局校验要求帧区域与当前屏几何一致，物理尺寸也要自洽
        Base::CapturedImage full = makeFrame(screen->name(), screen->geometry());
        full.logicalSize         = screen->geometry().size();
        full.devicePixelRatio    = screen->devicePixelRatio();
        full.image               = QImage(QSize(qRound(full.logicalSize.width() * full.devicePixelRatio), qRound(full.logicalSize.height() * full.devicePixelRatio)),
                                          QImage::Format_ARGB32_Premultiplied);
        full.image.fill(m_frameColor);
        m_source->setFrame(full);

        auto &rewind = m_controller->rewindCoordinatorForTesting();
        // 控制器构造里调过 start()：这一句同时判「调了」与「认了开启那一档」
        ASSERT_EQ(rewind.statusForTesting(), Core::RewindStatus::Buffering) << "控制器没按开启设置启动回溯缓冲，后面的流程无从判起";

        bool lastPaused = false;
        QObject::connect(&rewind, &App::RewindCoordinator::bufferStatusChanged, &rewind,
                         [&lastPaused](bool enabled, bool paused, const QString &) { lastPaused = enabled && paused; });

        auto &capture  = m_controller->capture();
        int   finished = 0;
        QObject::connect(&capture, &App::CaptureCoordinator::captureFlowFinished, &capture, [&finished] { ++finished; });

        rewind.captureTickForTesting();
        ASSERT_TRUE(waitUntil([&rewind] { return rewind.bufferStatsForTesting().sampleCount > 0; }, 5000)) << "回溯缓冲没抓到帧";

        const auto id = rewind.firstFrameIdForTesting();
        ASSERT_TRUE(id.has_value());
        rewind.chooseFrameForTesting(*id);
        ASSERT_TRUE(waitUntil([] { return visibleCaptureOverlay() != nullptr; }, 3000)) << "选帧之后没铺出框选遮罩";

        auto *overlay = visibleCaptureOverlay();
        ASSERT_NE(overlay, nullptr);
        const auto global = overlay->geometry().topLeft();
        QMouseEvent press{QEvent::MouseButtonPress, QPointF{10, 10}, QPointF{global + QPoint{10, 10}}, Qt::LeftButton, Qt::LeftButton, Qt::KeyboardModifiers{}};
        QApplication::sendEvent(overlay, &press);
        QMouseEvent move{QEvent::MouseMove, QPointF{109, 89}, QPointF{global + QPoint{109, 89}}, Qt::NoButton, Qt::LeftButton, Qt::KeyboardModifiers{}};
        QApplication::sendEvent(overlay, &move);
        QMouseEvent release{QEvent::MouseButtonRelease, QPointF{109, 89}, QPointF{global + QPoint{109, 89}}, Qt::LeftButton, Qt::NoButton, Qt::KeyboardModifiers{}};
        QApplication::sendEvent(overlay, &release);

        ASSERT_TRUE(waitUntil([this] { return m_controller->capture().annotationWindow().isVisible(); }, 3000)) << "框选之后标注窗没开起来";
        EXPECT_TRUE(lastPaused) << "标注窗开着时回溯缓冲该是暂停的，后面的恢复判据才有对照";

        auto &window = capture.annotationWindow();
        QKeyEvent enter{QEvent::KeyPress, Qt::Key_Return, Qt::KeyboardModifiers{}};
        QApplication::sendEvent(&window, &enter);

        EXPECT_EQ(finished, 1) << "回车复制没报收场：回溯协调器会一直停在 CaptureFlow";
        EXPECT_FALSE(window.isVisible()) << "回车复制之后标注窗还留在屏上";
        EXPECT_FALSE(lastPaused) << "收场之后回溯缓冲没恢复抓帧";

        QKeyEvent escape{QEvent::KeyPress, Qt::Key_Escape, Qt::KeyboardModifiers{}};
        QApplication::sendEvent(&window, &escape);
        EXPECT_EQ(finished, 1) << "同一轮流程报了两次收场，恢复动作会重复执行";
    }

    /**
     * @brief 钉住标注窗拖边框之后按新范围重抓
     * @details 窗口只报新几何；藏窗、重抓、换画布与更新选区都在协调器这一层。
     */
    TEST_F(AppControllerFixture, CroppingTheAnnotationWindowRegrabsTheNewRegion)
    {
        makeController();
        auto *screen = QGuiApplication::primaryScreen();
        ASSERT_NE(screen, nullptr);

        auto       &window = m_controller->capture().annotationWindow();
        const auto  before = m_source->screenCalls;
        const QRect region{5, 5, 40, 30};
        window.cropRequested(screen->name(), region);

        EXPECT_EQ(m_source->screenCalls, before + 1) << "拖完边框没有按新范围重抓";
        EXPECT_EQ(m_source->requestedScreen, screen->name()) << "重抓跑到别的屏去了";
        EXPECT_TRUE(window.isVisible()) << "重抓之后标注窗没有重新出现";
        EXPECT_EQ(window.geometry(), region) << "标注窗没有按新范围摆好";
    }

    /**
     * @brief 钉住 F5 换上新帧之后，拖边框重裁裁的是新帧而不是刷新之前那一张
     * @details 刷新只换画布不换记录时，冻帧还停在 F5 之前：下一回拖边框走冻帧那条重裁路，
     *          裁出来的画面就是刷新之前那一张，用户按了 F5 却看到旧内容。
     *          这一格先红后绿，重裁之后按新范围同时判颜色与几何。
     */
    TEST_F(AppControllerFixture, RefreshThenCropUsesTheRefreshedFrame)
    {
        m_frameScreenName = m_realScreenName;
        m_frameColor      = QColor(220, 0, 0);
        makeController();

        auto *screen = QGuiApplication::primaryScreen();
        ASSERT_NE(screen, nullptr);
        const QPoint origin   = screen->geometry().topLeft();
        const QRect  region   = QRect{0, 0, kFrameWidth, kFrameHeight};
        const QColor red      = QColor(220, 0, 0);
        const QColor green    = QColor(0, 200, 0);
        Core::rememberRegion(*m_settings, Core::RegionHistoryEntry{region, screen->name()});

        auto &window = m_controller->capture().annotationWindow();
        m_controller->capture().startLastRegion(App::CaptureIntent::Annotate);
        ASSERT_TRUE(window.isVisible()) << "标注窗没开起来，后面的判据无从谈起";
        ASSERT_EQ(window.canvas().image.pixelColor(kFrameWidth / 2, kFrameHeight / 2), red) << "起始帧不是那一张红的";

        m_source->setFrame(makeFrame(m_frameScreenName, region, green));
        window.refreshRequested(screen->name(), region);
        EXPECT_EQ(window.canvas().image.pixelColor(kFrameWidth / 2, kFrameHeight / 2), green) << "F5 之后画布没换成新帧";

        const QRect newLocal    = QRect{4, 4, 20, 16};
        const auto  callsBefore = m_source->screenCalls;
        window.cropRequested(screen->name(), newLocal.translated(origin));

        EXPECT_EQ(m_source->screenCalls, callsBefore) << "带冻帧的那一轮重裁不该再抓一次屏";
        EXPECT_EQ(window.canvas().logicalSize, newLocal.size()) << "重裁之后画布不是新范围";
        EXPECT_EQ(window.canvas().image.pixelColor(newLocal.width() / 2, newLocal.height() / 2), green) << "重裁裁出来的是 F5 之前那一张";
        EXPECT_EQ(window.geometry(), newLocal.translated(origin)) << "标注窗没有按新范围摆好";
    }

    /**
     * @brief 钉住 F5 重抓那一刻标注窗与工具栏都不在屏上，抓完两样都回来
     * @details 窗盖着的正是要重抓的那一块：不藏就把它自己拍进新底图，按住 F5 会一层层叠进去。
     *          判据在抓帧那一刻读可见态——抓完之后的恢复另占两条断言，两半缺一都算没做成。
     */
    TEST_F(AppControllerFixture, RefreshHidesAnnotationWindowWhileGrabbing)
    {
        m_frameScreenName = m_realScreenName;
        m_frameRegion     = QRect{0, 0, kFrameWidth, kFrameHeight};
        makeController();

        auto *screen = QGuiApplication::primaryScreen();
        ASSERT_NE(screen, nullptr);
        auto &window = m_controller->capture().annotationWindow();
        m_controller->capture().startFullScreen(App::CaptureIntent::Annotate);
        ASSERT_TRUE(window.isVisible()) << "标注窗没开起来，后面的判据无从谈起";
        ASSERT_TRUE(window.toolbar().isVisible()) << "工具栏没跟着摆出来，后面的判据无从谈起";

        bool sawWindowHidden   = false;
        bool sawToolbarHidden  = false;
        m_source->onGrabScreen = [&]
        {
            sawWindowHidden  = !window.isVisible();
            sawToolbarHidden = !window.toolbar().isVisible();
        };

        const auto callsBefore = m_source->screenCalls;
        window.refreshRequested(screen->name(), m_frameRegion);

        EXPECT_EQ(m_source->screenCalls, callsBefore + 1) << "F5 没有重抓这一块";
        EXPECT_TRUE(sawWindowHidden) << "抓帧时标注窗还在屏上：新底图会把它自己拍进去";
        EXPECT_TRUE(sawToolbarHidden) << "抓帧时工具栏还在屏上：新底图会把它自己拍进去";
        EXPECT_TRUE(window.isVisible()) << "抓完没把标注窗放回来";
        EXPECT_TRUE(window.toolbar().isVisible()) << "抓完没把工具栏放回来";
    }

    /**
     * @brief 钉住 F5 抓帧失败时标注窗与工具栏照原样留在原地，原因说给用户
     * @details 抓帧失败时画布与标注都不许动，窗口也不许因为藏过一眼就消失或换位置；
     *          原因要沿提示那条路交出去，而不是静默留在日志里。
     */
    TEST_F(AppControllerFixture, RefreshFailureRestoresTheAnnotationWindow)
    {
        m_frameScreenName = m_realScreenName;
        m_frameRegion     = QRect{0, 0, kFrameWidth, kFrameHeight};
        makeController();

        auto *screen = QGuiApplication::primaryScreen();
        ASSERT_NE(screen, nullptr);
        auto &window = m_controller->capture().annotationWindow();
        m_controller->capture().startFullScreen(App::CaptureIntent::Annotate);
        ASSERT_TRUE(window.isVisible()) << "标注窗没开起来，后面的判据无从谈起";
        ASSERT_TRUE(window.toolbar().isVisible()) << "工具栏没跟着摆出来，后面的判据无从谈起";
        const auto geometry = window.geometry();

        int     messages = 0;
        QString said;
        QObject::connect(&m_controller->capture(), &GyanisScribe::App::CaptureCoordinator::messageForUser, &m_controller->capture(),
                         [&messages, &said](const QString &text, GyanisScribe::Gui::ToastKind)
                         {
                             ++messages;
                             said = text;
                         });

        m_source->failNextScreenGrab(Base::Error{Base::ErrorCode::ScreenUnavailable, "替身：这一帧没抓到"});
        window.refreshRequested(screen->name(), m_frameRegion);

        EXPECT_TRUE(window.isVisible()) << "F5 抓失败后标注窗消失了";
        EXPECT_TRUE(window.toolbar().isVisible()) << "F5 抓失败后工具栏没回来";
        EXPECT_EQ(window.geometry(), geometry) << "F5 抓失败后标注窗换了位置";
        EXPECT_EQ(messages, 1) << "F5 抓失败的原因没有说给用户";
        EXPECT_TRUE(said.contains(QStringLiteral("这一帧没抓到"))) << said.toStdString();
    }

    /**
     * @brief 钉住「设了几秒就等几秒」：到点之前一次都没被抓，到点之后正好一次
     * @details 只判「最后抓到了」会把「立刻抓走」也放过去——而这一档存在的全部意义就是先等；
     *          到点那一半用有界轮询等，等的是真定时器，不是调度运气。
     */
    TEST_F(AppControllerFixture, DelayHoldsTheCaptureUntilTheTimerFires)
    {
        m_delaySeconds = 1;
        makeController();
        m_controller->capture().startFullScreen(App::CaptureIntent::CopyToClipboard);

        EXPECT_EQ(m_source->screenCalls, 0) << "设了 1 秒延时，按下那一刻不该已经抓完";
        ASSERT_TRUE(waitUntil([this] { return m_source->screenCalls >= 1; })) << "到点了还没抓";
        EXPECT_EQ(m_source->screenCalls, 1) << "一次按下只该抓一帧";
    }

    /**
     * @brief 钉住「截取当前窗口走同一条延时通道，且到点那一刻才取前台窗口」
     * @details 这一格漏过一次：前台窗口在按下那一刻就取走、截图也立刻开抓，延时被绕开；
     *          等的那几秒本是用来把菜单摆出来的，取早了取到的就是错的窗口。
     *          两条判据分开钉：句柄查询与抓帧，各自「按下为零、到点为一」。
     */
    TEST_F(AppControllerFixture, ForegroundWindowIsFetchedOnlyWhenTheTimerFires)
    {
        m_delaySeconds = 1;
        makeController();
        m_controller->capture().captureForegroundWindow();

        EXPECT_EQ(m_integration->foregroundQueries, 0) << "按下那一刻就取前台窗口＝绕过延时";
        EXPECT_EQ(m_source->windowCalls, 0) << "延时没到就开抓窗口";

        ASSERT_TRUE(waitUntil([this] { return m_source->windowCalls >= 1; })) << "到点了还没抓窗口";
        EXPECT_EQ(m_integration->foregroundQueries, 1) << "到点该取一次前台窗口";
        EXPECT_EQ(m_source->windowCalls, 1) << "一次按下只该抓一帧";
        EXPECT_EQ(m_source->requestedWindow, kForegroundWindowHandle) << "抓的该是那一刻取到的那枚句柄";
    }

    /**
     * @brief 钉住「在等的时候再按一次」：不排第二个、也不把在等的那一次顶掉
     * @details 两种坏形状各挡一半：偷偷排进第二枚定时器会把两次都做出来；后按的顶掉先按的
     *          会让用户等到的动作不是自己第一次按下的那个。两次按的是不同入口，按「哪一路被抓了」分账。
     */
    TEST_F(AppControllerFixture, SecondPressWhileWaitingKeepsTheFirstAction)
    {
        m_delaySeconds = 1;
        makeController();
        m_controller->capture().startFullScreen(App::CaptureIntent::CopyToClipboard);
        m_controller->capture().captureForegroundWindow(); // 顶上再按一次：该被回绝

        ASSERT_TRUE(waitUntil([this] { return m_source->screenCalls > 0 || m_source->windowCalls > 0; })) << "先按的那一次都没跑起来";
        pumpFor(700); // 若第二下真排了枚定时器，这一档够它到点
        EXPECT_EQ(m_source->screenCalls, 1) << "先按的那一次该照常跑";
        EXPECT_EQ(m_source->windowCalls, 0) << "后按的那一次既不该排进去、也不该把在等的顶掉";
        EXPECT_EQ(m_integration->foregroundQueries, 0);
    }

    /**
     * @brief 钉住「快速保存：落盘一份 + 历史清单上正好一条」
     * @details 清单必须落在注入的那份上：写进用户真实的清单是测试事故，读自己那份才判得出。
     *          条数是「正好一」而不是「至少一」：重复记账会让同一张图在历史里出现两次。
     */
    TEST_F(AppControllerFixture, QuickSaveLandsTheFileAndOneHistoryEntry)
    {
        m_frameScreenName = m_realScreenName;
        makeController();
        m_controller->capture().startFullScreen(App::CaptureIntent::QuickSave);

        ASSERT_EQ(m_source->screenCalls, 1) << "0 秒档该在按下时就抓完";

        const auto entries = readHistory();
        ASSERT_TRUE(entries.has_value()) << "历史清单读不回：" << entries.error().message;
        ASSERT_EQ(entries->size(), 1) << "快速保存该正好记一条";
        EXPECT_EQ(entries->front().kind, Core::HistoryKind::Screenshot);
        EXPECT_EQ(entries->front().size, QSize(kFrameWidth, kFrameHeight)) << "记的该是落盘那份的像素尺寸";
        expectUnderDirectory(entries->front().filePath, m_root / "shots");
    }

    /**
     * @brief 钉住「开着『未保存也进历史』时，没落盘的截图归档进历史」
     * @details 归档落点是未保存目录（不是截图目录）：这一档写的就是「没存文件的那一张」，
     *          落错目录等于把它当成了存过的那一类。
     */
    TEST_F(AppControllerFixture, UnsavedCaptureEntersHistoryWhenTheSwitchIsOn)
    {
        m_frameScreenName = m_realScreenName;
        m_keepUnsaved     = true;
        makeController();
        m_controller->capture().startFullScreen(App::CaptureIntent::CopyToClipboard);

        const auto entries = readHistory();
        ASSERT_TRUE(entries.has_value()) << "历史清单读不回：" << entries.error().message;
        ASSERT_EQ(entries->size(), 1) << "开着「未保存也进历史」时该正好记一条";
        EXPECT_EQ(entries->front().kind, Core::HistoryKind::Screenshot);
        expectUnderDirectory(entries->front().filePath, Base::Paths::unsavedCaptureDirectory());
    }

    /**
     * @brief 钉住「自动保存与归档两级都开着时，历史只记一条」
     * @details 自动保存已经把这帧写上盘了，归档那一支该认出「已在盘上」而跳过；
     *          两条都记会让同一张截图在历史里出现两次，用户删一条还剩一条。
     */
    TEST_F(AppControllerFixture, AutoSaveRecordsOnceEvenWithTheArchiveSwitchOn)
    {
        m_frameScreenName = m_realScreenName;
        m_autoSave        = true;
        m_keepUnsaved     = true;
        makeController();
        m_controller->capture().startFullScreen(App::CaptureIntent::CopyToClipboard);

        const auto entries = readHistory();
        ASSERT_TRUE(entries.has_value()) << "历史清单读不回：" << entries.error().message;
        ASSERT_EQ(entries->size(), 1) << "自动保存已经落盘，归档不该再记第二条";
        expectUnderDirectory(entries->front().filePath, m_root / "shots");
    }

    /**
     * @brief 钉住「截取当前窗口也按『截完之后』那一格交出去」
     * @details 这条入口此前不读那一格：交付时用的是上一次截图留下的值，冷启动时是默认的
     *          「进标注窗」。设置点名了快速保存，这里就该落盘并在清单上留一条。
     */
    TEST_F(AppControllerFixture, ForegroundWindowFollowsTheAfterCaptureSetting)
    {
        m_frameScreenName = m_realScreenName;
        m_afterAction     = App::CaptureIntent::QuickSave;
        makeController();
        m_controller->capture().captureForegroundWindow();

        ASSERT_EQ(m_source->windowCalls, 1) << "0 秒档该在按下那一刻抓完";

        const auto entries = readHistory();
        ASSERT_TRUE(entries.has_value()) << "历史清单读不回：" << entries.error().message;
        ASSERT_EQ(entries->size(), 1) << "设置点的是快速保存，这一张却没按它交出去";
        expectUnderDirectory(entries->front().filePath, m_root / "shots");
    }


    /**
     * @brief 钉住托盘的「逐屏截图」真的落到抓帧服务上
     * @details 托盘在场时顶部动作条整条收起，逐屏截图就只剩托盘子菜单这一个入口。
     *          这条接线曾经连在一个还没构造的成员上，QObject::connect 对空接收者不报错：
     *          图标在、菜单在，点下去什么都不会发生。
     */
    TEST_F(AppControllerFixture, TrayScreenCaptureReachesTheCaptureService)
    {
        makeController();

        m_controller->trayControllerForTesting().screenCaptureRequested(QStringLiteral("FakeScreen-1"));

        EXPECT_EQ(m_source->screenCalls, 1) << "托盘的逐屏截图没有走到抓帧服务：那条接线接在空对象上";
        EXPECT_EQ(m_source->requestedScreen, QStringLiteral("FakeScreen-1")) << "抓的不是托盘点名的那块屏";
    }

    /**
     * @brief 钉住「外部命令没起跑时命令行不报交付成功」
     * @details 脚本拿到 delivered 就当这一轮成了；把开不起来的程序名记成一次成功，是最难查的那种假绿。
     *          程序名用本用例临时目录下一个不存在的绝对路径：既不会碰这台机器上的工具，
     *          也不会被 PATH 上恰好同名的程序接住。
     */
    TEST_F(AppControllerFixture, FailedExternalCommandIsNotReportedAsDelivered)
    {
        m_frameScreenName = m_realScreenName;
        makeController();

        QStringList delivered;
        QStringList failures;
        QObject::connect(m_controller.get(), &Main::AppController::commandLineCaptureDelivered, m_controller.get(),
                         [&delivered](const QString &savedPath) { delivered << savedPath; });
        QObject::connect(m_controller.get(), &Main::AppController::commandLineActionFailed, m_controller.get(), [&failures](const QString &reason) { failures << reason; });

        const auto missingProgram = Base::Paths::toDisplayText(m_root / "no-such-command-3f9a.exe");
        App::CommandLineRequest request;
        request.action      = App::CommandLineAction::CaptureFullScreen;
        request.outputs     = App::CaptureOutputSet{App::CaptureOutput::File};
        request.execCommand = QStringLiteral("\"%1\"").arg(missingProgram);
        m_controller->startCommandLineCapture(request);

        EXPECT_TRUE(delivered.isEmpty()) << "外部命令没起跑，命令行却报了交付成功";
        ASSERT_EQ(failures.size(), 1) << "外部命令没起跑，命令行出口一个字都没说";
        EXPECT_TRUE(failures.front().contains(missingProgram)) << failures.front().toStdString();
    }

    /**
     * @brief 钉住「一次性启动不把上次的贴图摆回桌面，也不把它写没」
     * @details 一次性截图也建完整控制器：恢复旧布局等于让 --capture 把上次的贴图截进图里。
     *          布局是借管理器的公开保存路预置的，不是伪造的读数；最后再用默认档建一次，
     *          证明这份布局真的摆得回来，否则「没摆回来」那条判据就是空转。
     */
    TEST_F(AppControllerFixture, TransientStartupDoesNotRestoreTheLastPinLayout)
    {
        m_seedPinLayout = true;
        makeController(false);
        EXPECT_EQ(m_controller->pinCountForTesting(), 0) << "一次性启动把上次的贴图摆上了桌面";

        // 这个进程没接管过桌面上那批贴图：退出时也不许把布局写成空的
        ASSERT_TRUE(QMetaObject::invokeMethod(qApp, "aboutToQuit", Qt::DirectConnection)) << "用例没能触发退出信号";
        const auto kept = Core::loadLayout(*m_settings);
        ASSERT_TRUE(kept.has_value()) << kept.error().message;
        EXPECT_EQ(kept->size(), 1) << "一次性启动退出时把上次的贴图布局抹掉了";

        m_controller.reset();
        // 第二次不再预置：这一份布局必须是上一轮默认档真从盘上摆回来的，否则最后那条判据是空转
        m_seedPinLayout = false;
        makeController();
        EXPECT_EQ(m_controller->pinCountForTesting(), 1) << "预置的布局本来就摆不回来，上一条判据就成了空转";
    }

    /**
     * @brief 钉住「命令行 `--pin`」那条入口在拆族之后照走：桌上要多一张贴图
     * @details 这条链现在是「`Application` → `AppController::pinImageFile`（转发壳）→ `PinCoordinator::pinFile`
     *          → `PinManager` → 一扇贴图窗」。拆族时换过接收者，而件级用例测的是 `PinManager` 自己——
     *          接错线它一声不响，只有从这里走一遍才看得见。
     */
    TEST_F(AppControllerFixture, PinningAFileThroughThePublicEntryPutsAPinOnTheDesk)
    {
        makeController();
        const auto imagePath = m_root / "to-pin.png";
        ASSERT_TRUE(makeFrame(m_realScreenName).image.save(Base::Paths::toDisplayText(imagePath), "PNG")) << "样本图都没写出来";

        const auto before = countTopLevelWindows<GyanisScribe::Gui::PinWindow>();
        const auto pinned = m_controller->pinImageFile(Base::Paths::toDisplayText(imagePath));
        ASSERT_TRUE(pinned.has_value()) << pinned.error().message;
        EXPECT_EQ(countTopLevelWindows<GyanisScribe::Gui::PinWindow>(), before + 1) << "命令行 --pin 那条入口没贴出东西：协调器那条线断了";
    }

    /**
     * @brief 钉住取色入口在拆族之后照走：命令行 `--pick` 与热键都走它
     * @details 这一格钉的是「入口 → 取色协调器 → 一扇遮罩」这条链，中间那次改动换掉了接收者。
     */
    TEST_F(AppControllerFixture, PickingAColorFromTheEntryPointOpensTheOverlay)
    {
        makeController();
        m_frameScreenName = m_realScreenName;
        ASSERT_EQ(countTopLevelWindows<GyanisScribe::Gui::ColorPickerOverlay>(), 0) << "开局就不该有取色遮罩";

        m_controller->startColorPick();
        EXPECT_EQ(countTopLevelWindows<GyanisScribe::Gui::ColorPickerOverlay>(), 1) << "取色入口没铺出遮罩：协调器那条线断了";
    }

    /**
     * @brief 钉住「识别意图截完图之后，图真的交到了识别那一族」
     * @details 这条缝是拆族时新焊的：截图协调器拿着识别协调器的引用，按意图把帧递过去（CaptureCoordinator → OcrCoordinator）。
     *          识别链收到图后必然会开口说话——带真后端的档说「正在识别这一张…」，没后端的档说「没有编入识别后端」，
     *          机器的语言包对不上时会说「没有 zh-Hans」那类失败原因——都落在同一件可观测的事上：
     *          屏幕上多出一扇新的气泡（整条缝断了的话，一扇都不会多）。
     *          结果窗开不开取决于这台机器装没装那门语言，不在这条接缝用例的判据里（那是识别族自己的事，
     *          CI 的 runner 就只装了 en——把它算进来等于让这条用例跟着机器语言包走）。
     */
    TEST_F(AppControllerFixture, CaptureWithRecognizeIntentReachesTheOcrChain)
    {
        m_frameScreenName = m_realScreenName;
        makeController();
        const auto before = visibleToastBubbles();

        m_controller->capture().startFullScreen(App::CaptureIntent::Recognize);
        ASSERT_EQ(m_source->screenCalls, 1) << "0 秒档该在按下时就抓完";

        // 各档文案里都带「识别」或「语言包」两个词之一；拿它把「别的谁弹的提示」挡在外面
        const auto said = [&]
        {
            const QString text = newToastText(before);
            return !text.isEmpty() && (text.contains(QStringLiteral("识别")) || text.contains(QStringLiteral("语言包")));
        };
        EXPECT_TRUE(waitUntil(said)) << "识别意图截完图后识别那一族一句话都没说：截图协调器到识别协调器那条缝断了";
    }

    /**
     * @brief 钉住恢复旧组合写回配置时不会再触发一整轮重登
     * @details 自定义键改动触发重登：新组合被占用后热键层恢复旧组合，控制器把旧组合写回配置；
     *          写回同样会发 valueChanged，若没人压住这一位，就会再重登一轮。
     *          后端整轮清场次数就是「重登了几轮」：一次改动只该多清一次场。
     *          旧组合必须真的回到设置与在册表里，否则「没多登」可能只是新组合根本没失败。
     */
    TEST_F(AppControllerFixture, RestoredCustomBindingDoesNotRestartTheReload)
    {
        const auto customKey = App::customHotkeyKey("CaptureFullScreen");
        m_customHotkeyText   = QStringLiteral("Ctrl+Alt+9");
        makeController();

        const auto initial = m_controller->registerHotkeys();
        ASSERT_TRUE(initial.has_value()) << initial.error().message;
        ASSERT_NE(m_integration->hotkeyBackend, nullptr) << "替身平台没有交出热键后端，这一格无从判起";
        auto &backend = *m_integration->hotkeyBackend;
        ASSERT_EQ(backend.unregisterAllCalls, 1) << "启动登记该只清一次场";
        ASSERT_EQ(backend.registeredCount(), 10) << "九个固定动作加一条自定义键没有全部登记上";

        // 新组合被占用：写入设置即触发整轮重登，热键层恢复旧组合后控制器把它写回配置
        backend.rejectCombination(Qt::ControlModifier | Qt::AltModifier, Qt::Key_8);
        const auto unregisterBefore = backend.unregisterAllCalls;
        ASSERT_TRUE(m_settings->setValue(customKey.toStdString(), QStringLiteral("Ctrl+Alt+8")).has_value());

        EXPECT_EQ(backend.unregisterAllCalls, unregisterBefore + 1) << "改一次自定义键却重登了不止一轮：恢复旧组合的写回又触发了一整轮";
        EXPECT_LE(backend.unregisterAllCalls, 2) << "整轮清场次数越界：重登没有被压住";
        EXPECT_EQ(m_settings->stringValue(customKey.toStdString(), QString{}), QStringLiteral("Ctrl+Alt+9")) << "恢复的旧组合没有写回配置";
        EXPECT_TRUE(backend.isRegistered(Qt::ControlModifier | Qt::AltModifier, Qt::Key_9)) << "旧组合没有重新登记在册";
        EXPECT_FALSE(backend.isRegistered(Qt::ControlModifier | Qt::AltModifier, Qt::Key_8)) << "被占用的新组合不该留在册表里";
    }

    /**
     * @brief 钉住「动作条上的录屏键真的叫到了录屏协调器」
     * @details 这条链是「动作条按钮 → 控制器分派 → 录屏协调器（拼配置、摆控制条、起服务）」，
     *          拆族时换过接收者，接错一格就是「按了没反应」。离屏平台给不出屏名
     *          （`validateRecorderConfig` 会拒录），所以这一格钉的是同一下点击里该发生的三件事：
     *          协调器被叫到、控制条一闪、不能开的原因当面说给用户——接错线时三样一样都不会出现。
     *          真录一轮 → 出成品 → 进历史那条链由 Core/Plugins 的端到端用例覆盖，这里环境上到不了那一步。
     */
    TEST_F(AppControllerFixture, TogglingRecordingFromTheActionBarReachesTheCoordinator)
    {
        m_recordingDirectory = m_root / "videos";
        m_recordingCountdown = 0;
        makeController();

        auto *window = mainWindowOnDesk();
        ASSERT_NE(window, nullptr) << "没有托盘的环境里主窗口该自己现身，这条用例就指着它的动作条";
        const auto recordLabel = [&]
        {
            for (const auto &entry: GyanisScribe::App::visibleTrayMenuEntries(Platform::PlatformCapabilities{}))
            {
                if (entry.action == GyanisScribe::App::TrayAction::ToggleRecord)
                {
                    return entry.label;
                }
            }
            return QString{};
        }();
        ASSERT_FALSE(recordLabel.isEmpty()) << "托盘表里没有录屏这一项，用例的前提就没了";
        auto *recordButton = actionBarButton(*window, recordLabel);
        ASSERT_NE(recordButton, nullptr) << "动作条上没找到「" << recordLabel.toStdString() << "」那颗按钮";

        const auto before = visibleToastBubbles();
        recordButton->click();

        // 协调器起步前先把控制条摆出来，随后校验不过（离屏没有屏名）又把它收起——收起的是「藏」，
        // 销毁要等事件循环，所以这一刻它还数得到；这条同时挡掉「叫到了别的族」：别处不会摆这扇控制条
        EXPECT_EQ(countTopLevelWindows<GyanisScribe::Gui::RecordingHud>(), 1) << "按了录屏，控制条那一闪都没出现：动作条到录屏协调器那条线断了";
        const auto said = [&] { return newToastText(before).contains(QStringLiteral("录屏没能开始")); };
        EXPECT_TRUE(waitUntil(said)) << "录屏没开成，也没把原因说给用户听：接线断在协调器前面";

        // 「没能开始」不许在历史里留下东西：没开始就没成品
        pumpFor(300);
        const auto entries = readHistory();
        ASSERT_TRUE(entries.has_value());
        EXPECT_TRUE(entries->empty()) << "录屏没开始，历史里却多出了记录";
    }

    /**
     * @brief 钉住隐私扫描无命中：只说一句结果，不弹空的审核对话框，画布与窗口都留着
     * @details 隐私打码的入口在标注窗，结果却落在画布上：无命中这条最短的路把
     *          「结果图与画布对齐 → 认码 → 检测 → 提示」整条串起来，且不能把标注窗收掉。
     */
    TEST_F(AppControllerFixture, PrivacyScanWithoutHitsOnlyReportsTheOutcome)
    {
        m_frameScreenName = m_realScreenName;
        makeController();

        auto &window = m_controller->capture().annotationWindow();
        m_controller->capture().startFullScreen(App::CaptureIntent::Annotate);
        ASSERT_TRUE(window.isVisible()) << "标注窗没开起来，后面的判据无从谈起";

        int     messages = 0;
        QString said;
        QObject::connect(&m_controller->capture(), &App::CaptureCoordinator::messageForUser, &m_controller->capture(),
                         [&messages, &said](const QString &message, Gui::ToastKind)
                         {
                             ++messages;
                             said = message;
                         });

        // 纯色替身帧里没有文字也没有码：检测器给空清单
        m_controller->capture().handlePrivacyScan(window.canvas().image, Core::OcrResult{});

        EXPECT_EQ(messages, 1) << "无命中该只说一句结果";
        EXPECT_EQ(said, Base::uiText("没有发现可打码的敏感信息。请仍自行检查后再分享。")) << said.toStdString();
        EXPECT_TRUE(window.isVisible()) << "无命中提示之后标注窗被收掉了";
        EXPECT_TRUE(window.toolbar().isVisible());
    }

    /**
     * @brief 钉住过期结果被丢弃：画布在识别期间换过，就不许把旧框落到新画布上
     * @details 隐私扫描是异步的；等待期间用户按了 F5 或重裁，框的坐标已经不属于当前画布。
     *          这一格拿一张 cacheKey 不同的同尺寸图冒充过期结果，判的是提示而不是静默落笔。
     */
    TEST_F(AppControllerFixture, PrivacyScanRejectsAStaleImage)
    {
        m_frameScreenName = m_realScreenName;
        makeController();

        auto &window = m_controller->capture().annotationWindow();
        m_controller->capture().startFullScreen(App::CaptureIntent::Annotate);
        ASSERT_TRUE(window.isVisible());

        int     messages = 0;
        QString said;
        QObject::connect(&m_controller->capture(), &App::CaptureCoordinator::messageForUser, &m_controller->capture(),
                         [&messages, &said](const QString &message, Gui::ToastKind)
                         {
                             ++messages;
                             said = message;
                         });

        QImage stale(kFrameWidth, kFrameHeight, QImage::Format_ARGB32);
        stale.fill(Qt::black);
        ASSERT_NE(stale.cacheKey(), window.canvas().image.cacheKey());

        m_controller->capture().handlePrivacyScan(stale, Core::OcrResult{});

        EXPECT_EQ(messages, 1) << "过期结果该被挡下并说清出路";
        EXPECT_EQ(said, Base::uiText("画布已经变了，请重新点隐私打码")) << said.toStdString();
        EXPECT_TRUE(window.isVisible());
    }

    /**
     * @brief 钉住「关窗之后迟到的结果」被轮次守卫挡下
     * @details 引擎与画布在关窗后仍留着上一轮，cacheKey 完全对得上；只有轮次守卫能分辨
     *          用户已经收工。这一格用同一张画布冒充迟到结果，判的是提示与窗口不被重新摆出来。
     */
    TEST_F(AppControllerFixture, PrivacyScanAfterCloseIsRejected)
    {
        m_frameScreenName = m_realScreenName;
        makeController();

        auto &window = m_controller->capture().annotationWindow();
        m_controller->capture().startFullScreen(App::CaptureIntent::Annotate);
        ASSERT_TRUE(window.isVisible());
        ASSERT_TRUE(window.isRoundActive());

        // 记下关窗前的画布：迟到结果拿的就是这一张，cacheKey 对得上，走的只能是轮次守卫
        const QImage scanned = window.canvas().image;

        QToolButton *closeButton = nullptr;
        for (auto *candidate: window.toolbar().findChildren<QToolButton *>())
        {
            if (candidate->accessibleName() == Base::uiText("关闭标注"))
            {
                closeButton = candidate;
                break;
            }
        }
        ASSERT_NE(closeButton, nullptr) << "标注窗工具栏上找不到关闭按钮";
        closeButton->click();
        ASSERT_FALSE(window.isVisible()) << "点了关闭按钮，标注窗还留在屏上";
        ASSERT_FALSE(window.isRoundActive()) << "关窗之后这一轮标注还报「在编辑」";

        int     messages = 0;
        QString said;
        QObject::connect(&m_controller->capture(), &App::CaptureCoordinator::messageForUser, &m_controller->capture(),
                         [&messages, &said](const QString &message, Gui::ToastKind)
                         {
                             ++messages;
                             said = message;
                         });

        m_controller->capture().handlePrivacyScan(scanned, Core::OcrResult{});

        EXPECT_EQ(messages, 1) << "关窗之后回来的结果既没落笔也没把原因说给用户";
        EXPECT_EQ(said, Base::uiText("当前没有正在编辑的标注，请重新截图后再试")) << said.toStdString();
        EXPECT_FALSE(window.isVisible()) << "一扇已经关掉的窗被迟到的结果重新摆出来了";
    }

    /**
     * @brief 钉住有命中那一整条：审核确认后按实际交给引擎的框数报成功
     * @details 对话框是模态的，用例在进入 exec 的嵌套事件循环后用定时器按下「应用打码」。
     *          两个手机号各落一个框，成功提示必须是 2；窗口与这一轮都留着。
     */
    TEST_F(AppControllerFixture, PrivacyScanAppliesReviewedBoxesAndReportsTheCount)
    {
        m_frameScreenName = m_realScreenName;
        makeController();

        auto &window = m_controller->capture().annotationWindow();
        m_controller->capture().startFullScreen(App::CaptureIntent::Annotate);
        ASSERT_TRUE(window.isVisible());
        const QImage scanned = window.canvas().image;
        ASSERT_EQ(scanned.size(), QSize(kFrameWidth, kFrameHeight));

        Core::OcrResult result;
        for (const auto &[phone, box]: {std::pair{QStringLiteral("13800138000"), QRect{4, 4, 40, 12}}, std::pair{QStringLiteral("13900139000"), QRect{4, 28, 40, 12}}})
        {
            Core::OcrLine line;
            line.words.append(Core::OcrWord{phone, box});
            line.boundingBox = box;
            result.lines.append(line);
        }

        int     messages = 0;
        QString said;
        QObject::connect(&m_controller->capture(), &App::CaptureCoordinator::messageForUser, &m_controller->capture(),
                         [&messages, &said](const QString &message, Gui::ToastKind)
                         {
                             ++messages;
                             said = message;
                         });

        // 进入 handlePrivacyScan 之后对话框才 exec：定时器在嵌套事件循环里按下「应用打码」
        QTimer::singleShot(0,
                           []
                           {
                               if (auto *dialog = qobject_cast<QDialog *>(QApplication::activeModalWidget()))
                               {
                                   dialog->accept();
                               }
                           });

        m_controller->capture().handlePrivacyScan(scanned, result);

        EXPECT_EQ(messages, 1) << "有命中时既没落笔也没给成功提示";
        EXPECT_EQ(said, Base::uiText("已打码 2 处；请自行检查后再分享。")) << said.toStdString();
        EXPECT_TRUE(window.isVisible()) << "应用打码把标注窗收了";
        EXPECT_TRUE(window.isRoundActive());
    }

    /**
     * @brief 钉住对话框合上之后的复核：审核期间这一轮收场了就不再落笔
     * @details 模态框挡得住用户点窗，挡不住宿主或键盘路径收场；exec 返回后必须重认轮次，
     *          否则一批框会落到一扇已经关掉的窗口背后的画布上。
     */
    TEST_F(AppControllerFixture, PrivacyScanRechecksTheRoundAfterTheReviewDialog)
    {
        m_frameScreenName = m_realScreenName;
        makeController();

        auto &window = m_controller->capture().annotationWindow();
        m_controller->capture().startFullScreen(App::CaptureIntent::Annotate);
        ASSERT_TRUE(window.isVisible());
        const QImage scanned = window.canvas().image;

        QToolButton *closeButton = nullptr;
        for (auto *candidate: window.toolbar().findChildren<QToolButton *>())
        {
            if (candidate->accessibleName() == Base::uiText("关闭标注"))
            {
                closeButton = candidate;
                break;
            }
        }
        ASSERT_NE(closeButton, nullptr) << "标注窗工具栏上找不到关闭按钮";

        Core::OcrResult result;
        Core::OcrLine   line;
        line.words.append(Core::OcrWord{QStringLiteral("13800138000"), QRect{4, 4, 40, 12}});
        line.boundingBox = QRect{4, 4, 40, 12};
        result.lines.append(line);

        int     messages = 0;
        QString said;
        QObject::connect(&m_controller->capture(), &App::CaptureCoordinator::messageForUser, &m_controller->capture(),
                         [&messages, &said](const QString &message, Gui::ToastKind)
                         {
                             ++messages;
                             said = message;
                         });

        // 审核框还开着时收场，再按下「应用打码」：这一步只有落笔前的复核能挡住
        QTimer::singleShot(0,
                           [closeButton]
                           {
                               closeButton->click();
                               if (auto *dialog = qobject_cast<QDialog *>(QApplication::activeModalWidget()))
                               {
                                   dialog->accept();
                               }
                           });

        m_controller->capture().handlePrivacyScan(scanned, result);

        EXPECT_EQ(messages, 1) << "审核期间收场后既没落笔也没说清原因";
        EXPECT_EQ(said, Base::uiText("当前没有正在编辑的标注，请重新截图后再试")) << said.toStdString();
        EXPECT_FALSE(window.isVisible()) << "一扇已经关掉的窗被重新摆出来了";
    }

    /**
     * @brief 钉住隐私协调器确实接在窗口入口与画布那一层之间
     * @details 控制器里没有别的口子能拿到这份协调器，接线少一根都只能靠读代码发现：
     *          窗口的 privacyRequested 要叫到 requestScan（协调器当场回一句「正在检查」或后端原因），
     *          协调器的 scanSucceeded 要落到 CaptureCoordinator::handlePrivacyScan（无命中那句提示）。
     *          真识别一轮由 TestPrivacyCoordinator 的假引擎覆盖，这里只判接线。
     */
    TEST_F(AppControllerFixture, PrivacyCoordinatorIsWiredToWindowAndCanvas)
    {
        m_frameScreenName = m_realScreenName;
        makeController();

        auto &privacy = m_controller->privacyCoordinatorForTesting();
        auto &capture = m_controller->capture();
        auto &window  = capture.annotationWindow();
        capture.startFullScreen(App::CaptureIntent::Annotate);
        ASSERT_TRUE(window.isVisible());

        int privacyMessages = 0;
        QObject::connect(&privacy, &App::PrivacyCoordinator::messageForUser, &privacy, [&privacyMessages](const QString &, Gui::ToastKind) { ++privacyMessages; });

        QToolButton *privacyButton = nullptr;
        for (auto *candidate: window.toolbar().findChildren<QToolButton *>())
        {
            if (candidate->accessibleName() == Base::uiText("隐私打码"))
            {
                privacyButton = candidate;
                break;
            }
        }
        ASSERT_NE(privacyButton, nullptr) << "标注窗工具栏上找不到隐私打码入口";
        privacyButton->click();
        EXPECT_EQ(privacyMessages, 1) << "窗口的隐私打码入口没有叫到协调器（既没开始检查也没报后端原因）";
        EXPECT_TRUE(window.isVisible()) << "发起检查就把标注窗收了";

        int     canvasMessages = 0;
        QString said;
        QObject::connect(&capture, &App::CaptureCoordinator::messageForUser, &capture,
                         [&canvasMessages, &said](const QString &message, Gui::ToastKind)
                         {
                             ++canvasMessages;
                             said = message;
                         });

        // 直接发一条成功结果：真识别由 TestPrivacyCoordinator 的假引擎驱动，这里判的是这根线
        privacy.scanSucceeded(window.canvas().image, Core::OcrResult{});
        EXPECT_EQ(canvasMessages, 1) << "协调器的结果没有落到画布那一层";
        EXPECT_EQ(said, Base::uiText("没有发现可打码的敏感信息。请仍自行检查后再分享。")) << said.toStdString();
    }

} // namespace GyanisScribe
