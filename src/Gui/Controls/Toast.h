/**
 * @file Toast.h
 * @brief 轻量提示：右下角气泡，3 秒自动消失
 * @author Gyanis
 * @date 2026-10-10
 * @version 0.0.1
 * @copyright Copyright (c) . All rights reserved.
 */

#pragma once

#include "Gui/Theme/DesignTokens.h"

#include <QObject>

#include <QPoint>
#include <QRect>
#include <QString>
#include <QColor>
#include <QSize>

class QLabel;

namespace GyanisScribe::Gui
{
    class ThemeManager;

    /// 提示的种类，决定语义色
    enum class ToastKind
    {
        Information, ///< 中性信息
        Success,     ///< 成功
        Danger,      ///< 出错
    };

    /// 气泡停留时长，毫秒：需求写的是 3 秒
    inline constexpr int kToastVisibleMs = 3000;

    /// 气泡与屏幕边缘的间距，取 4/8/12/16/24/32 阶梯里的 16
    inline constexpr int kToastMargin = 16;

    /**
     * @brief 取某一类提示的语义色
     * @param kind 提示种类
     * @param palette 当前主题的色板
     * @return 对应的颜色
     */
    [[nodiscard]] QColor toastColorFor(ToastKind kind, const ThemePalette &palette);

    /**
     * @brief 算气泡的位置
     * @param screenAvailable 目标屏幕的可用区
     * @param toastSize 气泡自身尺寸
     * @return 左上角坐标，落在右下角且留出一个边距
     * @note 需求要求「不允许遮挡关键操作区域」：右下角是相对最不干扰的位置，
     *       而尺寸超过可用区时夹到屏幕内，而不是让气泡露出一半
     */
    [[nodiscard]] QPoint toastPositionFor(const QRect &screenAvailable, const QSize &toastSize);

    /**
     * @brief 托盘式气泡提示
     * @details 独立顶层窗口：不属于任何页面，截图、录屏、识别的反馈都用它。
     *          同一时刻只有一个在显示，新的提示直接顶掉旧的——排队会把三秒前的操作当成刚发生的。
     */
    class Toast : public QObject
    {
        Q_OBJECT

    public:
        /**
         * @brief 构造提示器
         * @param icons 图标供给器，气泡左侧那枚图标由它取
         * @param theme 主题管理器，气泡的底色与语义色每次显示时从这里现取
         * @param parent Qt 父对象
         */
        Toast(class IconProvider &icons, const class ThemeManager &theme, QObject *parent = nullptr);

        /**
         * @brief 显示一条提示
         * @param text 正文，用户看得懂的那句
         * @param kind 种类，决定语义色与图标
         * @param screenAvailable 显示在哪块屏幕的可用区，默认取主屏
         */
        void show(const QString &text, ToastKind kind = ToastKind::Information, const QRect &screenAvailable = {});

        /// 当前显示的气泡文本，空表示没在显示（用例据此判自动隐藏有没有生效）
        [[nodiscard]] QString currentText() const;

    private:
        class Bubble;

        IconProvider       &m_icons;    ///< 图标出口，不拥有
        const ThemeManager &m_theme;    ///< 主题来源，不拥有
        Bubble             *m_bubble{}; ///< 当前气泡，同一时刻最多一个
    };

} // namespace GyanisScribe::Gui
