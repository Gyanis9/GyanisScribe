#include "Core/Capture/FrameStitch.h"

#include "Base/Text.h"

#include <QImage>

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <expected>

namespace
{
    using GyanisScribe::Core::kMinimumOverlapRows;
    using GyanisScribe::Core::overlapRowCount;
    using GyanisScribe::Core::StitchOptions;

    /// 首帧再矮也先按这个行数开缓冲：几次追加之内不必扩容，免得每帧都搬一遍
    constexpr int kMinimumBufferRows = 64;

    /// 全流程共用同一套宽容度：吸顶头尾、接缝搜索与整帧判定各写一份迟早走岔
    constexpr StitchOptions kStitchOptions{};

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

    /// 两个 ARGB 像素是否在容差之内：任一通道差超过 pixelTolerance 就算不同
    [[nodiscard]] bool pixelsMatch(const QRgb upper, const QRgb lower, const int pixelTolerance)
    {
        return std::abs(qRed(upper) - qRed(lower)) <= pixelTolerance && std::abs(qGreen(upper) - qGreen(lower)) <= pixelTolerance &&
               std::abs(qBlue(upper) - qBlue(lower)) <= pixelTolerance && std::abs(qAlpha(upper) - qAlpha(lower)) <= pixelTolerance;
    }

    /// 两张同宽的帧里各取一行，看这一行是否超出它的宽容差
    [[nodiscard]] bool rowsDiffer(const QImage &upper, const int upperRow, const QImage &lower, const int lowerRow, const StitchOptions &options)
    {
        const auto width = upper.width();
        // 行内允许的不同像素数按宽度的百分之一取整；窄帧至少留一个，免得一个像素抖动就判死
        const auto  allowedDifferingPixels = std::max(1, width / 100);
        const auto *upperLine              = reinterpret_cast<const QRgb *>(upper.constScanLine(upperRow));
        const auto *lowerLine              = reinterpret_cast<const QRgb *>(lower.constScanLine(lowerRow));
        auto        differingPixels        = 0;
        for (auto column = 0; column < width; ++column)
        {
            if (!pixelsMatch(upperLine[column], lowerLine[column], options.pixelTolerance))
            {
                ++differingPixels;
                // 一行已超出容差就不必再数：整段判定只关心这一行算不算不同
                if (differingPixels > allowedDifferingPixels)
                {
                    return true;
                }
            }
        }
        return false;
    }

    /// 前一张的最后 rows 行与新一帧的前 rows 行在给定宽容度下是否算接上
    [[nodiscard]] bool regionMatches(const QImage &previous, const QImage &next, const int rows, const StitchOptions &options)
    {
        // 整段允许的不同行数按比例取整，同样至少留一行：动图常常只占一两行，按比例取整会归零
        const auto allowedDifferingRows = std::max(1, static_cast<int>(std::floor(static_cast<double>(rows) * options.differingRowRatio)));
        auto       differingRows        = 0;
        for (auto offset = 0; offset < rows; ++offset)
        {
            // 不同的行数一旦超过整段容差，这段长度就不可能是接缝，继续往下数没有意义
            if (rowsDiffer(previous, previous.height() - rows + offset, next, offset, options) && ++differingRows > allowedDifferingRows)
            {
                return false;
            }
        }
        return true;
    }

    /// 从候选帧顶部数起，连续多少行与参考带的同一行在容差内相同
    [[nodiscard]] int matchingLeadingRows(const QImage &candidate, const QImage &reference, const StitchOptions &options)
    {
        const auto candidates = std::min(candidate.height(), reference.height());
        for (auto rows = 0; rows < candidates; ++rows)
        {
            if (rowsDiffer(reference, rows, candidate, rows, options))
            {
                return rows;
            }
        }
        return candidates;
    }

    /// 从候选帧底部数起，连续多少行与参考带的同一行在容差内相同
    [[nodiscard]] int matchingTrailingRows(const QImage &candidate, const QImage &reference, const int maximumRows, const StitchOptions &options)
    {
        const auto candidates = std::min({candidate.height(), reference.height(), std::max(0, maximumRows)});
        for (auto rows = 0; rows < candidates; ++rows)
        {
            if (rowsDiffer(reference, reference.height() - 1 - rows, candidate, candidate.height() - 1 - rows, options))
            {
                return rows;
            }
        }
        return candidates;
    }

