#include "Capture/FrameStitch.h"

#include "Core/Text.h"

#include <QImage>

#include <algorithm>
#include <cstring>
#include <expected>

namespace
{
    using GyanisScribe::Capture::overlapRowCount;

    /// 统一到 ARGB32 再比：不同格式的 bytesPerLine 不一样，按行比字节就成了比内存布局
    [[nodiscard]] QImage asArgb(const QImage &image)
    {
        return image.format() == QImage::Format_ARGB32 ? image : image.convertToFormat(QImage::Format_ARGB32);
    }

    /// 前一张的最后 rows 行与新一帧的前 rows 行是否逐字节相同
    [[nodiscard]] bool rowsMatch(const QImage &previous, const QImage &next, int rows)
    {
        const auto stride     = previous.bytesPerLine();
        const auto startIndex = previous.height() - rows;
        for (auto offset = 0; offset < rows; ++offset)
        {
            if (std::memcmp(previous.constScanLine(startIndex + offset), next.constScanLine(offset), static_cast<std::size_t>(stride)) != 0)
            {
                return false;
            }
        }
        return true;
    }
} // namespace

namespace GyanisScribe::Capture
{
    int overlapRowCount(const QImage &previous, const QImage &next, const int maximumOverlapRows)
    {
        const auto upper = asArgb(previous);
        const auto lower = asArgb(next);
        if (upper.isNull() || lower.isNull() || upper.width() != lower.width() || upper.bytesPerLine() != lower.bytesPerLine())
        {
            return 0;
        }

        // 从最长往最短找，取第一个整段相等的：两帧里恰好相同的空白只会对上几行，
        // 而真接缝是整段重合，往短了找会把空白当成缝
        const auto candidates = std::min({upper.height(), lower.height(), std::max(0, maximumOverlapRows)});
        for (auto rows = candidates; rows >= kMinimumOverlapRows; --rows)
        {
            if (rowsMatch(upper, lower, rows))
            {
                return rows;
            }
        }
        return 0;
    }

    Core::Expected<QImage> appendFrame(const QImage &stitched, const QImage &next)
    {
        const auto base  = asArgb(stitched);
        const auto added = asArgb(next);
        if (base.isNull() || added.isNull())
        {
            return std::unexpected(Core::Error{Core::ErrorCode::InvalidArgument, GyanisScribe::Core::uiTextUtf8("长截图接不上：这一帧是空的。请重新截一次")});
        }
        if (base.width() != added.width())
        {
            return std::unexpected(Core::Error{Core::ErrorCode::InvalidArgument,
                                               GyanisScribe::Core::uiTextUtf8("长截图接不上：这一帧的宽度和前面不一致（多半是中途换了屏或改了缩放）。先把已有这一段收好再重来")});
        }

        // 重叠行数不可能超过新帧本身，所以 fresh 最小就是 0：一帧都没滚时下面只抄旧的那一段
        const auto overlap = overlapRowCount(base, added);
        const auto fresh   = added.height() - overlap;

        // 逐行抄而不是 QPainter：合成会把 alpha 再算一遍，抄过去才是原样
        QImage merged{QSize{base.width(), base.height() + fresh}, QImage::Format_ARGB32};
        merged.setDevicePixelRatio(base.devicePixelRatio());
        const auto stride = base.bytesPerLine();
        for (auto row = 0; row < base.height(); ++row)
        {
            std::memcpy(merged.scanLine(row), base.constScanLine(row), static_cast<std::size_t>(stride));
        }
        for (auto row = 0; row < fresh; ++row)
        {
            std::memcpy(merged.scanLine(base.height() + row), added.constScanLine(overlap + row), static_cast<std::size_t>(stride));
        }
        return merged;
    }

} // namespace GyanisScribe::Capture
