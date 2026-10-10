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
#include "App/TrayController.h"
#include "Ui/Toast.h"

#include <QObject>

#include <QImage>
#include <QPointer>
#include <QSize>

class QTimer;

#include <memory>

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
    class MainWindow;
    class TrayController;

    /**
     * @brief 一次截图要去做的事
     * @details 同一条抓帧链路服务两个入口：截完标注、截完直接识别。分开是因为
     *          识别那条不必先弹标注窗，多一步界面反而挡在用户与结果之间。
     */
    enum class CaptureIntent
    {
        Annotate,  ///< 截完进标注窗
        Recognize, ///< 截完直接识别文字
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
         * @param intent 截完之后做什么
         */
        void startCapture(CaptureIntent intent = CaptureIntent::Annotate);

        /**
         * @brief 整屏截图
         * @param intent 截完之后做什么
         */
        void startFullScreenCapture(CaptureIntent intent = CaptureIntent::Annotate);

        /**
         * @brief 重复上一次的选区截图
         * @param intent 截完之后做什么
         */
        void startLastRegionCapture(CaptureIntent intent = CaptureIntent::Annotate);

        /**
         * @brief 截取当前前台窗口
         * @details 句柄由平台集成给（Windows 走 GetForegroundWindow），抓帧那一步按句柄去问几何；
         *          平台给不出句柄时把原因发给用户，而不是静默什么都不做。
         */
        void captureForegroundWindow();

        /**
         * @brief 打开主窗口的某一页
         * @param page 目标页
         */
        void openMainWindow(MainWindowPage page);

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

    private:
        /**
         * @brief 抓到一帧之后的收尾：开启一轮标注
         * @param image 抓屏产物
         * @details 截完不再直接进剪贴板：标注窗的「复制」就是原来的那一步，只是多了先标一笔的机会
         */
        void handleCaptured(const Core::CapturedImage &image);

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

        /// 录屏结束：把成品路径落到日志
        void handleRecordingFinished(const QString &outputPath);

        /// 录屏中止：把原因落到日志
        void handleRecordingFailed(const QString &reason);
        /// 标注窗请求存文件
        void handleSaveRequested(const QImage &image);
        /// 标注窗请求贴图
        void handlePinRequested(const QImage &image);
        /// 标注窗请求识别文字
        void handleOcrRequested(const QImage &image);

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
         * @brief 起一层全屏取色遮罩
         * @details 抓的是按下那一刻的那一屏，之后遮罩显示的就是这一帧：放大镜要看的正是当前画面，
         *          实时抓屏会让鼠标一停画面就变，颜色也就抄不准。
         */
        void startColorPick();

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
        QSize                                                m_recordingSize;                                ///< 本轮录屏区尺寸，成品落地时记进历史
        QPointer<Record::RecordingHud>                       m_hud;                                          ///< 录屏控制条，一轮一个，收起即销毁
        QTimer                                              *m_hudTicker          = nullptr;                 ///< 控制条的每秒轮询，随一轮录屏起停
        int                                                  m_countdownRemaining = 0;                       ///< 倒计时剩余整秒，推给控制条的大字
        CaptureIntent                                        m_captureIntent      = CaptureIntent::Annotate; ///< 这一帧截完要去做的事
        Core::CapturedImage                                  m_lastCapture;                                  ///< 本轮抓到的帧，贴图与识别要用它定位
        QRect                                                m_lastSelection;                                ///< 本轮选区（全局逻辑坐标），标注窗与贴图的位置来源
    };

} // namespace GyanisScribe::App
