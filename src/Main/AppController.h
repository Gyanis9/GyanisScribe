/**
 * @file AppController.h
 * @brief 把各模块接起来的那一层：信号槽集中在这里，模块之间不互相 include 界面
 * @author Gyanis
 * @date 2026-10-09
 * @version 0.0.1
 * @copyright Copyright (c) . All rights reserved.
 */

#pragma once

#include "Base/Types.h"
#include "Core/Hotkey/IHotkeyManager.h"

#include "Main/MainWindow.h"
#include "App/CaptureIntent.h"
#include "App/ImageFetcher.h"
#include "App/CommandLine.h"
#include "App/TrayController.h"
#include "Gui/Controls/Toast.h"

#include <QObject>

#include <QImage>
#include <QPointer>
#include <QSize>

class QTimer;
class QScreen;

#include <filesystem>
#include <functional>
#include <memory>
#include <optional>

namespace GyanisScribe::Core
{
    class IScreenCaptureSource;
} // namespace GyanisScribe::Core

namespace GyanisScribe::Gui
{
    class CaptureService;
} // namespace GyanisScribe::Gui

namespace GyanisScribe::Core
{
    class AnnotationEngine;
} // namespace GyanisScribe::Core

namespace GyanisScribe::Gui
{
    class AnnotationWindow;
} // namespace GyanisScribe::Gui

namespace GyanisScribe::App
{
    class HistoryCoordinator;
    class OcrCoordinator;
    class CaptureCoordinator;
    class DeliveryCoordinator;
    class PickCoordinator;
    class PinCoordinator;
    class RecordingCoordinator;
} // namespace GyanisScribe::App

namespace GyanisScribe::Core
{
    enum class HistoryKind;
    class HistoryStore;
} // namespace GyanisScribe::Core

namespace GyanisScribe::Gui
{
    class ColorPickerOverlay;
} // namespace GyanisScribe::Gui

namespace GyanisScribe::Gui
{
    class PinManager;
    class PinGroupManagerWindow;
    struct PinBatchSaveReport;
} // namespace GyanisScribe::Gui

namespace GyanisScribe::Core
{
    struct RecorderConfig;
} // namespace GyanisScribe::Core

namespace GyanisScribe::Gui
{
    class RecordingHud;
} // namespace GyanisScribe::Gui

namespace GyanisScribe::Plugins
{
    class FfmpegRecordingEncoder;
    class RecordingService;
} // namespace GyanisScribe::Plugins

namespace GyanisScribe::Core
{
    class HotkeyManager;
} // namespace GyanisScribe::Core

namespace GyanisScribe::Platform
{
    class IPlatformIntegration;
} // namespace GyanisScribe::Platform

namespace GyanisScribe::Core
{
    class SettingsStore;
} // namespace GyanisScribe::Core

namespace GyanisScribe::Gui
{
    class IconProvider;
    class ThemeManager;
    class Toast;
} // namespace GyanisScribe::Gui

namespace GyanisScribe::App
{
    class TrayController;
} // namespace GyanisScribe::App

namespace GyanisScribe::App
{
    struct DropPinOutcome;
} // namespace GyanisScribe::App

namespace GyanisScribe::Main
{
    class MainWindow;

    /**
     * @brief 建控制器时可以换掉的几件底物
     * @details 只有测试用这两档：设置、平台集成、抓帧来源与历史清单落点是构造里唯一碰「本机环境」的根，
     *          其余模块都由它们派生。每一格留空就按生产那一份自建，两个公开构造因此汇在同一处装配。
     */
    struct AppControllerParts
    {
        std::unique_ptr<GyanisScribe::Core::SettingsStore>            settings;           ///< 配置存储；空 = 按便携标记自建
        std::unique_ptr<GyanisScribe::Platform::IPlatformIntegration> integration;        ///< 平台集成；空 = 本机那一份
        std::unique_ptr<GyanisScribe::Core::IScreenCaptureSource>     captureSource;      ///< 抓帧来源；空 = 本机抓屏
        std::filesystem::path                                         historyStoragePath; ///< 历史清单落点；空 = 默认位置
    };


    /**
     * @brief 核心控制器
     * @details 依赖在这里创建、连接也在这里挂：Capture 不认识 Hotkey，Hotkey 不认识 Settings，
     *          三者只经由本控制器串起来。后续加贴图、录屏、识别时同样只补连接，不改各模块。
     */
    class AppController : public QObject
    {
        Q_OBJECT