    /// 已认出的粘性头尾是否仍原样出现在这一帧里
    [[nodiscard]] bool stickyBandsStillMatch(const QImage &candidate, const QImage &referenceTop, const QImage &referenceBottom, const int topRows, const int bottomRows,
                                             const StitchOptions &options)
    {
        if (topRows > 0 && matchingLeadingRows(candidate, referenceTop, options) < topRows)
        {
            return false;
        }
        return bottomRows <= 0 || matchingTrailingRows(candidate, referenceBottom, candidate.height(), options) >= bottomRows;
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
        // 老三参入口按默认宽容度判定：整段对得上的帧行为不变，小面积动图也能接上
        return overlapRowCount(previous, next, StitchOptions{.maximumOverlapRows = maximumOverlapRows});
    }

    int overlapRowCount(const QImage &previous, const QImage &next, const StitchOptions &options)
    {
        const auto upper = asArgb(previous);
        const auto lower = asArgb(next);
        if (upper.isNull() || lower.isNull() || upper.width() != lower.width() || upper.bytesPerLine() != lower.bytesPerLine())
        {
            return 0;
        }

        // 从最长往最短找，取第一个整段匹配的：两帧里恰好相同的空白只会对上几行，
        // 而真接缝是整段重合，往短了找会把空白当成缝
        const auto candidates = std::min({upper.height(), lower.height(), std::max(0, options.maximumOverlapRows)});
        for (auto rows = candidates; rows >= kMinimumOverlapRows; --rows)
        {
            if (regionMatches(upper, lower, rows, options))
            {
                return rows;
            }
        }

        // 容差搜索落空时再按逐字节相等找一遍：纯像素相等的旧判定继续兜底
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
            // 开不出缓冲就记下来：append() 直接报内存不够，好过拿空指针逐行抄
            m_allocationFailed = true;
            return;
        }
        // 逐行抄而不是给入参挂一份引用：调用方交完这一帧就不再管它，缓冲得自己活下来
        copyRows(m_buffer, 0, source, 0, source.height());
        m_usedRows = source.height();

