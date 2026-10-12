/**
 * @file AppController.h
 * @brief 把各模块接起来的那一层：信号槽集中在这里，模块之间不互相 include 界面
 * @author Gyanis
 * @date 2026-10-09
 * @version 0.0.1
 * @copyright Copyright (c) . All rights reserved.
 */

#pragma once

#include "Core/Types.h"
#include "Hotkey/IHotkeyManager.h"

#include "App/MainWindow.h"
#include "App/CaptureIntent.h"
#include "App/CommandLine.h"
#include "App/TrayController.h"
#include "Ui/Toast.h"

#include <QObject>

#include <QImage>
#include <QPointer>
#include <QSize>

class QTimer;

#include <functional>
#include <memory>
#include <optional>

namespace GyanisScribe::Capture
{
    class CaptureService;
    class IScreenCaptureSource;
} // namespace GyanisScribe::Capture

namespace GyanisScribe::Annotation
{
    class AnnotationEngine;
    class AnnotationWindow;
} // namespace GyanisScribe::Annotation

namespace GyanisScribe::Ocr
{
    class IOcrEngine;
    class OcrResultWindow;
    class RecognitionRunner;
    struct OcrResult;
} // namespace GyanisScribe::Ocr

namespace GyanisScribe::History
{
    enum class HistoryKind;
    class HistoryStore;
} // namespace GyanisScribe::History

namespace GyanisScribe::Pick
{
    class ColorPickerOverlay;
} // namespace GyanisScribe::Pick

namespace GyanisScribe::Pin
{
    class PinManager;
} // namespace GyanisScribe::Pin

namespace GyanisScribe::Record
{
    class FfmpegRecordingEncoder;
    class RecordingHud;
    struct RecorderConfig;
    class RecordingService;
} // namespace GyanisScribe::Record

namespace GyanisScribe::Hotkey
{
    class HotkeyManager;
} // namespace GyanisScribe::Hotkey

namespace GyanisScribe::Platform
{
    class IPlatformIntegration;
} // namespace GyanisScribe::Platform

namespace GyanisScribe::Settings
{
    class SettingsStore;
} // namespace GyanisScribe::Settings

namespace GyanisScribe::Ui
{
    class IconProvider;
    class ThemeManager;
    class Toast;
} // namespace GyanisScribe::Ui

namespace GyanisScribe::App
{
    struct DropPinOutcome;
    class MainWindow;
    class TrayController;


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
        [[nodiscard]] Core::Expected<Hotkey::HotkeyReloadReport> registerHotkeys();

        /**
         * @brief 进入截图框选
         * @param intent 截完之后做什么；不填就按设置里那一档（Capture/AfterAction）走
         * @note 此处刻意不用 CaptureIntent 作默认值：默认值只能是「不填」这一个含义，
         *       写成 = Annotate 就分不清「用户没选」与「用户选了进标注」，设置里那档也就没了读的地方
         */
        void startCapture(std::optional<CaptureIntent> intent = std::nullopt);

        /**
         * @brief 整屏截图
         * @param intent 截完之后做什么，不填按设置里那一档
         */
        void startFullScreenCapture(std::optional<CaptureIntent> intent = std::nullopt);

        /**
         * @brief 重复上一次的选区截图
         * @param intent 截完之后做什么，不填按设置里那一档
         */
        void startLastRegionCapture(std::optional<CaptureIntent> intent = std::nullopt);

        /**
         * @brief 起一轮滚动长截图
         * @details 与 startCapture 同一条延时通道：先等设置里那几秒，再铺遮罩让人框出要滚的那一条。
         *          接好的长图从 captured 出来，去向仍由设置里那一档决定。
         */
        void startLongCapture();

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
         * @brief 命令行那次截图：处理固定走「快速保存」那一档（落盘 + 剪贴板），不做任何交互
         * @param action 三档范围之一：整屏 / 上次选区 / 前台窗口
         * @details 命令行没有界面可等，也不走延时那道闸。落盘成功从 imageFileSaved 交回路径，
         *          取不到条件（比如没有前台窗口）时从 commandLineActionFailed 交回中文原因。
         */
        void startCommandLineCapture(CommandLineAction action);