    public:
        /**
         * @brief 构造控制器并创建各模块实例
         * @param parent QObject 父对象，默认无
         * @note 要在 QApplication 存在之后构造：配置、屏幕列表与热键后端都要靠应用对象
         */
        explicit AppController(QObject *parent = nullptr);

        /**
         * @brief 用换进来的底物构造控制器（测试注入口）
         * @param parts 要换掉的底物；留空的格子按生产那一份自建
         * @param parent QObject 父对象，默认无
         * @details 与不带参数的那个构造汇到同一处私有构造：装配逻辑只写一份，
         *          生产路径就相当于把四个格子全留空。
         */
        explicit AppController(AppControllerParts parts, QObject *parent = nullptr);

        /**
         * @brief 析构控制器，按创建的反向顺序交还各模块
         */
        ~AppController() override;

        AppController(const AppController &)            = delete;
        AppController &operator=(const AppController &) = delete;

        /**
         * @brief 登记全局热键
         * @return 成功返回本轮登记结果；热键一项都没登记上也返回成功结果，失败项在报告里
         * @note 托盘、设置页与热键重载都调这一个，避免出现第二套登记路径
         */
        [[nodiscard]] GyanisScribe::Base::Expected<GyanisScribe::Core::HotkeyReloadReport> registerHotkeys();

        /**
         * @brief 打开主窗口的某一页
         * @param page 目标页
         */
        void openMainWindow(MainWindowPage page);

        /**
         * @brief 命令行那次截图：按请求里点名的去处逐处交付，不做任何交互
         * @param request 已解析的启动请求：范围、`--to` 点名的去处与 `--exec` 那一条命令
         * @details 命令行没有界面可等，也不走延时那道闸。交付顺序恒为「存文件 → 剪贴板 → 贴到屏幕上」；
         *          全部去处都交付完从 commandLineCaptureDelivered 交回成品路径（没点 file 时为空），
         *          取不到条件（比如没有前台窗口）或某一路交付失败时从 commandLineActionFailed 交回中文原因。
         *          **截图这一族在 `CaptureCoordinator` 里**，这一处只转发：`Application` 那条命令行入口认的是控制器。
         */
        void startCommandLineCapture(const GyanisScribe::App::CommandLineRequest &request);

        /// 命令行 `--pick` 的入口：取色这一族在 `PickCoordinator` 里，这里只转发
        void startColorPick();

        /// 命令行 `--pin` 的入口：识图与贴出那一族在 `PinCoordinator` 里，这里只转发
        [[nodiscard]] GyanisScribe::Base::Expected<void> pinImageFile(const QString &filePath);

        /**
         * @brief 截图这一族的协调器，供用例与命令行入口取用
         * @return 本控制器持有的那一份，所有权不在调用方
         */
        [[nodiscard]] GyanisScribe::App::CaptureCoordinator &capture() const noexcept;

        /**
         * @brief 配置存储，供设置页与后续模块取用
         * @return 本控制器持有的那一份，所有权不在调用方
         */
        [[nodiscard]] GyanisScribe::Core::SettingsStore &settings() const noexcept;

        /**
         * @brief 主题管理器
         * @return 控制器创建的那一个，界面从这里取当前生效的色板与图标描边色
         */
        [[nodiscard]] GyanisScribe::Gui::ThemeManager &theme() const noexcept;

        /**
         * @brief 图标供给器
         * @return 全应用共用的那一个，描边色已跟着主题接好
         */
        [[nodiscard]] GyanisScribe::Gui::IconProvider &icons() const noexcept;

    Q_SIGNALS:
        /**
         * @brief 命令行那一次截图交付完事
         * @param savedPath 这一轮落盘的成品路径；没点名 `file` 时为空
         * @details 脚本靠它把路径打到标准输出并定退出码；`--to` 里点了 `pin` 时进程不退出，
         *          那一张贴图属于这个进程，退出就把它一起带走了
         */
        void commandLineCaptureDelivered(const QString &savedPath);

        /**
         * @brief 命令行动作当场失败（还没走到截图就取不到条件，或某一路交付没成）
         * @param reason 中文原因
         */
        void commandLineActionFailed(const QString &reason);

