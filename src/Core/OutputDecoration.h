/**
 * @file OutputDecoration.h
 * @brief 成品出程序之前的装饰：一圈边框与一圈轻投影（纯图像计算，不碰界面）
 * @author Gyanis
 * @date 2026-10-12
 * @version 0.0.1
 * @copyright Copyright (c) . All rights reserved.
 */

#pragma once

#include "Core/Types.h"

#include <QColor>
#include <QImage>
#include <QSize>
#include <QString>

namespace GyanisScribe::Core
{

    /// 边框的默认宽度，物理像素：两圈足够把浅色截图从浅色底上分出来，再宽就抢画面
    inline constexpr int kOutputBorderPixels = 2;

    /// 边框宽的可调下限，物理像素
    inline constexpr int kMinimumOutputBorderPixels = 1;

    /// 边框宽的可调上限，物理像素：12 格已经盖过多数窗口标题栏的一圈
    inline constexpr int kMaximumOutputBorderPixels = 12;

    /// 投影向外延伸的默认宽度，物理像素
    inline constexpr int kOutputShadowPixels = 16;

    /// 投影宽的可调下限，物理像素：小于这一档渐隐只有两三层，看上去成了一条硬边
    inline constexpr int kMinimumOutputShadowPixels = 4;

    /// 投影宽的可调上限，物理像素：再宽就是拿一整圈空白换「更柔」，成品发虚也发胖
    inline constexpr int kMaximumOutputShadowPixels = 48;

    /// 投影紧贴成品那一圈的不透明度（0-255）：轻量档，只把成品从底上「抬」起来一点
    inline constexpr int kOutputShadowMaximumAlpha = 96;

    /// 边框的默认颜色：中灰，深浅底上都看得出边界，又不像纯黑那样把成品框死
    inline const QColor kOutputBorderColor{0x80, 0x80, 0x80};

    /**
     * @brief 一档成品装饰的参数
     * @details 缺省就是上面那几枚常量；界面与设置读出来的那一份经 parse* 那三个校验口进来，
     *          所以这里的值一定在合法区间内。投影浓淡仍是一个常量：只有宽与色被问过要调。
     */
    struct OutputDecorationStyle
    {
        int    borderPixels = kOutputBorderPixels; ///< 边框宽，物理像素
        QColor borderColor  = kOutputBorderColor;  ///< 边框色
        int    shadowPixels = kOutputShadowPixels; ///< 投影向外延伸的宽度，物理像素
    };

    /**
     * @brief 校验配置里读出的边框宽度
     * @param stored 配置读出的整数，物理像素
     * @return 成功返回可用值；不在 [1,12] 里时返回带范围与补救办法的中文原因
     */
    [[nodiscard]] Expected<int> parseOutputBorderPixels(int stored);

    /**
     * @brief 校验配置里读出的投影宽度
     * @param stored 配置读出的整数，物理像素
     * @return 成功返回可用值；不在 [4,48] 里时返回带范围与补救办法的中文原因
     */
    [[nodiscard]] Expected<int> parseOutputShadowPixels(int stored);

    /**
     * @brief 校验配置里读出的边框颜色写法
     * @param stored 一段 #RRGGBB 文本（空串表示没存过）
     * @return 成功返回该颜色（空串返回默认的中灰）；写法不对时返回中文原因
     */
    [[nodiscard]] Expected<QColor> parseOutputBorderColor(const QString &stored);

    /**
     * @brief 装饰之后的画布尺寸
     * @param imageSize 成品图尺寸，物理像素
     * @param border 加不加边框
     * @param shadow 加不加投影
     * @param style 这一档装饰的参数
     * @return 两档都关时与原尺寸相同；开着的那几档各自向四周撑开
     */
    [[nodiscard]] QSize decoratedOutputSize(const QSize &imageSize, bool border, bool shadow, const OutputDecorationStyle &style);

    /**
     * @brief 把边框与投影画到一张新图上
     * @param image 成品图，物理像素
     * @param border 加不加边框
     * @param shadow 加不加投影
     * @param fillBackground 目标格式带不了透明通道（JPG）时给真：投影底下先垫一层白，
     *        否则透明区在保存时会被压成不可预期的底色
     * @param style 这一档装饰的参数
     * @return 装好饰的新图；两档都关且不用垫底时原样交回入参（不复制、不改动）
     * @details 投影取「四周对称、无偏移」的柔和渐隐：截图贴出去时四边匀称，不会一边重一边轻。
     *          位置关系是「边框在外、投影最外」：投影紧贴边框外沿并按距离平方衰减。
     */
    [[nodiscard]] QImage decorateForOutput(const QImage &image, bool border, bool shadow, bool fillBackground, const OutputDecorationStyle &style);

} // namespace GyanisScribe::Core
