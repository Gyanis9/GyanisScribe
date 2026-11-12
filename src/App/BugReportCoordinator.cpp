#include "App/BugReportCoordinator.h"

#include "App/PrivacyCoordinator.h"
#include "Base/Logger.h"
#include "Base/Paths.h"
#include "Base/Text.h"
#include "Core/Ocr/IOcrEngine.h"
#include "Core/Privacy/PiiDetector.h"
#include "Core/Privacy/RedactionGeometry.h"
#include "Core/Settings/ISettingsStore.h"
#include "Core/Settings/SettingsKeys.h"
#include "Platform/IPlatformIntegration.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QGuiApplication>
#include <QSysInfo>
#include <QWidget>

#include <format>
#include <utility>

namespace GyanisScribe::App
{

    BugReportCoordinator::BugReportCoordinator(GyanisScribe::Core::ISettingsStore &settings, GyanisScribe::Platform::IPlatformIntegration &integration, QObject *parent) :
        QObject(parent), m_settings(settings), m_integration(&integration), m_barcodeReader([](const QImage &image) { return GyanisScribe::Plugins::readBarcodes(image); })
    {
    }

    BugReportCoordinator::BugReportCoordinator(GyanisScribe::Core::ISettingsStore &settings, std::unique_ptr<GyanisScribe::Core::IOcrEngine> engine, BarcodeReader barcodeReader,
                                               QObject *parent) :
        QObject(parent), m_settings(settings), m_injectedEngine(std::move(engine)), m_barcodeReader(std::move(barcodeReader))
    {
    }

    BugReportCoordinator::~BugReportCoordinator() = default;

    void BugReportCoordinator::openReport(QImage reportImage, QImage detectionImage, GyanisScribe::Core::BugReport::BugReportCaptureContext context, QWidget *parent)
    {
        // 全链只留一扇窗：第二次打开抬起来就是，不换内容也不重扫
        if (m_dialog)
        {
            m_dialog->show();
            m_dialog->raise();
            m_dialog->activateWindow();
            return;
        }

        // 没有成品可写就没有报告：留空比开一扇只能写文字的空窗更省事
        if (reportImage.isNull())
        {
            return;
        }

        // 先把成品尺寸留住：图交给对话框后，判底图能不能用只认这个读数
        const auto reportSize = reportImage.size();

        GyanisScribe::Core::BugReport::BugReportEnvironment environment;
        environment.appName          = QGuiApplication::applicationDisplayName().isEmpty() ? QCoreApplication::applicationName() : QGuiApplication::applicationDisplayName();
        environment.appVersion       = QCoreApplication::applicationVersion();
        environment.osName           = QSysInfo::prettyProductName();
        environment.osVersion        = QSysInfo::kernelVersion();
        environment.architecture     = QSysInfo::currentCpuArchitecture();
        environment.qtVersion        = QString::fromLatin1(qVersion());
        environment.screenName       = context.screenName;
        environment.logicalSize      = context.logicalSize;
        environment.physicalSize     = reportSize;
        environment.devicePixelRatio = context.devicePixelRatio > 0.0 ? context.devicePixelRatio : reportImage.devicePixelRatio();
        environment.capturedAt       = context.capturedAt;
        environment.windowTitle      = context.windowTitle;
        environment.processName      = context.processName;

        // 来源没带倍比时取图自己的读数；两处都缺就按 1.0 算，免得后面除零与逻辑尺寸算成空
        if (environment.devicePixelRatio <= 0.0)
        {
            environment.devicePixelRatio = 1.0;
        }
        if (environment.logicalSize.isEmpty())
        {
            environment.logicalSize =
                    QSize{qMax(1, qRound(reportImage.width() / environment.devicePixelRatio)), qMax(1, qRound(reportImage.height() / environment.devicePixelRatio))};
        }

        const auto configured = m_settings.stringValue(GyanisScribe::Core::kPathScreenshot, QString{});
        const auto directory  = configured.isEmpty() ? GyanisScribe::Base::Paths::defaultScreenshotDirectory() : GyanisScribe::Base::Paths::fromDisplayText(configured);
        const auto suggested  = GyanisScribe::Core::BugReport::bugReportDefaultPath(directory, QDateTime::currentDateTime());

        // 码框扩张要按逻辑尺寸折算：在这一刻定格，之后不再借对话框的环境读数
        m_scanLogicalSize = environment.logicalSize;

        auto *dialog = new GyanisScribe::Gui::BugReportDialog{std::move(reportImage), environment, suggested, parent};
        // 关掉就自毁，协调器只持 QPointer：下一次打开会重新建一扇，迟到的结果也不会写进已析构的对象
        dialog->setAttribute(Qt::WA_DeleteOnClose);
        m_dialog = dialog;
        QObject::connect(dialog, &QObject::destroyed, this,
                         [this]
                         {
                             m_dialog          = nullptr;
                             m_dialogKey       = 0;
                             m_queuedDetection = QImage{};
                             m_scanLogicalSize = QSize{};
                         });
        dialog->show();

        if (detectionImage.isNull())
        {
            // 没有检查底图：入口那边漏传了，不是尺寸问题，别让用户去查一个不存在的差异
            m_dialogKey       = 0;
            m_queuedDetection = QImage{};
            dialog->setScanUnavailable(GyanisScribe::Base::uiText("没有可用于检查的底图，无法自动打码；请自行检查后再分享"));
            return;
        }

        if (detectionImage.size() != reportSize)
        {
            // 识别框按检测图算，尺寸对不上就整体错位：不猜坐标，直接交给用户自己检查
            // 拒收时连钥匙与排队一起清掉：上一扇窗还在飞的同一张图回来时不该落进这扇只能自查的窗
            m_dialogKey       = 0;
            m_queuedDetection = QImage{};
            dialog->setScanUnavailable(GyanisScribe::Base::uiText("截图与检查底图尺寸不一致，无法自动打码；请自行检查后再分享"));
            return;
        }

        // 底图确认可用之后才落钥匙：拒收的窗不能替旧图担保
        m_dialogKey = static_cast<quint64>(detectionImage.cacheKey());
        ensureScanner();
        if (m_scanner->isScanning())
        {
            // 单飞：扫描器不排队，要换的那张图由这里攒着，等它空下来补跑
            // 正在扫的就是同一张时不再攒：它回来会按钥匙落进这扇窗，重排只会让同一帧跑两遍
            if (static_cast<quint64>(detectionImage.cacheKey()) != m_inFlightKey)
            {
                m_queuedDetection = detectionImage;
            }
        } else
        {
            startScan(detectionImage);
        }
    }