    private:
        /**
         * @brief 两个公开构造的汇合点
         * @param settings 配置存储，所有权交进来
         * @param integration 平台集成，所有权交进来
         * @param captureSource 抓帧来源，所有权交进来
         * @param historyStoragePath 历史清单落点，非空
         * @param parent QObject 父对象
         */
        AppController(std::unique_ptr<GyanisScribe::Core::SettingsStore> settings, std::unique_ptr<GyanisScribe::Platform::IPlatformIntegration> integration,
                      std::unique_ptr<GyanisScribe::Core::IScreenCaptureSource> captureSource, std::filesystem::path historyStoragePath, QObject *parent);

        /**
         * @brief 读设置里那一档「截完之后做什么」
         * @return 配置里存的档位；读不出时回落到词表第一档并把原因落进日志
         */
        [[nodiscard]] GyanisScribe::App::CaptureIntent settingsCaptureIntent() const;

        /// 托盘菜单的一项被点了
        void handleTrayAction(GyanisScribe::App::TrayAction action);

        /**
         * @brief 把某张贴图此刻盖住的那块屏幕重抓一次，换掉它的画面（`F5` 与右键那一项的落点）
         * @param index 发起那一张在册表里的序号
         * @details 抓之前那一张要暂时从桌上拿走（贴图是置顶窗，不藏就会抓到贴图的自己），抓完摆回原样；
         *          摆位、缩放、旋转与透明度都不动。按住不放时键盘自动重复连着报，就成了持续刷新。
         */
        void refreshPinWithScreenUnder(int index);

        /// 给用户看的一条提示：有气泡就用气泡，没有（例如起不来托盘的那类环境）就落日志
        void notifyUser(const QString &message, GyanisScribe::Gui::ToastKind kind);

        std::unique_ptr<GyanisScribe::Core::SettingsStore>            m_settings;       ///< 配置存储
        std::unique_ptr<GyanisScribe::Platform::IPlatformIntegration> m_integration;    ///< 平台集成
        std::unique_ptr<GyanisScribe::Core::IScreenCaptureSource>     m_captureSource;  ///< 抓帧来源
        std::unique_ptr<GyanisScribe::Core::HotkeyManager>            m_hotkeys;        ///< 热键登记与触发
        std::unique_ptr<GyanisScribe::Gui::ThemeManager>              m_theme;          ///< 主题档位与样式表
        std::unique_ptr<GyanisScribe::Gui::IconProvider>              m_icons;          ///< 图标供给器，描边色跟主题
        std::unique_ptr<GyanisScribe::Gui::PinManager>                m_pins;           ///< 贴图管理器
        std::unique_ptr<GyanisScribe::App::RecordingCoordinator>      m_recording;      ///< 录屏这一族（配置、服务、控制条、倒计时）
        std::unique_ptr<GyanisScribe::App::OcrCoordinator>            m_ocr;            ///< 识别这一族（后端选型、搬运工、结果窗）
        std::unique_ptr<GyanisScribe::App::TrayController>            m_tray;           ///< 托盘图标与菜单
        std::unique_ptr<MainWindow>                                   m_mainWindow;     ///< 设置与关于的入口
        std::unique_ptr<GyanisScribe::Gui::Toast>                     m_toast;          ///< 右下角提示气泡，操作结果的落地处
        std::unique_ptr<GyanisScribe::Core::HistoryStore>             m_historyStore;   ///< 历史清单本体：与主窗口共享同一份，删记录不删文件
        std::unique_ptr<GyanisScribe::App::HistoryCoordinator>        m_history;        ///< 历史这一族（增删清空与「这条去哪了」）
        std::unique_ptr<GyanisScribe::App::PickCoordinator>           m_picker;         ///< 取色这一族（遮罩、写法与字母档、最近取色）
        std::unique_ptr<GyanisScribe::App::CaptureCoordinator>        m_capture;        ///< 截图这一族（枢纽：入口、延时、抓帧、按意图交付）
        std::unique_ptr<GyanisScribe::App::PinCoordinator>            m_pinCoordinator; ///< 贴图这一族的贴出动作（剪贴板/文件/回贴/组管理）
        std::unique_ptr<GyanisScribe::App::DeliveryCoordinator>       m_delivery;       ///< 交付这一族（装饰、落盘、外部命令、批量报告）
    };

} // namespace GyanisScribe::Main
