/**
 * @file CaptureCoordinator.h
 * @brief 截图这一族（枢纽）从控制器里搬出来的落脚处（审查台账 A-11）
 * @author Gyanis
 * @date 2026-10-16
 * @version 0.0.1
 * @copyright Copyright (c) . All rights reserved.
 */

#pragma once

#include "App/CaptureIntent.h"
#include "App/CommandLine.h"
#include "Base/Types.h"
#include "Gui/Controls/Toast.h"

#include <QObject>
#include <QPoint>
#include <QRect>
#include <QSize>
#include <QString>

#include <filesystem>
#include <functional>
#include <memory>
#include <optional>

class QScreen;
class QTimer;

namespace GyanisScribe::Core
{
    class AnnotationEngine;
    class IScreenCaptureSource;
    class ISettingsStore;
} // namespace GyanisScribe::Core

namespace GyanisScribe::Gui
{
    class AnnotationWindow;
    class CaptureService;
    class IconProvider;
    class ThemeManager;
} // namespace GyanisScribe::Gui

namespace GyanisScribe::Platform
{
    class IPlatformIntegration;
} // namespace GyanisScribe::Platform

namespace GyanisScribe::App
{
    class DeliveryCoordinator;
    class HistoryCoordinator;
    class OcrCoordinator;
    class PinCoordinator;
} // namespace GyanisScribe::App

namespace GyanisScribe::App
{

    /**
     * @brief 截图这一族：入口 → 延时 → 抓帧 → 按意图交付
     * @details 它是枢纽——交付那一跳要往识别、贴图、标注、落盘四处去，所以依赖在构造时一次给全
     *          （`Dependencies` 是本仓既有形状，AppControllerParts 同款），不各自去摸全局。
     *          要出给用户的话走 messageForUser；命令行那次截图失败还要往 commandLineActionFailed
     *          另交一份（那条路径没有界面可看提示，退出码靠它）。
     */
    class CaptureCoordinator : public QObject
    {
        Q_OBJECT

    public:
        /// 构造依赖：枢纽要往四个方向去，缺一条就没有对应的交付路
        struct Dependencies
        {
            GyanisScribe::Core::ISettingsStore           &settings;      ///< 延时秒数、选区历史、快捷入口那几档
            GyanisScribe::Platform::IPlatformIntegration &integration;   ///< 前台窗口与能力位
            GyanisScribe::Gui::ThemeManager              &theme;         ///< 标注窗的主题
            GyanisScribe::Gui::IconProvider              &icons;         ///< 标注窗的图标
            GyanisScribe::Core::IScreenCaptureSource     &captureSource; ///< 抓帧来源
            GyanisScribe::App::OcrCoordinator            &ocr;           ///< 交付到识别
            GyanisScribe::App::PinCoordinator            &pins;          ///< 交付到贴图
            GyanisScribe::App::DeliveryCoordinator       &delivery;      ///< 交付到落盘与外部命令
            GyanisScribe::App::HistoryCoordinator        &history;       ///< 未保存截图那一路也要进历史清单
        };

        /**
         * @brief 构造：把截图服务、标注引擎与标注窗一并建起来（三者同一条命）
         * @param dependencies 依赖集
         * @param parent Qt 父对象
         */
        CaptureCoordinator(Dependencies dependencies, QObject *parent = nullptr);

        /**
         * @brief 析构
         * @note 声明在头、定义在 .cpp：三个成员的唯一指针各指着一个只前向声明的类型
         *       （标注引擎、标注窗、截图服务），析构要在类型完整的 TU 里实例化——
         *       否则 moc 合并 TU（只收头）里内联析构会让严格档的 MSVC 报
         *       「can't delete an incomplete type」（本地 14.51 放过、CI 的 14.44 不放过）。
         */
        ~CaptureCoordinator() override;

        /// 框选截图（热键、托盘、标注窗的「重新截」都从这里进）
        void startInteractive(std::optional<GyanisScribe::App::CaptureIntent> intent = std::nullopt);

        /// 整屏截图
        void startFullScreen(std::optional<GyanisScribe::App::CaptureIntent> intent = std::nullopt);

        /// 重复上次选区截图
        void startLastRegion(std::optional<GyanisScribe::App::CaptureIntent> intent = std::nullopt);

        /// 滚动长截图：入口只做「去框那一条区域」，接与收口都在截图服务里
        void startLong();

        /// 空白画布：不进截图，直接把标注窗开在一张白底上
        void startBlankCanvas();

