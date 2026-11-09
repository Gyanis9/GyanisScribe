#include "Gui/History/ThumbnailPixels.h"

#include "Core/History/HistoryThumbnail.h"

#include <QCoreApplication>
#include <QFileInfo>
#include <QImageReader>
#include <QThread>

#include <algorithm>
#include <cmath>

namespace GyanisScribe::Gui
{
    DecodedThumbnail decodeThumbnailPixels(const QString &displayPath)
    {
        DecodedThumbnail decoded;
        decoded.decodedOffGuiThread = QThread::currentThread() != QCoreApplication::instance()->thread();

        QImageReader reader{displayPath};
        reader.setAutoTransform(true);

        // 读一次就够：先按最大那档 DPR 的覆盖尺寸预缩，三档缩略图都从这一份像素上裁
        if (const auto source = reader.size(); source.isValid() && !source.isEmpty())
        {
            const auto edge  = Core::kThumbnailEdge * kLargestThumbnailDevicePixelRatio;
            const auto ratio = std::max(static_cast<double>(edge) / static_cast<double>(source.width()), static_cast<double>(edge) / static_cast<double>(source.height()));
            reader.setScaledSize(
                    QSize{static_cast<int>(std::ceil(static_cast<double>(source.width()) * ratio)), static_cast<int>(std::ceil(static_cast<double>(source.height()) * ratio))});
        }

        const auto image = reader.read();
        if (image.isNull())
        {
            decoded.readFailed = true;
            return decoded;
        }

        decoded.oneX   = Core::makeThumbnail(image, QSize{Core::kThumbnailEdge, Core::kThumbnailEdge}, Core::kThumbnailCornerRadius);
        decoded.twoX   = Core::makeThumbnail(image, QSize{Core::kThumbnailEdge * 2, Core::kThumbnailEdge * 2}, Core::kThumbnailCornerRadius * 2);
        decoded.threeX = Core::makeThumbnail(image, QSize{Core::kThumbnailEdge * kLargestThumbnailDevicePixelRatio, Core::kThumbnailEdge * kLargestThumbnailDevicePixelRatio},
                                             Core::kThumbnailCornerRadius * kLargestThumbnailDevicePixelRatio);
        return decoded;
    }

    QString thumbnailCacheKey(const QString &displayPath)
    {
        // 修改时间进毫秒：同一路径被新内容覆盖时，键跟着变，旧图自然被当成另一张
        const QFileInfo info{displayPath};
        return displayPath + QChar{u'|'} + QString::number(info.lastModified().toMSecsSinceEpoch()) + QChar{u'|'} + QString::number(info.size());
    }

} // namespace GyanisScribe::Gui
