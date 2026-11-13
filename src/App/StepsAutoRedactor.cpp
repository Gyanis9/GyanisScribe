#include "App/StepsAutoRedactor.h"

#include "App/PrivacyCoordinator.h"
#include "Base/Logger.h"
#include "Core/Ocr/IOcrEngine.h"
#include "Core/Privacy/PiiDetector.h"
#include "Core/Privacy/RedactImage.h"
#include "Core/Privacy/RedactionGeometry.h"
#include "Plugins/Barcode/BarcodeDecoder.h"

#include <QImage>
#include <QSaveFile>
#include <QString>

#include <format>
#include <utility>

namespace GyanisScribe::App
{
    StepsAutoRedactor::StepsAutoRedactor(GyanisScribe::Core::ISettingsStore &settings, GyanisScribe::Platform::IPlatformIntegration &integration, QObject *parent) :
        QObject(parent), m_settings(settings), m_integration(&integration)
    {
    }

    StepsAutoRedactor::StepsAutoRedactor(GyanisScribe::Core::ISettingsStore &settings, std::unique_ptr<GyanisScribe::Core::IOcrEngine> engine, QObject *parent) :
        QObject(parent), m_settings(settings), m_injectedEngine(std::move(engine))
    {
    }

    StepsAutoRedactor::~StepsAutoRedactor() = default;

    bool StepsAutoRedactor::isRunning() const noexcept
    {
        return m_running;
    }

    void StepsAutoRedactor::run(std::vector<std::filesystem::path> images, std::function<void(int, int, int)> done)
    {
        if (m_running)
        {
            // 一批没跑完不可能再停一次录制：真发生了就当场收尾，不让调用方永远等那扇开不出来的审核窗
            if (done)
            {
                done(static_cast<int>(m_images.size()), m_redacted, m_failed);
            }
            return;
        }

        m_images      = std::move(images);
        m_next        = 0;
        m_redacted    = 0;
        m_failed      = 0;
        m_inFlightKey = 0;
        m_done        = std::move(done);

        if (!m_scanner)
        {
            if (m_injectedEngine)
            {
                m_scanner = std::make_unique<PrivacyCoordinator>(m_settings, std::move(m_injectedEngine));
            } else
            {
                m_scanner = std::make_unique<PrivacyCoordinator>(m_settings, *m_integration);
            }
            // 结果按发出去那张图的 cacheKey 认领：换了一张就说明这条结果不属于手上这一张
            QObject::connect(m_scanner.get(), &PrivacyCoordinator::scanSucceeded, this,
                             [this](const QImage &image, const GyanisScribe::Core::OcrResult &result) { handleOne(image, result); });
            QObject::connect(m_scanner.get(), &PrivacyCoordinator::scanFailed, this,
                             [this](const GyanisScribe::Base::Error &error)
                             {
                                 GyanisScribe::Base::Logger::warning(std::format("步骤截图检查失败：{}", error.message));
                                 m_inFlightKey = 0;
                                 ++m_failed;
                                 startNext();
                             });
            // 一张一条「正在检查」会把这批图刷成一片气泡：过程话降级到诊断日志，结果仍由收尾回调交回
            QObject::connect(m_scanner.get(), &PrivacyCoordinator::messageForUser, this, [](const QString &message, GyanisScribe::Gui::ToastKind)
                             { GyanisScribe::Base::Logger::log(GyanisScribe::Base::Severity::Debug, message.toStdString()); });
        }

        m_running = true;
        startNext();
    }

    void StepsAutoRedactor::startNext()
    {
        if (m_next >= m_images.size())
        {
            m_running = false;
            if (m_done)
            {
                m_done(static_cast<int>(m_images.size()), m_redacted, m_failed);
            }
            return;
        }

        const auto  &path  = m_images[m_next++];
        const QImage image = QImage{QString::fromStdWString(path.wstring())};
        if (image.isNull())
        {
            // 零字节或坏掉的 PNG 读出来是空图：记失败就够了，原文件一个字节都不碰
            GyanisScribe::Base::Logger::warning(std::format("步骤截图读不出来，原样保留：{}", QString::fromStdWString(path.wstring()).toStdString()));
            ++m_failed;
            startNext();
            return;
        }

        m_inFlightKey = image.cacheKey();
        m_scanner->requestScan(image);
    }

    void StepsAutoRedactor::handleOne(const QImage &image, const GyanisScribe::Core::OcrResult &result)
    {
        if (image.cacheKey() != m_inFlightKey)
        {
            return; // 不是这一张的结果
        }
        m_inFlightKey = 0;

        // 与画布那条同一套码框：二维码取外接框，一维码把扫描线撑到最小高度；
        // 这里没有画布，逻辑尺寸就取图自己的像素尺寸，两种框都按 1:1 折算
        std::vector<QRect> boxes;
        if (const auto readings = GyanisScribe::Plugins::readBarcodes(image); readings)
        {
            boxes.reserve(readings->size());
            for (const auto &reading: *readings)
            {
                const QRect box = reading.matrix ? reading.box.normalized().intersected(image.rect())
                                                 : GyanisScribe::Core::Privacy::expandBarcodeScanline(reading.box, image.size(), image.size());
                if (!box.isEmpty())
                {
                    boxes.push_back(box);
                }
            }
        } else
        {
            // 码识别后端不在场不该拖住文本检测：记下原因，命中清单里只是少掉二维码那一路
            GyanisScribe::Base::Logger::warning(std::format("步骤截图里的二维码识别已跳过：{}", readings.error().message));
        }

        const auto items = GyanisScribe::Core::Privacy::detectPii(result, boxes, image.size());
        if (items.empty())
        {
            startNext();
            return;
        }

        std::vector<QRect> physicalBoxes;
        physicalBoxes.reserve(items.size());
        for (const auto &item: items)
        {
            physicalBoxes.push_back(item.physicalBox);
        }
        const auto redacted = GyanisScribe::Core::Privacy::redactImage(image, physicalBoxes, GyanisScribe::Core::ToolKind::Mosaic, image.size(),
                                                                       GyanisScribe::Core::kMosaicBlockSizes[GyanisScribe::Core::kDefaultMosaicBlockStep]);
        if (!redacted)
        {
            GyanisScribe::Base::Logger::warning(std::format("步骤截图打码失败，原样保留：{}", redacted.error().message));
            ++m_failed;
            startNext();
            return;
        }

        // 覆写这一张就是它真正的去向：审核窗随后读的还是同一个路径，用户看到的已经是打过码的那份。
        // 写临时文件再改名，中途失败时原图仍在。单飞之下最近发出去的那张就是手上这张
        const auto path = m_images[m_next - 1];
        QSaveFile  file{QString::fromStdWString(path.wstring())};
        if (!file.open(QIODevice::WriteOnly) || !redacted->image.save(&file, "PNG") || !file.commit())
        {
            GyanisScribe::Base::Logger::warning(std::format("步骤截图打码后写不回去，原样保留：{}", QString::fromStdWString(path.wstring()).toStdString()));
            ++m_failed;
            startNext();
            return;
        }

        ++m_redacted;
        startNext();
    }
} // namespace GyanisScribe::App