        /// 截前台窗口所在的那块屏（窗口本身由标注窗的候选框住）
        void captureForegroundWindow();

        /// 命令行那一次截图：记下这一轮点名的去处，走同一条抓帧路
        void startCommandLine(const GyanisScribe::App::CommandLineRequest &request);

        /// 按当前屏数抓一次并交回（不弹遮罩，供命令行与「上次区域」那几条路用）
        void captureScreen(const QString &screenName);

        /**
         * @brief 按设置的延时起一轮截图
         * @param begin 到点要走的那一步
         * @details 一次只允许有一个在等：再按一次保留先按的那一个（用户的意图是「我刚按的那次」，
         *          换掉会把他等的那个也撤了）。
         */
        void schedule(std::function<void()> begin);

        /// 抓一块区域就现在（贴图那条 F5 用的口子）：不弹遮罩、不走意图
        [[nodiscard]] GyanisScribe::Base::Expected<GyanisScribe::Base::CapturedImage> grabRegionNow(const QRect &logicalRegion, const QString &screenName);

    signals:
        /// 要说给用户的一句话（由控制器决定用气泡还是落日志）
        void messageForUser(const QString &message, GyanisScribe::Gui::ToastKind kind);
        /// 命令行那次截图失败：那条路径没有界面可看提示，退出码靠它
        void commandLineActionFailed(const QString &reason);
        /// 命令行那一次交付完了：成品路径交回控制器（它才知道要不要打到标准输出）
        void commandLineCaptureDelivered(const QString &filePath);

    private:
        /// 「截完之后」那一档从设置现读：读不出按默认档走并把原因落进日志
        [[nodiscard]] GyanisScribe::App::CaptureIntent settingsCaptureIntent() const;

        /// 抓帧回来：记下这一轮，再按意图交付
        void handleCaptured(const GyanisScribe::Base::CapturedImage &image);

        /// 按这一轮点名的意图把成品交出去（识别/剪贴板/存文件/快速保存/贴到屏幕/标注）
        void deliverFrame(const QImage &image, QScreen &screen);

        /// 命令行那一条路按点名的去处依次交付（先盘上那份、再剪贴板、最后贴到屏幕）
        void deliverCommandLineOutputs();

        /// 标注窗请求复制（同一套装饰口径：剪贴板带得了透明通道，不垫白）
        void copyToClipboard(const QImage &image);

        /// 标注窗请求打印：开系统打印对话框，按纸面等比居中画上去
        void printImage(const QImage &image);

        /// 标注窗请求识别二维码/条形码：认出来就复制到剪贴板并报一声
        void scanImage(const QImage &image);

        /// 标注窗请求刷新截图（F5）：重抓这块区域换底图，标注留着
        void refreshLastCapture(const QString &screenName, const QRect &region);

        Dependencies                                          m_dependencies;
        std::unique_ptr<GyanisScribe::Gui::CaptureService>    m_capture;                                             ///< 截图编排
        std::unique_ptr<GyanisScribe::Core::AnnotationEngine> m_annotation;                                          ///< 标注引擎，跨轮次保留（载入新帧会清空）
        std::unique_ptr<GyanisScribe::Gui::AnnotationWindow>  m_annotationWindow;                                    ///< 标注窗口，复用同一把工具栏
        GyanisScribe::App::CaptureIntent                      m_intent = GyanisScribe::App::CaptureIntent::Annotate; ///< 这一帧截完要去做的事
        GyanisScribe::App::CaptureOutputSet                   m_commandLineOutputs;                                  ///< 命令行那一次点名的去处，已按交付顺序排好
        QString                                               m_commandLineExec;                ///< --exec 写的那一条命令；只在 m_commandLineExecGiven 为真时压过设置里那一格
        bool                                                  m_commandLineExecGiven = false;   ///< 命令行到底给没给 --exec：给了就只跑那一条
        bool                                                  m_commandLineActive    = false;   ///< 正在跑命令行那次截图：失败路径要往 commandLineActionFailed 也交一份
        QTimer                                               *m_delayTimer           = nullptr; ///< 延时截图那一枚，一次只允许有一个在等
        std::function<void()>                                 m_pendingCapture;                 ///< 延时到点要走的那一步，随定时器一起活到触发
        GyanisScribe::Base::CapturedImage                     m_lastCapture;                    ///< 本轮抓到的帧，贴图与识别要用它定位
        QRect                                                 m_lastSelection;                  ///< 本轮选区（全局逻辑坐标），标注窗与贴图的位置来源
    };

} // namespace GyanisScribe::App
