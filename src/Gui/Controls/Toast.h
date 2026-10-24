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
     * @brief 判断某类提示要不要逐字显现
     * @param kind 提示种类
     * @param animationsEnabled 系统与应用是否允许动画
     * @return 允许动画时成功与信息提示为 true；错误提示一律 false
     * @details 错误提示要整句一起出现：逐字浮现会让人先读半句再改口。
     */
    [[nodiscard]] bool shouldStreamToast(ToastKind kind, bool animationsEnabled) noexcept;

    /**
     * @brief 算文字列的上边距，把首行基线压到图标下方
     * @param iconHeight 图标逻辑高度
     * @param ascent 文本字体的 ascent
     * @param emHeight 文本字体的 em 高，取 QFontInfo::pixelSize()，不是 QFontMetrics::height() 的行距
     * @param iconMargin 图标上方的留白
     * @return 文字列的上边距，使图标底边落在首行基线下方 0.125em
     */
    [[nodiscard]] int iconTextTopPad(int iconHeight, int ascent, int emHeight, int iconMargin) noexcept;

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
         * @brief 销毁提示器，并关掉还亮着的气泡
         * @details 气泡是没有父控件的顶层窗口，不随 QObject 的父子关系回收。
         */
        ~Toast() override;

        /**
         * @brief 显示一条提示
         * @param text 正文，用户看得懂的那句
         * @param kind 种类，决定语义色与图标
         * @param screenAvailable 显示在哪块屏幕的可用区，默认取主屏
         */
        void show(const QString &text, ToastKind kind = ToastKind::Information, const QRect &screenAvailable = {});

        /// 当前显示的气泡文本，空表示没在显示（用例据此判自动隐藏有没有生效）
        [[nodiscard]] QString currentText() const;

        /// 当前气泡是否还盖着流式文字覆盖层
        [[nodiscard]] bool hasStreamingOverlay() const;

        /// 当前气泡里的流式覆盖层个数，用于判旧层有没有随文本一起销毁
        [[nodiscard]] int streamingOverlayCount() const;

        /// 当前气泡图标的描边色，应等于该条提示的语义色
        [[nodiscard]] QColor currentIconTint() const;

        /// 当前气泡文字列的上边距，按标签真实字体算出
        [[nodiscard]] int currentIconTextPad() const;

        /**
         * @brief 把当前气泡的流式动画向前推指定毫秒
         * @param milliseconds 本次推进的毫秒数，负值按 0 处理
         * @details 只给用例用：不依赖真实时钟，推进到时间轴终点即收工。
         */
        void advanceStreamingForTesting(int milliseconds);

    private:
        class Bubble;

        IconProvider       &m_icons;    ///< 图标出口，不拥有
        const ThemeManager &m_theme;    ///< 主题来源，不拥有
        Bubble             *m_bubble{}; ///< 当前气泡，同一时刻最多一个
    };

} // namespace GyanisScribe::Gui
