/**
 * @file BugReportCoordinator.h
 * @brief Bug 报告一条链的协作对象：组装环境与建议路径、开一扇报告窗、把本地隐私检查结果推进去
 * @author Gyanis
 * @date 2026-11-12
 * @version 0.0.1
 * @copyright Copyright (c) . All rights reserved.
 */

#pragma once

#include "Base/Types.h"
#include "Core/BugReport/BugReport.h"
#include "Gui/BugReport/BugReportDialog.h"
#include "Plugins/Barcode/BarcodeDecoder.h"

#include <QImage>
#include <QObject>
#include <QPointer>
#include <QSize>

#include <functional>
#include <memory>
#include <vector>

class QWidget;

namespace GyanisScribe::Core
{
    class IOcrEngine;
    class ISettingsStore;
    struct OcrResult;
} // namespace GyanisScribe::Core

namespace GyanisScribe::Platform
{
    class IPlatformIntegration;
} // namespace GyanisScribe::Platform

namespace GyanisScribe::App
{
    class PrivacyCoordinator;

    /**
     * @brief 报告窗与自动打码的协作对象
     * @details 报告窗自己不做识别：本类把截图来源组装成报告里的环境，懒建一个隐私扫描器，
     *          把识别结果与码框交给检测器，再把命中项推进对话框。全链只开一扇窗，
     *          再次打开时就地抬起；扫描结果按请求时那张图的 cacheKey 对号入座，
     *          关掉旧窗之后迟到的结果不会落进新窗。
     *          不接扫描器的 messageForUser：检查状态只落在报告窗里，不再额外弹全局气泡。
     */
    class BugReportCoordinator : public QObject
    {
        Q_OBJECT

    public:
        /// 码识别函数：成功返回认出的码列表，空列表表示图里没有码
        using BarcodeReader = std::function<GyanisScribe::Base::Expected<std::vector<GyanisScribe::Plugins::BarcodeReading>>(const QImage &)>;

        /**
         * @brief 生产构造：报告窗按设置与平台能力选识别后端
         * @param settings 配置来源，读截图输出目录与识别语言
         * @param integration 平台能力来源，后端选型用
         * @param parent Qt 父对象
         */
        BugReportCoordinator(GyanisScribe::Core::ISettingsStore &settings, GyanisScribe::Platform::IPlatformIntegration &integration, QObject *parent = nullptr);

        /**
         * @brief 测试注入构造：跳过后端工厂，并换掉码识别
         * @param settings 配置来源，读截图输出目录与识别语言
         * @param engine 识别后端，所有权移入本对象；为空表示这条链没有识别后端，检查结果按不可用推进
         * @param barcodeReader 码识别函数；为空表示这条链不找码
         * @param parent Qt 父对象
         */
        BugReportCoordinator(GyanisScribe::Core::ISettingsStore &settings, std::unique_ptr<GyanisScribe::Core::IOcrEngine> engine, BarcodeReader barcodeReader,
                             QObject *parent = nullptr);

        /**
         * @brief 析构
         * @note 声明在头、定义在 .cpp：扫描器与引擎只前向声明，内联析构会在别的 TU 里对不完整类型求 sizeof
         */
        ~BugReportCoordinator() override;

        /**
         * @brief 用一张截图开 Bug 报告
         * @param reportImage 将随报告保存的截图成品，物理像素
         * @param detectionImage 用来跑识别与码检测的同一帧；与成品尺寸必须一致，否则自动打码按不可用处理
         * @param context 截图的来源信息，写进报告的环境与窗口段
         * @param parent 对话框的父窗口
         * @details 已有一扇报告窗时只抬起它，不换内容也不重扫；首次打开立刻显示，
         *          扫描在后台跑，不阻塞调用方。
         */
        void openReport(QImage reportImage, QImage detectionImage, GyanisScribe::Core::BugReport::BugReportCaptureContext context, QWidget *parent);

        /**
         * @brief 取当前报告窗
         * @return 当前窗口；没开过或已关闭时为空
         */
        [[nodiscard]] GyanisScribe::Gui::BugReportDialog *dialogForTesting() const noexcept;

        /**
         * @brief 当前有没有一次扫描在跑或排着
         * @return true 表示识别还没回来，或已排进队列
         */
        [[nodiscard]] bool isScanningForTesting() const noexcept;

    private:
        /// 第一次扫描前把隐私扫描器建起来：引擎按注入或平台能力选一次
        void ensureScanner();

        /// 把一张检测图交给扫描器，并记下请求时它的 cacheKey
        void startScan(const QImage &image);

        /// 扫描器空下来后补跑最新的排队检测图
        void startQueuedScan();

        /**
         * @brief 一次扫描成功：补上码框后交给检测器，命中项只在还是当前窗的那张图时推进对话框
         * @param image 请求时那一张，方框与它同系
         * @param result 识别结果，一行都没认出来也照常走检测
         */
        void handleScanSucceeded(const QImage &image, const GyanisScribe::Core::OcrResult &result);

        /**
         * @brief 一次扫描失败：只有当前窗正在等的那张图失败才置不可用
         * @param error 后端给的故障类别与中文原因
         */
        void handleScanFailed(const GyanisScribe::Base::Error &error);

        GyanisScribe::Core::ISettingsStore                    &m_settings;              ///< 配置来源，不拥有
        GyanisScribe::Platform::IPlatformIntegration          *m_integration = nullptr; ///< 平台能力来源，不拥有
        std::unique_ptr<GyanisScribe::Core::IOcrEngine>        m_injectedEngine;        ///< 用例注入的识别后端；生产路径为空
        BarcodeReader                                          m_barcodeReader;         ///< 码识别函数，生产路径指向 Plugins::readBarcodes
        std::unique_ptr<GyanisScribe::App::PrivacyCoordinator> m_scanner;               ///< 懒建的隐私扫描器
        QPointer<GyanisScribe::Gui::BugReportDialog>           m_dialog;                ///< 当前报告窗，关掉即空
        QImage                                                 m_queuedDetection;       ///< 扫描器忙时攒下的最新检测图
        QSize                                                  m_scanLogicalSize;       ///< 本轮检查对应的逻辑尺寸，码框扩张用
        quint64                                                m_dialogKey   = 0;       ///< 当前窗等的结果所属图的 cacheKey
        quint64                                                m_inFlightKey = 0;       ///< 正在扫的那张图的 cacheKey
    };

} // namespace GyanisScribe::App
