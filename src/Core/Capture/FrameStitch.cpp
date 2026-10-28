#include "Core/Capture/FrameStitch.h"

#include "Base/Text.h"

#include <QImage>

#include <algorithm>
#include <cstring>
#include <expected>

namespace
{
    using GyanisScribe::Core::overlapRowCount;

    /// 首帧再矮也先按这个行数开缓冲：几次追加之内不必扩容，免得每帧都搬一遍
    constexpr int kMinimumBufferRows = 64;

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

    /// 把 source 从 sourceRow 起的 rows 行抄到 destination 的 destinationRow 处；两图必须同宽
    void copyRows(QImage &destination, int destinationRow, const QImage &source, int sourceRow, int rows)
    {
        const auto stride = destination.bytesPerLine();
        for (auto row = 0; row < rows; ++row)
        {
            std::memcpy(destination.scanLine(destinationRow + row), source.constScanLine(sourceRow + row), static_cast<std::size_t>(stride));
        }
    }
} // namespace

namespace GyanisScribe::Core
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

    FrameStitcher::FrameStitcher(const QImage &first)
    {
        const auto source = asArgb(first);
        if (source.isNull())
        {
            return;
        }
        m_buffer = QImage{QSize{source.width(), std::max(source.height(), kMinimumBufferRows)}, QImage::Format_ARGB32};
        m_buffer.setDevicePixelRatio(first.devicePixelRatio());
        if (m_buffer.isNull())
        {
            // 开不出缓冲就当一行都没接上：m_usedRows 留在 0，append() 会报「这一帧是空的」，好过拿空指针逐行抄
            return;
        }
        // 逐行抄而不是给入参挂一份引用：调用方交完这一帧就不再管它，缓冲得自己活下来
        copyRows(m_buffer, 0, source, 0, source.height());
        m_usedRows = source.height();
    }

    Base::Expected<void> FrameStitcher::append(const QImage &next)
    {
        const auto added = asArgb(next);
        if (m_buffer.isNull() || added.isNull())
        {
            return std::unexpected(Base::Error{Base::ErrorCode::InvalidArgument, GyanisScribe::Base::uiTextUtf8("长截图接不上：这一帧是空的。请重新截一次")});
        }
        if (m_buffer.width() != added.width())
        {
            return std::unexpected(Base::Error{Base::ErrorCode::InvalidArgument,
                                               GyanisScribe::Base::uiTextUtf8("长截图接不上：这一帧的宽度和前面不一致（多半是中途换了屏或改了缩放）。先把已有这一段收好再重来")});
        }

        // 只把已写好的那几行交给接缝判定：扩容留出的空行还没写过，混进来比就是拿未初始化的字节碰运气。
        // 浅视图只活在这一句里，出了作用域就不会留下指向 m_buffer 的句柄
        const auto overlap = [this, &added]
        {
            const QImage view{m_buffer.constBits(), m_buffer.width(), m_usedRows, m_buffer.bytesPerLine(), QImage::Format_ARGB32};
            return overlapRowCount(view, added);
        }();

        // 重叠行数不可能超过新帧本身，所以 fresh 最小就是 0：一帧都没滚时这一帧什么都不写
        const auto fresh = added.height() - overlap;
        ensureCapacity(m_usedRows + fresh);
        if (m_usedRows + fresh > m_buffer.height())
        {
            // 扩容没成功时缓冲还是原来那一张，往下写就越界了；让调用方把已有这一段收好
            return std::unexpected(Base::Error{Base::ErrorCode::InternalError, GyanisScribe::Base::uiTextUtf8("长截图接不上：内存不够再开一段缓冲。先把已有这一段收好再重来")});
        }
        // 逐行抄而不是 QPainter：合成会把 alpha 再算一遍，抄过去才是原样
        copyRows(m_buffer, m_usedRows, added, overlap, fresh);
        m_usedRows += fresh;
        return {};
    }

    int FrameStitcher::pixelHeight() const noexcept
    {
        return m_usedRows;
    }

    QImage FrameStitcher::snapshot() const
    {
        // copy 出来的是一份独立像素：交出去之后累加器再扩容也碰不到调用方那一张
        QImage cropped = m_buffer.copy(0, 0, m_buffer.width(), m_usedRows);
        cropped.setDevicePixelRatio(m_buffer.devicePixelRatio());
        return cropped;
    }

    int FrameStitcher::allocatedPixelHeightForTesting() const noexcept
    {
        return m_buffer.height();
    }

    const uchar *FrameStitcher::bufferAddressForTesting() const noexcept
    {
        return m_buffer.constBits();
    }

    void FrameStitcher::ensureCapacity(const int rows)
    {
        if (rows <= m_buffer.height())
        {
            return;
        }

        // 按倍数长：每追加一帧都精确重分配的话，接 n 帧要搬 n^2/2 行，等于回到逐帧重抄的老路
        const auto newCapacity = std::max(rows, m_buffer.height() * 2);
        QImage     grown{QSize{m_buffer.width(), newCapacity}, QImage::Format_ARGB32};
        if (grown.isNull())
        {
            // 分配失败就留着原缓冲：换成空图会把已经接好的行连地址一起丢掉，append() 那边也还能报错
            return;
        }
        grown.setDevicePixelRatio(m_buffer.devicePixelRatio());
        copyRows(grown, 0, m_buffer, 0, m_usedRows);
        m_buffer = grown;
    }

} // namespace GyanisScribe::Core
