/**
 * @file ColorPickerOverlay.h
 * @brief 取色遮罩：全屏冻帧、放大镜跟光标、点一下就复制颜色
 * @author Gyanis
 * @date 2026-10-10
 * @version 0.0.1
 * @copyright Copyright (c) . All rights reserved.
 */

#pragma once

#include "Base/Types.h"
#include "Core/Pick/ColorFormat.h"
#include "Core/Pick/PickGeometry.h"

#include <QColor>
#include <QPoint>
#include <QWidget>

#include <vector>

namespace GyanisScribe::Gui
{
    class ThemeManager;
} // namespace GyanisScribe::Gui

namespace GyanisScribe::Gui
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
         * @param casing HEX 字母那几位的大小写（写法不是 HEX 时不起作用）
         * @param view 放大镜视野档；「不显示」只收掉那枚像素插图，当前像素的十字与信息条照旧
         * @param theme 主题管理器，放大镜边框与文字色随主题
         * @param parent Qt 父对象
         */
        ColorPickerOverlay(Base::CapturedImage frame, const QRect &screenGeometry, Core::ColorFormat format, Core::HexCasing casing, Core::MagnifierView view,
                           const Gui::ThemeManager &theme, QWidget *parent = nullptr);

        /**
         * @brief 某一屏幕坐标处的颜色
         * @param globalPos 屏幕坐标
         * @return 该像素的颜色；坐标落在冻帧之外时夹到边缘取色
         */
        [[nodiscard]] QColor colorAt(const QPoint &globalPos) const;

        /**
         * @brief 当前光标的屏幕坐标
         * @return 遮罩内最后一次移动到的位置，初始为构造时的光标位置
         * @note 不叫 cursor()：那会和 QWidget::cursor() 同名，经基类指针一调拿到的是另一个东西
         */
        [[nodiscard]] QPoint pointerPosition() const;

        /**
         * @brief 当前颜色按当前写法排好的文本
         * @return 与 colorPicked 里那条文本同源
         */
        [[nodiscard]] QString currentText() const;

        /**
         * @brief 这一轮会不会画出那枚像素插图
         * @return 视野档不是「不显示」时为真
         * @details 判据要有出口：画在遮罩里的东西没法从外面看，而「设置里选了不显示却没接上」
         *          正是只能靠这一类核对才发现的错。
         */
        [[nodiscard]] bool isMagnifierVisible() const noexcept;

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
        /**
         * @brief 画遮罩：冻帧、压暗层、放大镜与底部色块行
         * @details 重写 QWidget::paintEvent()：抓来的冻帧按逻辑尺寸铺满整窗（高 DPI 下帧比逻辑
         *          尺寸大，不缩就会画出屏幕外），再压一层半透明黑，最后按光标位置画放大镜与色块行。
         *          帧还没到手时什么都不画，窗口留成默认底色。
         * @param event 绘制事件，本实现不读它
         */
        void paintEvent(QPaintEvent *event) override;

        /**
         * @brief 记下光标位置并重绘
         * @details 重写 QWidget::mouseMoveEvent()：光标取全局坐标（放大镜与色块命中都按全局算），
         *          顺带算出这一格是否压在色块行上，然后请求重绘；不改可见性，也不动窗口几何。
         * @param event 鼠标移动事件
         */
        void mouseMoveEvent(QMouseEvent *event) override;

        /**
         * @brief 左键取色，右键等同 Esc
         * @details 重写 QWidget::mousePressEvent()：压在色块行上就取那一块（色块行是给「再取一次
         *          最近用过的色」留的），没压住就按光标取底下的像素，两条出口都随即收窗。右键不发
         *          颜色，只发 cancelled()——取色中途最想做的就是把这一层关掉。
         * @param event 鼠标按下事件
         */
        void mousePressEvent(QMouseEvent *event) override;

        /**
         * @brief 认 Esc 与数字键 1..8，其余交回 Qt
         * @details 重写 QWidget::keyPressEvent()：Esc 作废这一轮；不带修饰键的 1..8 取色块行上
         *          对应那一块，与点击取那一块共用一条出口，光标动没动都不影响；色块行没那么长时
         *          这个键不作处理，继续交回基类，输入法与系统组合键因此仍归它们自己。
         * @param event 键盘按下事件
         */
        void keyPressEvent(QKeyEvent *event) override;

    private:
        /// 取当前色并发信号、随后关闭遮罩
        void pickAndClose();

        /// 取定一个颜色：发信号、随后关闭遮罩；色块行与光标取色共用这一处出口
        void deliverColor(const QColor &color);

        /// 某个屏幕坐标压在色块行的第几块上；不在任何一块上时返回 -1
        [[nodiscard]] int swatchIndexAt(const QPoint &globalPos) const;

        Base::CapturedImage      m_frame;              ///< 这一屏的冻帧
        QRect                    m_screenGeometry;     ///< 冻帧对应屏幕的几何
        Core::ColorFormat        m_format;             ///< 复制用的写法
        Core::HexCasing          m_hexCasing;          ///< 写法是 HEX 时字母那几位的大小写
        Core::MagnifierView      m_magnifierView;      ///< 放大镜视野档，「不显示」时只画十字与信息条
        const Gui::ThemeManager &m_theme;              ///< 主题管理器，画放大镜边框用主色
        QPoint                   m_cursor;             ///< 当前光标的屏幕坐标
        std::vector<QColor>      m_recent;             ///< 最近取色清单，最近的在前；命中的下标与色块行同序
        int                      m_hoveredSwatch = -1; ///< 光标压在色块行第几块上；-1 表示没压在任何一块上
    };

} // namespace GyanisScribe::Gui
