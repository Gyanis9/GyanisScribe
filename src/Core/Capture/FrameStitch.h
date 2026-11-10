/**
 * @file FrameStitch.h
 * @brief 滚动长截图的接缝：认出两张相邻帧重叠了多少行，并把新的一段接上去
 * @author Gyanis
 * @date 2026-10-28
 * @version 0.0.1
 * @copyright Copyright (c) . All rights reserved.
 */

#pragma once

#include "Base/Text.h"
#include "Base/Types.h"

#include <QImage>

namespace GyanisScribe::Core
{

    /// 允许回溯的最大重叠行数：一屏的高度量级，超过这个数就当没有重叠
    inline constexpr int kMaximumOverlapRows = 1200;

    /// 认定是「真接上了」所需的最小重叠行数：只对上两三行多半是两帧里的空白区恰好相同
    inline constexpr int kMinimumOverlapRows = 24;

    /**
     * @brief 接缝判定的放宽口径：允许滚动过程中小面积变化不算断缝
     * @details 动图、光标闪烁或半透明浮层会让相邻两帧的同一块像素略有出入；
     *          逐像素完全相等会把真接缝判成接不上，整帧重复接进长图。
     */
    struct StitchOptions
    {
        int    maximumOverlapRows = kMaximumOverlapRows; ///< 最多往回看多少行
        int    pixelTolerance     = 8;                   ///< 单个通道差超过它，这个像素就算不同
        double differingRowRatio  = 0.02;                ///< 允许不同的行数占重叠行数的比例
    };

    /**
     * @brief 认出两张帧重叠了多少行
     * @param previous 前一张帧，已接在成品下面的那一段
     * @param next 刚抓到的新一帧
     * @param maximumOverlapRows 最多往回看多少行
     * @return 重叠行数；对不上少于 kMinimumOverlapRows 行时返回 0
     * @details 从最长往最短找，取第一个整段匹配的：滚动一页往往只露出新的一截，
     *          取「最短的相等段」会把两帧里恰好相同的空白当成接缝，接上去就是重复的一段。
     *          两张帧必须同宽；宽度不同说明中途换了屏或缩放，调用方得先收摊。
     */
    [[nodiscard]] int overlapRowCount(const QImage &previous, const QImage &next, int maximumOverlapRows = kMaximumOverlapRows);

    /**
     * @brief 按给定宽容度认出两张帧重叠了多少行
     * @param previous 前一张帧，已接在成品下面的那一段
     * @param next 刚抓到的新一帧
     * @param options 重叠上限与两档宽容度
     * @return 重叠行数；容差与精确两轮都找不到时返回 0
     * @details 行内允许少量像素不同、整段允许少量行不同，小面积动图不会再让真接缝落空；
     *          先按给定容差从长到短找，找不到再按零像素容差找一遍。
     */
    [[nodiscard]] int overlapRowCount(const QImage &previous, const QImage &next, const StitchOptions &options);

    /**
     * @brief 长截图拼不下去时的统一说法：缓冲开不出来或扩不出去，都归到这一条
     * @return 内部故障，正文告诉用户缩小选区或分段重截
     * @details 累加器与收口两处共用这一条，改文案时只需改这里；收口时快照为空也报它。
     */
    [[nodiscard]] inline Base::Error stitchOutOfMemoryError()
    {
        return Base::Error{Base::ErrorCode::InternalError, Base::uiTextUtf8("长截图接不上：内存不够继续拼接。请缩小选区或分段重截")};
    }

    /**
     * @brief 长图累加器：首帧定下宽度与缩放，之后每帧只把新露出的那几行写进缓冲
     * @details 缓冲按倍数扩容，收尾由 snapshot() 深拷贝一次；接 n 帧的总成本与已接高度成正比，
     *          而不是每帧都把整张长图重抄两遍。
     * @note 像素归本类独占：构造与 append() 都不与入参共享，snapshot() 交出的也是独立图像。
     */
    class FrameStitcher
    {
    public:
        /**
         * @brief 用首帧开一个累加器
         * @param first 第一帧；内部转成 ARGB32 抄进自有缓冲，此后与入参不再共享像素
         * @details 首帧非空但缓冲开不出来时只记下这一笔：append() 会直接报内存不够，
         *          而不是拿空缓冲去接缝或逐行抄。
         */
        explicit FrameStitcher(const QImage &first);

        /**
         * @brief 把新一帧接到缓冲末尾
         * @param next 刚抓到的新一帧，宽度必须与首帧一致
         * @return 接上了返回空；空图或宽度不同时返回中文原因；缓冲开不出来时返回内部故障
         * @details 只写 next 里没与已接部分重合的那几行；装不下时按倍数扩容，旧内容逐行搬过去。
         */
        [[nodiscard]] Base::Expected<void> append(const QImage &next);

        /**
         * @brief 已接好的像素高度
         * @return 行数；不含扩容后还没用上的那些空行
         */
        [[nodiscard]] int pixelHeight() const noexcept;

        /**
         * @brief 取一份独立的长图
         * @return 深拷贝出来的图，缩放因子与构造时一致
         */
        [[nodiscard]] QImage snapshot() const;

        /**
         * @brief 缓冲当前分配了多少行（只给用例用）
         * @return 容量行数，正常会大于 pixelHeight()
         */
        [[nodiscard]] int allocatedPixelHeightForTesting() const noexcept;

        /**
         * @brief 缓冲首地址（只给用例用）
         * @return 同一轮里没扩容就保持不变；缓冲为空时是 nullptr
         */
        [[nodiscard]] const uchar *bufferAddressForTesting() const noexcept;

        // 像素归本类独占：拷贝与移动都等于把同一块缓冲交给第二个持有者，这里一并禁掉
        FrameStitcher(const FrameStitcher &)            = delete;
        FrameStitcher &operator=(const FrameStitcher &) = delete;
        FrameStitcher(FrameStitcher &&)                 = delete;
        FrameStitcher &operator=(FrameStitcher &&)      = delete;

    private:
        /**
         * @brief 保证缓冲装得下 rows 行
         * @param rows 需要的总行数
         * @details 装得下就原地不动；装不下按「所需行数」与「现有容量两倍」中较大的那个重分配，
         *          连续追加 n 行只触发 O(log n) 次分配。
         */
        void ensureCapacity(int rows);

        QImage m_buffer;                   ///< 累加用的整块缓冲，容量可能大于已用行数
        int    m_usedRows         = 0;     ///< 缓冲里已经写好的行数，就是成品高度
        bool   m_allocationFailed = false; ///< 构造时缓冲没开出来：append() 直接报内存不够，不碰空缓冲
    };

} // namespace GyanisScribe::Core