        /**
         * @brief 命令行贴图：把盘上的图片贴到光标处
         * @param filePath 图片文件路径
         * @return 成功返回空载荷；文件不在、或不是能贴的图片时返回中文原因
         * @details 复用拖放那套内容转换：图片读像素、动图连来源带走、读不出的说清是哪一件
         */
        [[nodiscard]] Core::Expected<void> pinImageFile(const QString &filePath);

        /**
         * @brief 起一层全屏取色遮罩（热键、托盘与命令行共用这一条）
         * @details 抓的是按下那一刻的那一屏，之后遮罩显示的就是这一帧：放大镜要看的正是当前画面，
         *          实时抓屏会让鼠标一停画面就变，颜色也就抄不准。
         */
        void startColorPick();

        /**
         * @brief 录屏热键：没在录就开始整屏录，正在录就停下并编码
         * @details 独立的录屏界面还没做，先做成一个开关：再按一次就是「停止并保存」，
         *          用户不必在屏幕另一头找停止按钮。
         */
        void toggleRecording();

        /**
         * @brief 配置存储，供设置页与后续模块取用
         * @return 本控制器持有的那一份，所有权不在调用方
         */
        [[nodiscard]] Settings::SettingsStore &settings() const noexcept;

        /**
         * @brief 主题管理器
         * @return 控制器创建的那一个，界面从这里取当前生效的色板与图标描边色
         */
        [[nodiscard]] GyanisScribe::Ui::ThemeManager &theme() const noexcept;

        /**
         * @brief 图标供给器
         * @return 全应用共用的那一个，描边色已跟着主题接好
         */
        [[nodiscard]] GyanisScribe::Ui::IconProvider &icons() const noexcept;

    Q_SIGNALS:
        /**
         * @brief 成品落盘：存文件、快速保存与自动保存三条路共用这一处
         * @param filePath 写下的完整路径
         * @details 命令行那次截图靠它把路径打到标准输出；界面这一侧没有人监听
         */
        void imageFileSaved(const QString &filePath);

        /**
         * @brief 命令行动作当场失败（还没走到截图就取不到条件）
         * @param reason 中文原因
         */
        void commandLineActionFailed(const QString &reason);

    private:
        /**
         * @brief 抓到一帧之后的收尾：开启一轮标注
         * @param image 抓屏产物
         * @details 截完不再直接进剪贴板：标注窗的「复制」就是原来的那一步，只是多了先标一笔的机会
         */
        void handleCaptured(const Core::CapturedImage &image);

        /**
         * @brief 读设置里那一档「截完之后做什么」
         * @return 配置里存的档位；读不出时回落到词表第一档并把原因落进日志
         */
        [[nodiscard]] CaptureIntent settingsCaptureIntent() const;

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
        void handleTrayAction(TrayAction action);

        /// 拼这一轮录屏的参数：屏幕名与输出目录都从这里取
        [[nodiscard]] Core::Expected<Record::RecorderConfig> makeRecordingConfig() const;

        /// 起一层录屏控制条并开始每秒推一次状态
        void showRecordingHud();

        /// 把服务当前状态推给控制条
        void updateRecordingHud();

        /// 收起控制条与轮询
        void hideRecordingHud();

        /// 录屏结束：把成品路径落到日志，历史按这一轮实际落帧的尺寸记（一张都没落就不记尺寸）
        void handleRecordingFinished(const QString &outputPath, const QSize &frameSize);

        /// 录屏中止：把原因落到日志
        void handleRecordingFailed(const QString &reason);

        /**
         * @brief 控制条上的「取消」：把正在录的这一段丢掉
         * @details 走 RecordingService::cancel() 而不是 toggleRecording()：后者会封盘并起一次编码，
         *          等成品出来再删等于让用户白等一趟，也把「这一段不要了」做成了有成品的一轮。
         */
        void cancelRecording();

        /// 录屏已取消：收起控制条，说一句「没有成品」
        void handleRecordingAborted();

        /// 标注窗请求存文件
        void handleSaveRequested(const QImage &image);

        /**
         * @brief 把成品写进截图目录：装饰、格式质量校验、起名、落盘与历史记账都在这一处
         * @param image 成品图（未装饰）
         * @return 成功返回存下的文件名；失败返回已带中文原因的 Error（调用方负责提示）
         * @details 「存成文件」「快速保存」与「自动保存」三条路共用：三条各写一遍落盘
         *          就迟早出现「这边记了历史那边没记」这类暗差
         */
        [[nodiscard]] Core::Expected<QString> writeImageToDisk(const QImage &image);

