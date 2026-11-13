#include "App/TiledPrivacyScanner.h"

#include "App/PrivacyCoordinator.h"
#include "Base/Logger.h"
#include "Base/Text.h"
#include "Core/Ocr/IOcrEngine.h"
#include "Core/Privacy/PrivacyScanTiles.h"

#include <QSize>

#include <utility>

namespace GyanisScribe::App
{
    TiledPrivacyScanner::TiledPrivacyScanner(GyanisScribe::Core::ISettingsStore &settings, GyanisScribe::Platform::IPlatformIntegration &integration, QObject *parent) :
        QObject(parent), m_settings(settings), m_integration(&integration)
    {
    }

    TiledPrivacyScanner::TiledPrivacyScanner(GyanisScribe::Core::ISettingsStore &settings, std::unique_ptr<GyanisScribe::Core::IOcrEngine> engine, QObject *parent) :
        QObject(parent), m_settings(settings), m_injectedEngine(std::move(engine))
    {
    }

    TiledPrivacyScanner::~TiledPrivacyScanner() = default;

    bool TiledPrivacyScanner::isScanning() const noexcept
    {
        return m_scanning;
    }

    void TiledPrivacyScanner::start(const QImage &image)
    {
        if (m_scanning)
        {
            // 协调器是单飞的：这一带没回来之前再发请求会被它直接丢掉，这一轮就会永远等下去。
            // 作废旧一轮、把新图记下，等那一带回来再开新一轮
            m_abandoned    = true;
            m_pendingImage = image;
            return;
        }
        beginScan(image);
    }

    void TiledPrivacyScanner::beginScan(const QImage &image)
    {
        m_image = image;
        m_tiles =
                GyanisScribe::Core::Privacy::scanTilesForImage(image.size(), GyanisScribe::Core::Privacy::kPrivacyScanBandHeight, GyanisScribe::Core::Privacy::kPrivacyScanOverlap);
        m_nextTile     = 0;
        m_merged       = GyanisScribe::Core::OcrResult{};
        m_inFlightKey  = 0;
        m_abandoned    = false;
        m_pendingImage = QImage{};

        if (m_tiles.empty())
        {
            m_scanning = false;
            emit scanFailed(GyanisScribe::Base::Error{GyanisScribe::Base::ErrorCode::InvalidArgument, GyanisScribe::Base::uiTextUtf8("这张图没法分带扫描：尺寸为空")});
            return;
        }

        if (!m_scanner)
        {
            if (m_injectedEngine)
            {
                m_scanner = std::make_unique<PrivacyCoordinator>(m_settings, std::move(m_injectedEngine));
            } else
            {
                m_scanner = std::make_unique<PrivacyCoordinator>(m_settings, *m_integration);
            }
            // 结果按「发出去那一带副本的 cacheKey」认领：旧一轮迟到的结果 key 对不上，直接丢
            QObject::connect(m_scanner.get(), &PrivacyCoordinator::scanSucceeded, this,
                             [this](const QImage &tile, const GyanisScribe::Core::OcrResult &result) { handleTileSucceeded(tile, result); });
            QObject::connect(m_scanner.get(), &PrivacyCoordinator::scanFailed, this, [this](const GyanisScribe::Base::Error &error) { handleTileFailed(error); });
            // 分带扫描不往界面上弹气泡：每一带都来一条「正在检查」会把提示刷屏，失败原因已经由 scanFailed 交回。
            // 这路话降级到诊断日志，排查时仍看得到内层协调器说了什么
            QObject::connect(m_scanner.get(), &PrivacyCoordinator::messageForUser, this, [](const QString &message, GyanisScribe::Gui::ToastKind)
                             { GyanisScribe::Base::Logger::log(GyanisScribe::Base::Severity::Debug, message.toStdString()); });
        }

        m_scanning = true;
        startNextTile();
    }

    void TiledPrivacyScanner::startNextTile()
    {
        if (m_abandoned)
        {
            return;
        }
        if (m_nextTile >= m_tiles.size())
        {
            m_scanning = false;
            emit scanSucceeded(m_image, m_merged);
            return;
        }
        const QRect tile = m_tiles[m_nextTile++];
        QImage      copy = m_image.copy(tile);
        m_inFlightKey    = copy.cacheKey();
        m_scanner->requestScan(copy);
    }

    void TiledPrivacyScanner::handleTileSucceeded(const QImage &tile, const GyanisScribe::Core::OcrResult &result)
    {
        if (tile.cacheKey() != m_inFlightKey)
        {
            return; // 旧一轮的结果
        }
        m_inFlightKey = 0;
        if (m_abandoned)
        {
            // 这一轮已经被新图作废：既不合并，也不往下走；有排队的图就当场开新一轮
            m_scanning = false;
            if (!m_pendingImage.isNull())
            {
                beginScan(std::exchange(m_pendingImage, QImage{}));
            }
            return;
        }
        const auto index = m_nextTile - 1;
        m_merged = GyanisScribe::Core::Privacy::mergeTileResult(m_merged, result, m_tiles[index].topLeft(), m_tiles[index].height(), index == 0, index + 1 == m_tiles.size());
        startNextTile();
    }

    void TiledPrivacyScanner::handleTileFailed(const GyanisScribe::Base::Error &error)
    {
        if (m_abandoned)
        {
            m_scanning = false;
            if (!m_pendingImage.isNull())
            {
                beginScan(std::exchange(m_pendingImage, QImage{}));
            }
            return;
        }
        m_inFlightKey = 0;
        m_scanning    = false;
        emit scanFailed(error);
    }
} // namespace GyanisScribe::App