    GyanisScribe::Gui::BugReportDialog *BugReportCoordinator::dialogForTesting() const noexcept
    {
        return m_dialog.data();
    }

    bool BugReportCoordinator::isScanningForTesting() const noexcept
    {
        return (m_scanner != nullptr && m_scanner->isScanning()) || m_inFlightKey != 0;
    }

    void BugReportCoordinator::ensureScanner()
    {
        if (m_scanner)
        {
            return;
        }

        if (m_injectedEngine)
        {
            m_scanner = std::make_unique<PrivacyCoordinator>(m_settings, std::move(m_injectedEngine));
        } else if (m_integration != nullptr)
        {
            m_scanner = std::make_unique<PrivacyCoordinator>(m_settings, *m_integration);
        } else
        {
            // 注入构造两者都没给：按空引擎处理，由扫描器给出「后端不可用」的可操作原因
            m_scanner = std::make_unique<PrivacyCoordinator>(m_settings, std::unique_ptr<GyanisScribe::Core::IOcrEngine>{});
        }

        // 不接 messageForUser：这条链的检查状态只落在报告窗里，再弹全局气泡反而盖住窗口
        QObject::connect(m_scanner.get(), &PrivacyCoordinator::scanSucceeded, this, &BugReportCoordinator::handleScanSucceeded);
        QObject::connect(m_scanner.get(), &PrivacyCoordinator::scanFailed, this, &BugReportCoordinator::handleScanFailed);
    }

    void BugReportCoordinator::startScan(const QImage &image)
    {
        // 结果回来时只凭 cacheKey 认这张图：同尺寸的另一张截图也必须算过期
        m_inFlightKey = static_cast<quint64>(image.cacheKey());
        m_scanner->requestScan(image);
    }

    void BugReportCoordinator::startQueuedScan()
    {
        if (!m_dialog || m_queuedDetection.isNull() || (m_scanner != nullptr && m_scanner->isScanning()))
        {
            return;
        }

        const QImage next = std::exchange(m_queuedDetection, QImage{});
        startScan(next);
    }

    void BugReportCoordinator::handleScanSucceeded(const QImage &image, const GyanisScribe::Core::OcrResult &result)
    {
        const auto key     = static_cast<quint64>(image.cacheKey());
        const bool wasLive = key == m_inFlightKey;
        m_inFlightKey      = 0;

        // 两道对号：这次结果确实是刚发出的那次，且它属于眼前这扇窗；关窗后迟到的结果落不进来
        if (m_dialog && wasLive && key == m_dialogKey)
        {
            std::vector<QRect> boxes;
            if (m_barcodeReader)
            {
                if (const auto readings = m_barcodeReader(image); readings)
                {
                    boxes.reserve(readings->size());
                    for (const auto &reading: *readings)
                    {
                        const QRect box = reading.matrix ? reading.box.normalized().intersected(image.rect())
                                                         : GyanisScribe::Core::Privacy::expandBarcodeScanline(reading.box, image.size(), m_scanLogicalSize);
                        if (!box.isEmpty())
                        {
                            boxes.push_back(box);
                        }
                    }
                } else
                {
                    // 码识别后端不在场不该拖住文本检测：记下原因，清单里不出现二维码项
                    GyanisScribe::Base::Logger::warning(std::format("Bug 报告里的二维码识别已跳过：{}", readings.error().message));
                }
            }

            m_dialog->setScanResult(GyanisScribe::Core::Privacy::detectPii(result, boxes, image.size()));
        }

        startQueuedScan();
    }

    void BugReportCoordinator::handleScanFailed(const GyanisScribe::Base::Error &error)
    {
        const auto failedKey = m_inFlightKey;
        m_inFlightKey        = 0;

        // 只有失败的就是当前窗正在等的那一张才置不可用：旧图失败不能污染新窗
        if (m_dialog && failedKey != 0 && failedKey == m_dialogKey)
        {
            m_dialog->setScanUnavailable(QString::fromStdString(error.message));
        }

        startQueuedScan();
    }

} // namespace GyanisScribe::App