        /**
         * @brief 按设置里的两格给成品装边框与投影
         * @param image 成品图
         * @param fillBackground 目标端带不了透明通道时给真（存 JPG、打印到纸面）：投影底下垫白
         * @return 装好饰的图；两格都关时原样交回
         */
        [[nodiscard]] QImage decorateOutput(const QImage &image, bool fillBackground) const;

        /// 标注窗请求贴图
        void handlePinRequested(const QImage &image);
        /**
         * @brief 把剪贴板里的内容贴到光标那一处（托盘那一条的落点）
         * @details 剪贴板来的图没有「原来那块位置」，跟着鼠标落才是用户此刻的落点；
         *          剪贴板里是文件（资源管理器里复制的那一份）时走拖放那条内容转换，
         *          一次贴多件、动图连来源带走；取不到图时把原因与替代做法发给用户，不静默什么都不做
         */
        void pasteClipboardAsPin();

        /**
         * @brief 贴出一批之后的一句提示
         * @param outcome 贴出的张数与没贴成的文件名
         * @details 拖放落图与剪贴板贴图两处共用：贴上一部分也要说清另一半去哪了，
         *          静默丢掉一件比整次拒绝更让人困惑
         */
        void notifyPinnedDrop(const DropPinOutcome &outcome);
        /// 标注窗请求识别文字
        void handleOcrRequested(const QImage &image);

        /**
         * @brief 标注窗请求打印：开系统打印对话框，按纸面等比居中画上去
         * @param image 成品图（未装饰）
         * @details 用户取消对话框就静默收场——打印这件事「什么都没发生」也是一种正常结果。
         *          打到纸上按没有透明通道处理：投影底下垫白，不然透明区会按驱动作自己的解释落色。
         */
        void handlePrintRequested(const QImage &image);

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

        /**
         * @brief 用给定语言认一次当前这张图，并把结果摆进结果窗口
         * @param language 语言标签，如 zh-Hans
         * @details 首次识别与窗口里「换语言/重新识别」走同一条路：两条路各写一遍认字，
         *          就会出现一个能提示「后端没编进来」而另一个静默给空结果。
         */
        void recognizeWithLanguage(const QString &language);

        /**
         * @brief 工作线程认完之后把结果摆进结果窗口
         * @param image 请求时那一张，方框与它同系；期间用户重新截的图不算这一张
         * @param language 请求时那门语言，认成之后才记住它
         * @param result 识别结果，空结果也照常开窗
         * @details 与 handleOcrFailed 分两条槽而不是一条带 Expected：这里两个分支各写各的文案，
         *          合在一条里就要在槽里再判一次有没有值，而 Runner 已经判过了。
         */
        void handleOcrSucceeded(const QImage &image, const QString &language, const Ocr::OcrResult &result);

        /**
         * @brief 识别失败：把后端给的中文原因原样报给用户
         * @param error 后端给的故障类别与文案
         */
        void handleOcrFailed(const Core::Error &error);

        /// 给用户看的一条提示：有气泡就用气泡，没有（例如起不来托盘的那类环境）就落日志
        void notifyUser(const QString &message, GyanisScribe::Ui::ToastKind kind);

        /**
         * @brief 把一份成品记进历史清单并刷新历史页
         * @param filePath 成品文件路径
         * @param kind 成品类别
         * @param size 成品尺寸，视频记的是录屏区尺寸
         * @details 记不进清单只警告不报错：成品已经在盘上，把「保存成功」说成失败更糟。
         */
        void recordHistory(const std::filesystem::path &filePath, GyanisScribe::History::HistoryKind kind, const QSize &size);

        /**
         * @brief 从盘上读回一张图
         * @param filePath 图片路径
         * @return 图；读不出时返回写明原因的错误
         */
        [[nodiscard]] Core::Expected<QImage> loadImageFile(const std::filesystem::path &filePath) const;

        /// 历史页要求把一条成品重新贴回屏幕
        void handleHistoryPin(const std::filesystem::path &filePath);

        /// 对盘上的一张图做识别：历史页的「重新识别」与托盘的「识别图片文件…」共用这一份实现
        void recognizeImageFile(const std::filesystem::path &filePath);

        /// 先让用户挑一张盘上的图，再交给 recognizeImageFile；取消时什么都不做
        void pickImageFileAndRecognize();