        // 顶部与底部各留一条参考带：粘性标题栏与输入框在之后每帧都原样回来，拿首帧这一段做比对基准。
        // 带高上限是帧高的四分之一：比这更「稳定」的横带多半是整块内容，认成头尾就是吃用户内容
        const auto bandRows = std::max(1, source.height() / 4);
        m_referenceTop      = source.copy(0, 0, source.width(), bandRows);
        m_referenceBottom   = source.copy(0, source.height() - bandRows, source.width(), bandRows);
        m_referenceTop.setDevicePixelRatio(first.devicePixelRatio());
        m_referenceBottom.setDevicePixelRatio(first.devicePixelRatio());
    }

    Base::Expected<void> FrameStitcher::append(const QImage &next)
    {
        // 首帧非空却没能开出缓冲：这一轮注定接不上，先把它报出去，别拿空缓冲去比接缝
        if (m_allocationFailed)
        {
            return std::unexpected(stitchOutOfMemoryError());
        }
        const auto added = asArgb(next);
        if (m_buffer.isNull() || added.isNull())
        {
            return std::unexpected(Base::Error{Base::ErrorCode::InvalidArgument, GyanisScribe::Base::uiTextUtf8("长截图接不上：这一帧是空的。请重新截一次")});
        }
        if (m_buffer.width() != added.width())
        {
            return std::unexpected(
                    Base::Error{Base::ErrorCode::InvalidArgument,
                                GyanisScribe::Base::uiTextUtf8(
                                        "长截图接不上：这一帧的宽度和前面不一致（多半是中途换了屏或改了缩放）。这一段已中止，临时画面不会保留；请在同一屏幕与缩放下重截")});
        }

        auto topSkip      = 0;
        auto bottomSkip   = 0;
        auto overlap      = 0;
        auto bufferTrim   = 0;
        auto stickyTop    = m_stickyTopRows;
        auto stickyBottom = m_stickyBottomRows;

        if (added.height() >= kMinimumOverlapRows)
        {
            // 只把已写好的那几行交给接缝判定：扩容留出的空行还没写过，混进来比就是拿未初始化的字节碰运气。
            // 浅视图只活在这些语句里，出了作用域就不会留下指向缓冲的句柄
            const auto bufferRows  = [this](const int rows) { return QImage{m_buffer.constBits(), m_buffer.width(), rows, m_buffer.bytesPerLine(), QImage::Format_ARGB32}; };
            const auto contentRows = [&added](const int firstRow, const int rows)
            { return QImage{added.constScanLine(firstRow), added.width(), rows, added.bytesPerLine(), QImage::Format_ARGB32}; };

            if ((stickyTop > 0 || stickyBottom > 0) && stickyBandsStillMatch(added, m_referenceTop, m_referenceBottom, stickyTop, stickyBottom, kStitchOptions))
            {
                // 头尾仍原样在：它们不再参与接缝，只拿中间可滚动的内容找；容差与整帧路径共用同一套
                topSkip              = stickyTop;
                bottomSkip           = stickyBottom;
                const auto remaining = added.height() - topSkip - bottomSkip;
                overlap              = remaining >= kMinimumOverlapRows ? overlapRowCount(bufferRows(m_usedRows), contentRows(topSkip, remaining), kStitchOptions) : 0;
            } else
            {
                // 头尾不再稳定（或还没认出来）就按整帧处理：整帧能接上时不动头尾，免得把真实内容当标题栏裁掉
                stickyTop    = 0;
                stickyBottom = 0;
                overlap      = overlapRowCount(bufferRows(m_usedRows), added, kStitchOptions);
                if (overlap < kMinimumOverlapRows)
                {
                    // 整帧接不上才怀疑有粘性头尾：找新帧与首帧首尾最长的容差一致横带，太短的按内容看待
                    const auto topRun    = matchingLeadingRows(added, m_referenceTop, kStitchOptions);
                    const auto bottomRun = topRun < added.height() ? matchingTrailingRows(added, m_referenceBottom, added.height() - topRun, kStitchOptions) : 0;
                    topSkip              = topRun >= kMinimumStickyRows ? topRun : 0;
                    bottomSkip           = bottomRun >= kMinimumStickyRows ? bottomRun : 0;
                    const auto remaining = added.height() - topSkip - bottomSkip;
                    if (topSkip + bottomSkip > 0 && remaining >= kMinimumOverlapRows)
                    {
                        // 首帧也带着这条粘性尾时，缓冲末尾那一小段正是输入框：先把它从缓冲末尾裁掉再接中间内容。
                        // 裁剪前提是从缓冲底部往上仍是这条参考尾带（容差判定）；末尾已经是内容时裁掉就是吃用户内容
                        auto trim = 0;
                        if (bottomSkip > 0 && m_usedRows > bottomSkip && matchingTrailingRows(bufferRows(m_usedRows), m_referenceBottom, bottomSkip, kStitchOptions) >= bottomSkip)
                        {
                            trim = bottomSkip;
                        }
                        overlap = overlapRowCount(bufferRows(m_usedRows - trim), contentRows(topSkip, remaining), kStitchOptions);
                        if (overlap >= kMinimumOverlapRows)
                        {
                            bufferTrim   = trim;
                            stickyTop    = topSkip;
                            stickyBottom = bottomSkip;
                        }
                    }
                }
            }

            if (overlap < kMinimumOverlapRows)
            {
                // 没有可信接缝就不接：整帧硬接只会得到一张重复的长图，还占掉用户的滚动位置
                return std::unexpected(Base::Error{
                        Base::ErrorCode::InvalidArgument,
                        Base::uiTextUtf8("长截图接不上：这一段和上一段没有可信的重叠。请回到上一段末尾附近，等画面停稳后再滚一小段；如果已经滚过一屏，请缩小滚动幅度重来")});
            }
        }

        // 到了这里接缝可信：把认出的粘性头尾记下来，并先从缓冲末尾裁掉已经写进去的吸底尾。
        // 裁剪只动已用行数，缓冲里的旧像素留在原处，snapshot() 不会把它们交出去
        m_stickyTopRows    = stickyTop;
        m_stickyBottomRows = stickyBottom;
        m_usedRows -= bufferTrim;

        // 重叠行数不可能超过新帧本身，所以 fresh 最小就是 0：一帧都没滚时这一帧什么都不写
        const auto fresh = added.height() - topSkip - bottomSkip - overlap;
        ensureCapacity(m_usedRows + fresh);
        if (m_usedRows + fresh > m_buffer.height())
        {
            // 扩容没成功时缓冲还是原来那一张，往下写就越界了；让调用方把已有这一段收好
            return std::unexpected(stitchOutOfMemoryError());
        }
        // 逐行抄而不是 QPainter：合成会把 alpha 再算一遍，抄过去才是原样
        copyRows(m_buffer, m_usedRows, added, topSkip + overlap, fresh);
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

    int FrameStitcher::stickyTopRowsForTesting() const noexcept
    {
        return m_stickyTopRows;
    }

    int FrameStitcher::stickyBottomRowsForTesting() const noexcept
    {
        return m_stickyBottomRows;
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
