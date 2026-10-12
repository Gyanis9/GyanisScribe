/**
 * @file ColorPickerOverlay.h
 * @brief 取色遮罩：全屏冻帧、放大镜跟光标、点一下就复制颜色
 * @author Gyanis
 * @date 2026-10-10
 * @version 0.0.1
 * @copyright Copyright (c) . All rights reserved.
 */

#pragma once

#include "Core/Types.h"
#include "Pick/ColorFormat.h"
#include "Pick/PickGeometry.h"

#include <QColor>
#include <QPoint>
#include <QWidget>

#include <vector>

namespace GyanisScribe::Ui
{
    class ThemeManager;
} // namespace GyanisScribe::Ui

namespace GyanisScribe::Pick
{

    /**
     * @brief 取色遮罩
     * @details 显示的是按下热键那一刻抓的那一帧，不是实时画面：放大镜要看的正是当前帧，
     *          实时抓屏会让鼠标一停画面就变，颜色抄不准。
     *          左键点 = 取这个颜色并关闭，Esc = 不取直接关闭。
     */
    class ColorPickerOverlay : public QWidget
    {
        Q_OBJECT

    public:
        /**
         * @brief 构造遮罩
         * @param frame 这一屏的冻帧
         * @param screenGeometry frame 对应屏幕的几何，屏幕坐标
         * @param format 复制用的颜色写法
         * @param theme 主题管理器，放大镜边框与文字色随主题
         * @param parent Qt 父对象
         */
        ColorPickerOverlay(const Core::CapturedImage &frame, const QRect &screenGeometry, ColorFormat format, const Ui::ThemeManager &theme, QWidget *parent = nullptr);

        /**
         * @brief 某一屏幕坐标处的颜色
         * @param globalPos 屏幕坐标
         * @return 该像素的颜色；坐标落在冻帧之外时夹到边缘取色
         */
        [[nodiscard]] QColor colorAt(const QPoint &globalPos) const;

        /**
         * @brief 当前光标的屏幕坐标
         * @return 遮罩内最后一次移动到的位置，初始为构造时的光标位置
         */
        [[nodiscard]] QPoint cursor() const;

        /**
         * @brief 当前颜色按当前写法排好的文本
         * @return 与 colorPicked 里那条文本同源
         */
        [[nodiscard]] QString currentText() const;

        /**
         * @brief 交给遮罩的「最近取色」清单
         * @param colors 颜色列表，最近的在前；空表不摆色块行
         * @details 摆在屏幕底边中间：点一下就是取那个颜色（按当前写法复制并关闭），
         *          数字键 1..8 取第几个。同样的颜色重复取工作时不必回原处重新对像素
         */
        void setRecentColors(std::vector<QColor> colors);

    signals:
        /**
         * @brief 用户取定了一个颜色
         * @param color 颜色本体
         * @param text 按当前写法排好的文本，直接可粘进 CSS 或编辑器
         */
        void colorPicked(const QColor &color, const QString &text);

        /// 用户按 Esc 放弃取色
        void cancelled();

    protected:
        void paintEvent(QPaintEvent *event) override;
        void mouseMoveEvent(QMouseEvent *event) override;
        void mousePressEvent(QMouseEvent *event) override;
        void keyPressEvent(QKeyEvent *event) override;

    private:
        /// 取当前色并发信号、随后关闭遮罩
        void pickAndClose();

        /// 取定一个颜色：发信号、随后关闭遮罩；色块行与光标取色共用这一处出口
        void deliverColor(const QColor &color);

        /// 某个屏幕坐标压在色块行的第几块上；不在任何一块上时返回 -1
        [[nodiscard]] int swatchIndexAt(const QPoint &globalPos) const;

        Core::CapturedImage     m_frame;              ///< 这一屏的冻帧
        QRect                   m_screenGeometry;     ///< 冻帧对应屏幕的几何
        ColorFormat             m_format;             ///< 复制用的写法
        const Ui::ThemeManager &m_theme;              ///< 主题管理器，画放大镜边框用主色
        QPoint                  m_cursor;             ///< 当前光标的屏幕坐标
        std::vector<QColor>     m_recent;             ///< 最近取色清单，最近的在前；命中的下标与色块行同序
        int                     m_hoveredSwatch = -1; ///< 光标压在色块行第几块上；-1 表示没压在任何一块上
    };

} // namespace GyanisScribe::Pick