        /// 历史页要求去掉一条记录（成品文件保留）
        void handleHistoryRemove(const std::filesystem::path &filePath);

        /**
         * @brief 在文件管理器里打开这条记录所在的那个目录
         * @param filePath 选中的那条记录的成品路径
         * @details 目录而不是文件本身：各家系统「选中并打开所在目录」的接口不一，而打开目录在所有平台上都做得到。
         */
        void handleHistoryOpenFolder(const std::filesystem::path &filePath);

        /// 清空历史清单：只清记录，成品文件留在各自目录里
        void handleHistoryClear();

        std::unique_ptr<Settings::SettingsStore>             m_settings;                                     ///< 配置存储
        std::unique_ptr<Platform::IPlatformIntegration>      m_integration;                                  ///< 平台集成
        std::unique_ptr<Capture::IScreenCaptureSource>       m_captureSource;                                ///< 抓帧来源
        std::unique_ptr<Capture::CaptureService>             m_capture;                                      ///< 截图编排
        std::unique_ptr<Hotkey::HotkeyManager>               m_hotkeys;                                      ///< 热键登记与触发
        std::unique_ptr<GyanisScribe::Ui::ThemeManager>      m_theme;                                        ///< 主题档位与样式表
        std::unique_ptr<GyanisScribe::Ui::IconProvider>      m_icons;                                        ///< 图标供给器，描边色跟主题
        std::unique_ptr<Annotation::AnnotationEngine>        m_annotation;                                   ///< 标注引擎，跨轮次保留（载入新帧会清空）
        std::unique_ptr<Annotation::AnnotationWindow>        m_annotationWindow;                             ///< 标注窗口，复用同一把工具栏
        std::unique_ptr<Pin::PinManager>                     m_pins;                                         ///< 贴图管理器
        std::unique_ptr<Ocr::IOcrEngine>                     m_ocr;                                          ///< 识别后端，按设置与平台能力选一次
        std::unique_ptr<Ocr::RecognitionRunner>              m_ocrRunner;                                    ///< 识别的线程搬运工
        std::unique_ptr<Record::FfmpegRecordingEncoder>      m_recorder;                                     ///< 录屏的编码步
        std::unique_ptr<Record::RecordingService>            m_recording;                                    ///< 录屏服务，一轮一个会话
        std::unique_ptr<TrayController>                      m_tray;                                         ///< 托盘图标与菜单
        std::unique_ptr<MainWindow>                          m_mainWindow;                                   ///< 设置与关于的入口
        std::unique_ptr<GyanisScribe::Ui::Toast>             m_toast;                                        ///< 右下角提示气泡，操作结果的落地处
        QImage                                               m_ocrImage;                                     ///< 最近一次送去识别的图，换语言重认按它算
        QPointer<Ocr::OcrResultWindow>                       m_ocrWindow;                                    ///< 识别结果窗口，关了就重建，不拥有
        std::unique_ptr<GyanisScribe::History::HistoryStore> m_history;                                      ///< 历史清单：成品文件的索引，删记录不删文件
        QPointer<Pick::ColorPickerOverlay>                   m_picker;                                       ///< 取色遮罩，关掉即重建
        QPointer<Record::RecordingHud>                       m_hud;                                          ///< 录屏控制条，一轮一个，收起即销毁
        QTimer                                              *m_hudTicker          = nullptr;                 ///< 控制条的每秒轮询，随一轮录屏起停
        int                                                  m_countdownRemaining = 0;                       ///< 倒计时剩余整秒，推给控制条的大字
        CaptureIntent                                        m_captureIntent      = CaptureIntent::Annotate; ///< 这一帧截完要去做的事
        bool                                                 m_commandLineActive  = false;   ///< 正在跑命令行那次截图：失败路径要往 commandLineActionFailed 也交一份
        QTimer                                              *m_captureDelayTimer  = nullptr; ///< 延时截图那一枚，一次只允许有一个在等
        std::function<void()>                                m_pendingCapture;               ///< 延时到点要走的那一步，随定时器一起活到触发
        Core::CapturedImage                                  m_lastCapture;                  ///< 本轮抓到的帧，贴图与识别要用它定位
        QRect                                                m_lastSelection;                ///< 本轮选区（全局逻辑坐标），标注窗与贴图的位置来源
    };

} // namespace GyanisScribe::App
