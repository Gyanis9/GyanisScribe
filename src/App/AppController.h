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

#include "App/MainWindow.h"
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

namespace GyanisScribe::Record
{
    class FfmpegRecordingEncoder;
    class RecordingHud;
    struct RecorderConfig;
    class RecordingService;
} // namespace GyanisScribe::Record

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
    struct DropPinOutcome;
    class MainWindow;
    class TrayController;

    /**
     * @brief 建控制器时可以换掉的几件底物
     * @details 只有测试用这两档：设置、平台集成、抓帧来源与历史清单落点是构造里唯一碰「本机环境」的根，
     *          其余模块都由它们派生。每一格留空就按生产那一份自建，两个公开构造因此汇在同一处装配。
     */
    struct AppControllerParts
    {
        std::unique_ptr<Core::SettingsStore>            settings;           ///< 配置存储；空 = 按便携标记自建
        std::unique_ptr<Platform::IPlatformIntegration> integration;        ///< 平台集成；空 = 本机那一份
        std::unique_ptr<Capture::IScreenCaptureSource>  captureSource;      ///< 抓帧来源；空 = 本机抓屏
        std::filesystem::path                           historyStoragePath; ///< 历史清单落点；空 = 默认位置
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
        [[nodiscard]] Base::Expected<Core::HotkeyReloadReport> registerHotkeys();

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
        void startCommandLineCapture(const CommandLineRequest &request);

        /**
         * @brief 命令行贴图：把盘上的图片贴到光标处
         * @param filePath 图片文件路径
         * @return 成功返回空载荷；文件不在、或不是能贴的图片时返回中文原因
         * @details 复用拖放那套内容转换：图片读像素、动图连来源带走、读不出的说清是哪一件
         */
        [[nodiscard]] Base::Expected<void> pinImageFile(const QString &filePath);

        /**
         * @brief 打开贴图组管理窗（托盘与主窗口的动作条都走这一条）
         * @details 窗已开着就把它抬到前面并重读一遍，不开第二扇：两扇表对着同一桌贴图，
         *          其中一扇按下去切了组，另一扇上那一列「摆在桌上」就成了旧账。
         */
        void openPinGroupManager();

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
        [[nodiscard]] Core::SettingsStore &settings() const noexcept;

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
        AppController(std::unique_ptr<Core::SettingsStore> settings, std::unique_ptr<Platform::IPlatformIntegration> integration,
                      std::unique_ptr<Capture::IScreenCaptureSource> captureSource, std::filesystem::path historyStoragePath, QObject *parent);

        /**
         * @brief 抓到一帧之后的收尾：开启一轮标注
         * @param image 抓屏产物
         * @details 截完不再直接进剪贴板：标注窗的「复制」就是原来的那一步，只是多了先标一笔的机会
         */
        void handleCaptured(const Base::CapturedImage &image);

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
         * @brief 截图成功后起跑那条外部命令（设置里那一格，或命令行 `--exec` 点名的那一条）
         * @details 只在「这一轮真的截到了一张屏上的图」之后跑：标注窗里再存一次、从历史重新贴出都不算一次截图，
         *          把这些也算进去会让一个上传脚本被同一次截图喂两遍。
         *          命令为空就什么都不做；跑不起来只报一句，不把截图本身判成失败。
         */
        void runAfterCaptureCommand();

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
        [[nodiscard]] Base::Expected<Record::RecorderConfig> makeRecordingConfig() const;

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
        [[nodiscard]] Base::Expected<QString> writeImageToDisk(const QImage &image);

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
         * @brief 把最近关掉的那张贴图重新按在光标处（托盘那一条）
         * @details 只有「关闭」过的按得回来，「销毁」过的不回；册子最多记 5 张，取一张少一张。
         *          空册也要回一句：这条入口摆在托盘上，不出声就成了「按了没反应」。
         */
        void repinLastClosedPin();
        /**
         * @brief 把某张贴图的内容换成剪贴板里那一份（贴图窗上按 `Ctrl+V` 的落点）
         * @param index 发起那一张在册表里的序号，由贴图管理器报上来
         * @details 内容转换与「把剪贴板贴到屏幕上」共用同一条判据（图像、纯文本卡片、颜色卡片三种都能换）；
         *          文件列表不在这一条里——替换只有一张目标，选哪一件都没有唯一答案。
         *          取不到内容或那张是动图时把原因发给用户，不动原来那一份。
         */
        void replacePinWithClipboard(int index);

        /**
         * @brief 把某张贴图此刻盖住的那块屏幕重抓一次，换掉它的画面（`F5` 与右键那一项的落点）
         * @param index 发起那一张在册表里的序号
         * @details 抓之前那一张要暂时从桌上拿走（贴图是置顶窗，不藏就会抓到贴图的自己），抓完摆回原样；
         *          摆位、缩放、旋转与透明度都不动。按住不放时键盘自动重复连着报，就成了持续刷新。
         */
        void refreshPinWithScreenUnder(int index);

        /**
         * @brief 整组「批量保存」落完账后那一句提示
         * @param report 写成几张、几张没写成，与第一条没写成的原因
         * @details 一次写走 N 份文件，不出声就成了「按了不知道存没存上」；一张都没写成时把原因原样报出来，
         *          那是唯一还能行动的那一句（改目录或改格式那一格）。
         */
        void reportBatchSave(const Gui::PinBatchSaveReport &report);

        /**
         * @brief 一句里用到的分组标签：起过名字就带上名字，没起过就是「分组 N」
         * @param group 组号
         * @return 界面用的一段文本
         */
        [[nodiscard]] QString groupLabel(int group) const;

        /**
         * @brief 改完组名那一句回声
         * @param group 哪一组
         * @param name 新名字；空串表示取消了命名
         */
        void reportGroupRenamed(int group, const QString &name);

        /**
         * @brief 删掉一组之后那句：并回去了几张，还是那组本来就空着
         * @param group 被删的组号
         * @param movedCount 并回第一组的张数
         */
        void reportGroupDeleted(int group, int movedCount);

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
        void handleOcrFailed(const Base::Error &error);

        /// 给用户看的一条提示：有气泡就用气泡，没有（例如起不来托盘的那类环境）就落日志
        void notifyUser(const QString &message, GyanisScribe::Gui::ToastKind kind);

        /**
         * @brief 去把一条图片地址取回来并贴到屏上
         * @param url 图片地址，调用方已用 `isFetchableImageUrl` 粗筛过
         * @param dropPoint 落点；空表示跟着光标走（剪贴板那条路没有「原来那块位置」）
         * @details 取图是异步的：这里只发起，成败都在 `ImageFetcher` 的两条信号上收口。
         */
        void startImageFetch(const QUrl &url, std::optional<QPoint> dropPoint);

        /**
         * @brief 抓一块点名要的屏
         * @param screenName 屏幕名
         * @details 走整屏截图同一条路（含延时那一档）：两处各写一份截全屏的流程，
         *          延时与成品去向那几档迟早只改一处。
         */
        void captureNamedScreen(const QString &screenName);

        /**
         * @brief 把一份成品记进历史清单并刷新历史页
         * @param filePath 成品文件路径
         * @param kind 成品类别
         * @param size 成品尺寸，视频记的是录屏区尺寸
         * @details 记不进清单只警告不报错：成品已经在盘上，把「保存成功」说成失败更糟。
         */
        void recordHistory(const std::filesystem::path &filePath, GyanisScribe::Core::HistoryKind kind, const QSize &size);

        /**
         * @brief 从盘上读回一张图
         * @param filePath 图片路径
         * @return 图；读不出时返回写明原因的错误
         */
        [[nodiscard]] Base::Expected<QImage> loadImageFile(const std::filesystem::path &filePath) const;

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

        std::unique_ptr<Core::SettingsStore>              m_settings;                                     ///< 配置存储
        std::unique_ptr<Platform::IPlatformIntegration>   m_integration;                                  ///< 平台集成
        std::unique_ptr<Capture::IScreenCaptureSource>    m_captureSource;                                ///< 抓帧来源
        std::unique_ptr<Capture::CaptureService>          m_capture;                                      ///< 截图编排
        std::unique_ptr<Core::HotkeyManager>              m_hotkeys;                                      ///< 热键登记与触发
        std::unique_ptr<GyanisScribe::Gui::ThemeManager>  m_theme;                                        ///< 主题档位与样式表
        std::unique_ptr<GyanisScribe::Gui::IconProvider>  m_icons;                                        ///< 图标供给器，描边色跟主题
        std::unique_ptr<Annotation::AnnotationEngine>     m_annotation;                                   ///< 标注引擎，跨轮次保留（载入新帧会清空）
        std::unique_ptr<Annotation::AnnotationWindow>     m_annotationWindow;                             ///< 标注窗口，复用同一把工具栏
        std::unique_ptr<Gui::PinManager>                  m_pins;                                         ///< 贴图管理器
        std::unique_ptr<ImageFetcher>                     m_imageFetcher;                                 ///< 网页取图：拖进来的可以是一条图片地址
        std::optional<QPoint>                             m_pendingFetchPoint;                            ///< 在飞那一张取回来落在哪儿；空表示跟光标走
        std::unique_ptr<Ocr::IOcrEngine>                  m_ocr;                                          ///< 识别后端，按设置与平台能力选一次
        std::unique_ptr<Ocr::RecognitionRunner>           m_ocrRunner;                                    ///< 识别的线程搬运工
        std::unique_ptr<Record::FfmpegRecordingEncoder>   m_recorder;                                     ///< 录屏的编码步
        std::unique_ptr<Record::RecordingService>         m_recording;                                    ///< 录屏服务，一轮一个会话
        std::unique_ptr<TrayController>                   m_tray;                                         ///< 托盘图标与菜单
        std::unique_ptr<MainWindow>                       m_mainWindow;                                   ///< 设置与关于的入口
        std::unique_ptr<GyanisScribe::Gui::Toast>         m_toast;                                        ///< 右下角提示气泡，操作结果的落地处
        QImage                                            m_ocrImage;                                     ///< 最近一次送去识别的图，换语言重认按它算
        QPointer<Ocr::OcrResultWindow>                    m_ocrWindow;                                    ///< 识别结果窗口，关了就重建，不拥有
        std::unique_ptr<GyanisScribe::Core::HistoryStore> m_history;                                      ///< 历史清单：成品文件的索引，删记录不删文件
        QPointer<Gui::ColorPickerOverlay>                 m_picker;                                       ///< 取色遮罩，关掉即重建
        QPointer<Gui::PinGroupManagerWindow>              m_groupManager;                                 ///< 贴图组管理窗，开着一扇就复用那一扇
        QPointer<Record::RecordingHud>                    m_hud;                                          ///< 录屏控制条，一轮一个，收起即销毁
        QTimer                                           *m_hudTicker          = nullptr;                 ///< 控制条的每秒轮询，随一轮录屏起停
        int                                               m_countdownRemaining = 0;                       ///< 倒计时剩余整秒，推给控制条的大字
        CaptureIntent                                     m_captureIntent      = CaptureIntent::Annotate; ///< 这一帧截完要去做的事
        CaptureOutputSet                                  m_commandLineOutputs;                           ///< 命令行那一次点名的去处，已按交付顺序排好
        QString                                           m_commandLineExec;              ///< --exec 写的那一条命令；只在 m_commandLineExecGiven 为真时压过设置里那一格
        bool                                              m_commandLineExecGiven = false; ///< 命令行到底给没给 --exec：给了就只跑那一条，不与设置里的命令叠着跑两次
        QString                                           m_writtenFilePath;              ///< 这一轮落盘的成品路径（显示文本）；没落盘时为空，外部命令的 %file% 用它填
        bool                                              m_commandLineActive = false;    ///< 正在跑命令行那次截图：失败路径要往 commandLineActionFailed 也交一份
        QTimer                                           *m_captureDelayTimer = nullptr;  ///< 延时截图那一枚，一次只允许有一个在等
        std::function<void()>                             m_pendingCapture;               ///< 延时到点要走的那一步，随定时器一起活到触发
        Base::CapturedImage                               m_lastCapture;                  ///< 本轮抓到的帧，贴图与识别要用它定位
        QRect                                             m_lastSelection;                ///< 本轮选区（全局逻辑坐标），标注窗与贴图的位置来源
    };

} // namespace GyanisScribe::App
