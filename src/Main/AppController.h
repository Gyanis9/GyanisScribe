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
    class DeliveryCoordinator;
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
         * @brief 进入截图框选
         * @param intent 截完之后做什么；不填就按设置里那一档（Capture/AfterAction）走
         * @note 此处刻意不用 GyanisScribe::App::CaptureIntent 作默认值：默认值只能是「不填」这一个含义，
         *       写成 = Annotate 就分不清「用户没选」与「用户选了进标注」，设置里那档也就没了读的地方
         */
        void startCapture(std::optional<GyanisScribe::App::CaptureIntent> intent = std::nullopt);

        /**
         * @brief 整屏截图
         * @param intent 截完之后做什么，不填按设置里那一档
         */
        void startFullScreenCapture(std::optional<GyanisScribe::App::CaptureIntent> intent = std::nullopt);

        /**
         * @brief 重复上一次的选区截图
         * @param intent 截完之后做什么，不填按设置里那一档
         */
        void startLastRegionCapture(std::optional<GyanisScribe::App::CaptureIntent> intent = std::nullopt);

        /**
         * @brief 起一轮滚动长截图
         * @details 与 startCapture 同一条延时通道：先等设置里那几秒，再铺遮罩让人框出要滚的那一条。
         *          接好的长图从 captured 出来，去向仍由设置里那一档决定。
         */
        void startLongCapture();

        /**
         * @brief 新建一张空白画布并直接进标注窗（托盘与主窗口那一条）
         * @details 不走截图那条链：没有帧、没有选区记录、不碰「上次选区」，也不触发音效、自动保存与历史
         *          ——那一串都挂在「抓到了一张屏上的图」这个前提上，这里什么都没抓。
         *          画布摆在光标所在那块屏的可用区中央，来源区域留空，因此 `F5` 刷新会自己拒并说明原因。
         */
        void startBlankCanvas();

        /**
         * @brief 截取当前前台窗口
         * @details 与 startCapture 同一条延时通道，前台窗口在到点那一刻才取：等的那几秒本就是用来
         *          把菜单、悬停状态摆出来的，按下时就取走会把这一档的意义抹掉。
         *          句柄由平台集成给（Windows 走 GetForegroundWindow），抓帧那一步按句柄去问几何；
         *          平台给不出句柄时把原因发给用户，而不是静默什么都不做。
         */
        void captureForegroundWindow();

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
         */
        void startCommandLineCapture(const GyanisScribe::App::CommandLineRequest &request);

        /**
         * @brief 起一层全屏取色遮罩（热键、托盘与命令行共用这一条）
         * @details 抓的是按下那一刻的那一屏，之后遮罩显示的就是这一帧：放大镜要看的正是当前画面，
         *          实时抓屏会让鼠标一停画面就变，颜色也就抄不准。
         */
        void startColorPick();

        /// 命令行 `--pin` 的入口：识图与贴出那一族在 `PinCoordinator` 里，这里只转发
        [[nodiscard]] GyanisScribe::Base::Expected<void> pinImageFile(const QString &filePath);

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
         * @brief 抓到一帧之后的收尾：开启一轮标注
         * @param image 抓屏产物
         * @details 截完不再直接进剪贴板：标注窗的「复制」就是原来的那一步，只是多了先标一笔的机会
         */
        void handleCaptured(const GyanisScribe::Base::CapturedImage &image);

        /**
         * @brief 按「截完之后」那一档把这一帧交出去，最后一档是开标注窗
         * @param image 抓到的图
         * @param screen 截图所在那块屏（调用方已确认它在场）
         * @details 与「截图完成」那串记账分开写：外部命令要在交付之后才跑，而这一处每个档位都自己 return。
         */
        void deliverCapturedFrame(const QImage &image, QScreen &screen);

        /**
         * @brief 命令行那一次的交付：把点名的几处逐处走完
         * @details 交付顺序只由 App/CaptureDelivery.h 那张词表定；任一处失败就整次报错退出码 1，
         *          不把「成了两路、少了一路」说成成功。
         */
        void deliverCommandLineOutputs();

        /**
         * @brief 读设置里那一档「截完之后做什么」
         * @return 配置里存的档位；读不出时回落到词表第一档并把原因落进日志
         */
        [[nodiscard]] GyanisScribe::App::CaptureIntent settingsCaptureIntent() const;

        /**
         * @brief 按设置里的延时决定「现在就开」还是「等 N 秒再开」
         * @param begin 真正开始截图的那一步，延时的读端与热键那条走同一个
         * @details 延时是为「菜单、悬停提示这类一碰就没的画面」留的：没有它就只能整屏截完再裁。
         *          延时期内再按一次不排队、也不叠第二个定时器——两层遮罩叠着弹出来没人收得干净，
         *          所以只说一句「上一次还在等」。
         */
        void scheduleCapture(std::function<void()> begin);

        /// 标注窗请求复制
        void handleCopyRequested(const QImage &image);

        /// 托盘菜单的一项被点了
        void handleTrayAction(GyanisScribe::App::TrayAction action);

        /**
         * @brief 把某张贴图此刻盖住的那块屏幕重抓一次，换掉它的画面（`F5` 与右键那一项的落点）
         * @param index 发起那一张在册表里的序号
         * @details 抓之前那一张要暂时从桌上拿走（贴图是置顶窗，不藏就会抓到贴图的自己），抓完摆回原样；
         *          摆位、缩放、旋转与透明度都不动。按住不放时键盘自动重复连着报，就成了持续刷新。
         */
        void refreshPinWithScreenUnder(int index);

        /**
         * @brief 标注窗请求识别二维码/条形码：认出来就复制到剪贴板并报一声
         * @param image 成品图（未装饰）
         * @details 与识别文字不同，这里不另开结果窗：多半只是想知道「这里头是什么」——
         *          认出来直接进剪贴板，提示里说清认出了几个；一个都没认出来也只报一声，
         *          标注窗不关（见 AnnotationWindow::scanRequested）
         */
        void handleScanRequested(const QImage &image);

        /**
         * @brief 标注窗请求打印：开系统打印对话框，按纸面等比居中画上去
         * @param image 成品图（未装饰）
         * @details 用户取消对话框就静默收场——打印这件事「什么都没发生」也是一种正常结果。
         *          打到纸上按没有透明通道处理：投影底下垫白，不然透明区会按驱动作自己的解释落色。
         */
        void handlePrintRequested(const QImage &image);

        /**
         * @brief 标注窗请求刷新截图（F5）：重抓这块区域换底图，标注留着
         * @param screenName 屏幕名
         * @param region 该屏内的逻辑区域
         * @details 走采集服务的就地重抓口：不出 captured、不记选区，也就不会触发「截图完成」
         *          那一串反应（音效、自动保存、意图分派）——刷新不是一次新截图。
         */
        void refreshLastCapture(const QString &screenName, const QRect &region);

        /**
         * @brief 某张贴图上取到了一个像素的颜色
         * @param color 颜色
         * @details 排成设置里那一档写法、复制到剪贴板并给一句提示：与屏幕取色那一路同一个格式出口，
         *          用户从哪一处取的色，粘出来的都是同一种写法；两处都进「最近取色」那一份清单
         */
        void handlePinColorPicked(const QColor &color);

        /**
         * @brief 读「最近取色」清单
         * @return 设置里那份清单（最近的在前）；坏词整体拒绝、按空清单继续并把原因落进日志
         */
        [[nodiscard]] std::vector<QColor> recentPickedColors() const;

        /**
         * @brief 把刚取到的颜色压进「最近取色」清单并写回设置
         * @param color 颜色
         */
        void rememberPickedColor(const QColor &color);

        /// 给用户看的一条提示：有气泡就用气泡，没有（例如起不来托盘的那类环境）就落日志
        void notifyUser(const QString &message, GyanisScribe::Gui::ToastKind kind);

        /**
         * @brief 去把一条图片地址取回来并贴到屏上
         * @param url 图片地址，调用方已用 `GyanisScribe::App::isFetchableImageUrl` 粗筛过
         * @param dropPoint 落点；空表示跟着光标走（剪贴板那条路没有「原来那块位置」）
         * @details 取图是异步的：这里只发起，成败都在 `GyanisScribe::App::ImageFetcher` 的两条信号上收口。
         */
        void startImageFetch(const QUrl &url, std::optional<QPoint> dropPoint);

        /**
         * @brief 抓一块点名要的屏
         * @param screenName 屏幕名
         * @details 走整屏截图同一条路（含延时那一档）：两处各写一份截全屏的流程，
         *          延时与成品去向那几档迟早只改一处。
         */
        void captureNamedScreen(const QString &screenName);

        std::unique_ptr<GyanisScribe::Core::SettingsStore>            m_settings;          ///< 配置存储
        std::unique_ptr<GyanisScribe::Platform::IPlatformIntegration> m_integration;       ///< 平台集成
        std::unique_ptr<GyanisScribe::Core::IScreenCaptureSource>     m_captureSource;     ///< 抓帧来源
        std::unique_ptr<GyanisScribe::Gui::CaptureService>            m_capture;           ///< 截图编排
        std::unique_ptr<GyanisScribe::Core::HotkeyManager>            m_hotkeys;           ///< 热键登记与触发
        std::unique_ptr<GyanisScribe::Gui::ThemeManager>              m_theme;             ///< 主题档位与样式表
        std::unique_ptr<GyanisScribe::Gui::IconProvider>              m_icons;             ///< 图标供给器，描边色跟主题
        std::unique_ptr<GyanisScribe::Core::AnnotationEngine>         m_annotation;        ///< 标注引擎，跨轮次保留（载入新帧会清空）
        std::unique_ptr<GyanisScribe::Gui::AnnotationWindow>          m_annotationWindow;  ///< 标注窗口，复用同一把工具栏
        std::unique_ptr<GyanisScribe::Gui::PinManager>                m_pins;              ///< 贴图管理器
        std::unique_ptr<GyanisScribe::App::ImageFetcher>              m_imageFetcher;      ///< 网页取图：拖进来的可以是一条图片地址
        std::optional<QPoint>                                         m_pendingFetchPoint; ///< 在飞那一张取回来落在哪儿；空表示跟光标走
        std::unique_ptr<GyanisScribe::App::RecordingCoordinator>      m_recording;         ///< 录屏这一族（配置、服务、控制条、倒计时）
        std::unique_ptr<GyanisScribe::App::OcrCoordinator>            m_ocr;               ///< 识别这一族（后端选型、搬运工、结果窗）
        std::unique_ptr<GyanisScribe::App::TrayController>            m_tray;              ///< 托盘图标与菜单
        std::unique_ptr<MainWindow>                                   m_mainWindow;        ///< 设置与关于的入口
        std::unique_ptr<GyanisScribe::Gui::Toast>                     m_toast;             ///< 右下角提示气泡，操作结果的落地处
        std::unique_ptr<GyanisScribe::Core::HistoryStore>             m_historyStore;      ///< 历史清单本体：与主窗口共享同一份，删记录不删文件
        std::unique_ptr<GyanisScribe::App::HistoryCoordinator>        m_history;           ///< 历史这一族（增删清空与「这条去哪了」）
        QPointer<GyanisScribe::Gui::ColorPickerOverlay>               m_picker;            ///< 取色遮罩，关掉即重建
        std::unique_ptr<GyanisScribe::App::PinCoordinator>            m_pinCoordinator;    ///< 贴图这一族的贴出动作（剪贴板/文件/回贴/组管理）
        GyanisScribe::App::CaptureIntent                              m_captureIntent = GyanisScribe::App::CaptureIntent::Annotate; ///< 这一帧截完要去做的事
        GyanisScribe::App::CaptureOutputSet                           m_commandLineOutputs;                                         ///< 命令行那一次点名的去处，已按交付顺序排好
        QString                                                       m_commandLineExec;              ///< --exec 写的那一条命令；只在 m_commandLineExecGiven 为真时压过设置里那一格
        bool                                                          m_commandLineExecGiven = false; ///< 命令行到底给没给 --exec：给了就只跑那一条，不与设置里的命令叠着跑两次
        std::unique_ptr<GyanisScribe::App::DeliveryCoordinator>       m_delivery;                     ///< 交付这一族（装饰、落盘、外部命令、批量报告）
        bool                                                          m_commandLineActive = false;    ///< 正在跑命令行那次截图：失败路径要往 commandLineActionFailed 也交一份
        QTimer                                                       *m_captureDelayTimer = nullptr;  ///< 延时截图那一枚，一次只允许有一个在等
        std::function<void()>                                         m_pendingCapture;               ///< 延时到点要走的那一步，随定时器一起活到触发
        GyanisScribe::Base::CapturedImage                             m_lastCapture;                  ///< 本轮抓到的帧，贴图与识别要用它定位
        QRect                                                         m_lastSelection;                ///< 本轮选区（全局逻辑坐标），标注窗与贴图的位置来源
    };

} // namespace GyanisScribe::Main
