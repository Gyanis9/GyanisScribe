/**
 * @file ControlPalette.h
 * @brief 控件状态到语义色的映射
 * @author Gyanis
 * @date 2026-11-08
 * @version 0.0.1
 * @copyright Copyright (c) . All rights reserved.
 */

#pragma once

#include "Gui/Theme/DesignTokens.h"

#include <QColor>
#include <QWidget>

namespace GyanisScribe::Gui
{

    /// SurfaceWidget/Label 用哪一档语义色铺底
    enum class SurfaceFillRole
    {
        None,             ///< 不铺底
        WindowBackground, ///< 窗口底
        Surface,          ///< 卡片与浮层
        ControlFill       ///< 按钮与输入
    };

    /// 一次控件绘制的填充、描边与描边宽度
    struct ControlColors
    {
        QColor fill;            ///< 填充色
        QColor border;          ///< 描边色；borderWidth 为 0 时不画
        int    borderWidth = 0; ///< 描边宽度，逻辑像素
    };

    /**
     * @brief 按钮的填充与聚焦描边
     * @param palette 当前主题的语义色
     * @param kind 当前主题档
     * @param isDefault 是否主按钮
     * @param enabled 是否启用
     * @param hover 是否悬停
     * @param down 是否按下
     * @param focused 是否持有焦点
     * @param inlineFocus 是否由控件自绘焦点边（标注工具栏内）
     * @return 填充与描边；浅色/深色普通控件不给描边，由焦点环覆盖层负责；
     *         高对比度档无焦点时也保留 borderStrong 描边，保住这一档「控件一律 2px 边」的契约
     */
    [[nodiscard]] ControlColors buttonColors(const ThemePalette &palette, ThemeKind kind, bool isDefault, bool enabled, bool hover,
                                             bool down, bool focused, bool inlineFocus);

    /**
     * @brief 工具按钮的填充与聚焦描边
     * @param palette 当前主题的语义色
     * @param kind 当前主题档
     * @param checked 是否选中
     * @param enabled 是否启用
     * @param hover 是否悬停
     * @param down 是否按下
     * @param focused 是否持有焦点
     * @param inlineFocus 是否由控件自绘焦点边
     * @return 填充与描边；未选中且无状态时填充透明；高对比度档选中时补 accent 描边，
     *         对齐这一档 QSS 的 QToolButton:checked
     */
    [[nodiscard]] ControlColors toolButtonColors(const ThemePalette &palette, ThemeKind kind, bool checked, bool enabled, bool hover,
                                                 bool down, bool focused, bool inlineFocus);

    /**
     * @brief 输入类控件的填充与聚焦描边
     * @param palette 当前主题的语义色
     * @param kind 当前主题档
     * @param enabled 是否启用
     * @param readOnly 是否只读
     * @param focused 是否持有焦点
     * @param inlineFocus 是否由控件自绘焦点边
     * @return 填充与描边；只读与禁用落 mutedFill；
     *         高对比度档无焦点时也保留 borderStrong 描边，对齐这一档输入控件的 2px 边
     */
    [[nodiscard]] ControlColors inputColors(const ThemePalette &palette, ThemeKind kind, bool enabled, bool readOnly, bool focused,
                                            bool inlineFocus);

    /**
     * @brief 当前控件是否位于标注工具栏内
     * @param widget 待判定控件
     * @return 祖先链里出现对象名 `AnnotationToolbar` 时为真
     * @note 标注工具栏不装 6px 外扩焦点环，聚焦边由控件自己画；用祖先链判定，避免每个控件手工写属性
     */
    [[nodiscard]] bool usesInlineFocusBorder(const QWidget &widget);

} // namespace GyanisScribe::Gui
