/**
 * @file ColorCard.h
 * @brief 颜色卡片与文本卡片共用的观感常量，以及把一种颜色渲染成卡片的画法
 * @author Gyanis
 * @date 2026-10-14
 * @version 0.0.1
 * @copyright Copyright (c) . All rights reserved.
 */

#pragma once

#include <QColor>
#include <QImage>
#include <QString>

namespace GyanisScribe::Pin
{

    /// 颜色卡片：色块加底部一条写法条
    inline constexpr int kColorCardWidth       = 160;
    inline constexpr int kColorCardHeight      = 100;
    inline constexpr int kColorCardStripHeight = 28;

    /// 卡片配色的另一格：条上那一行字的字号（文本卡片与色卡共用，两处各写一个数就会差出一档）
    inline constexpr int kCardFontPointSize = 14;

    /// 卡片配色：白底、近黑字、浅灰边。写在实现里而不是进主题：卡片是独立成品，
    /// 深浅主题各渲染一种会让同一段文字在别人桌面上有两种样子（文本卡片也取这三个）
    inline const QColor kCardBackground{252, 252, 252};
    inline const QColor kCardText{24, 24, 24};
    inline const QColor kCardBorder{220, 224, 230};

    /**
     * @brief 把一种颜色渲染成一张卡片
     * @param color 颜色
     * @param label 底部条上写的文字（通常是这一档的写法）
     * @return 上块下条的图：色块本体 + 底部写法条
     * @details 画法住在 Pin 这一层：贴图窗右键「改这一张的颜色…」要就地重画这张卡，
     *          而 Pin 不依赖 App（方向不能反）。走界面层转手的话，改色这条路上会多一个
     *          「谁来重画」的跨层往返，判据也只能钉到其中一半。
     */
    [[nodiscard]] QImage renderColorCard(const QColor &color, const QString &label);

} // namespace GyanisScribe::Pin
