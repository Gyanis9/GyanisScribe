/**
 * @file SquircleStyle.h
 * @brief 应用级代理样式：接管菜单与提示弹层的原生面板，置空自绘输入类的原生边框
 * @author Gyanis
 * @date 2026-11-08
 * @version 0.0.1
 * @copyright Copyright (c) . All rights reserved.
 */

#pragma once

#include <QProxyStyle>

namespace GyanisScribe::Gui
{
    /**
     * @brief 连续曲率控件共用的代理样式
     * @details 菜单面板与菜单框由 Gui::Menu 自绘：原生风格若照常画一层方形面板，
     *          自绘的连续曲率高亮会被它盖住，角上的透明区也会露出方底。菜单拦截只对
     *          Gui::Menu 与不基于 QMenu 的自绘弹层生效，系统对话框与文本控件的标准右键菜单
     *          仍由原生风格画。提示弹层没有自绘类，
     *          原生面板整层改成连续曲率面板，窗口本身也让出透明底并关掉原生掩膜。
     *          输入类的自绘底同理：带 squircle 属性的控件（及其内部的子行编辑）不再让原生风格
     *          画输入面板、边框与下拉/微调框主体，否则自绘的连续曲率底会被一层方底盖掉。
     *          滚动条与滑块的几何也在这里接管：滑块、轨道与翻页区都按自绘用的同一组常量给出，
     *          命中测试与拖动换算因此和画出来的手柄重合。
     */
    class SquircleStyle : public QProxyStyle
    {
    public:
        /**
         * @brief 把本样式装成应用样式
         * @note 必须在 QApplication 构造之后、第一次 setStyleSheet 之前调用；
         *       进程内只装一次，重复调用不会再造第二份样式
         */
        static void install();

        /**
         * @brief 提示弹层窗口接入透明底
         * @details 重写 QProxyStyle::polish()：Qt 内部的 QTipLabel 按类名认出来，
         *          打开 WA_TranslucentBackground、清掉自动填底，窗口的可见像素只来自
         *          PE_PanelTipLabel 画出的连续曲率面板；其余控件原样交给基类
         * @param widget 待打磨的控件
         * @note 必须在窗口创建与首次显示之前落下：不透明窗口会在 8px 面板的角外
         *       铺一层方底，圆角就白画了
         */
        void polish(QWidget *widget) override;

    protected:
        /**
         * @brief 提示弹层不装原生掩膜
         * @details 重写 QProxyStyle::styleHint()：QTipLabel::resizeEvent 会按 SH_ToolTip_Mask
         *          装一圈原生掩膜，掩膜按矩形或原生圆角裁角，会把连续曲率面板的角切掉；
         *          对提示弹层返回 0，其余提示原样交给基类
         * @param hint 待查询的样式提示
         * @param option 绘制参数
         * @param widget 目标控件
         * @param returnData 提示的附加返回值
         * @return 提示弹层为 0，其余由基类给出
         */
        int styleHint(StyleHint hint, const QStyleOption *option, const QWidget *widget, QStyleHintReturn *returnData) const override;

        /**
         * @brief 接管菜单与提示弹层的原生面板，置空自绘输入类的原生输入面板与边框
         * @details 重写 QProxyStyle::drawPrimitive()：非原生 QMenu 的 PE_PanelMenu 与
         *          PE_FrameMenu 直接返回（Gui::Menu 与同样自绘面板的下拉弹层容器都在此列），
         *          Qt 原生 QMenu 落回原生风格；PE_PanelTipLabel 按紧凑档画出连续曲率面板与描边；
         *          带 squircle 属性的控件（或其子控件）的 PE_PanelLineEdit、PE_FrameLineEdit
         *          与 QAbstractScrollArea 的 PE_Frame 也直接返回，其余图元原样交给基类
         * @param element 待绘制的图元
         * @param option 绘制参数
         * @param painter 目标画家
         * @param widget 目标控件
         * @note 提示弹层的面板走样式表引擎的回落路径：QToolTip 的规则里必须没有边框声明，
         *       否则 QStyleSheetStyle 会自己画完 PE_PanelTipLabel，这里不会被执行
         */
        void drawPrimitive(PrimitiveElement element, const QStyleOption *option, QPainter *painter, const QWidget *widget) const override;

        /**
         * @brief 自绘菜单的项只画内容，不画原生底与空区底
         * @details 重写 QProxyStyle::drawControl()：非原生 QMenu 的 CE_MenuEmptyArea 直接返回；
         *          Gui::Menu 的 CE_MenuItem 清掉选中态与 Button、Highlight 两种画刷后再交基类。
         *          Qt 原生 QMenu 两类都交给原生风格，选中高亮与面板照常画出
         * @param element 待绘制的控件元素
         * @param option 绘制参数
         * @param painter 目标画家
         * @param widget 目标控件
         * @note 当前项高亮与面板由 Gui::Menu 自绘；原生风格在这里补的不透明底
         *       会盖住高亮，也会在菜单边缘露出一条与主题不合的色带
         */
        void drawControl(ControlElement element, const QStyleOption *option, QPainter *painter, const QWidget *widget) const override;

        /**
         * @brief 自绘输入类不交给原生风格画下拉框与微调框主体
         * @details 重写 QProxyStyle::drawComplexControl()：带 squircle 属性的控件画 CC_ComboBox
         *          与 CC_SpinBox 时整支返回；当前项文字由 QComboBox::paintEvent 随后单独发起的
         *          CE_ComboBoxLabel 绘制，不受影响。原生下拉箭头属于 CC_ComboBox 这一支，
         *          随主体一起跳过；应用里的箭头由三份主题 QSS 的 QComboBox::down-arrow 提供，
         *          不套样式表的 QComboBox 不会自带箭头
         * @param control 待绘制的复杂控件
         * @param option 绘制参数
         * @param painter 目标画家
         * @param widget 目标控件
         */
        void drawComplexControl(ComplexControl control, const QStyleOptionComplex *option, QPainter *painter, const QWidget *widget) const override;

        /**
         * @brief 滚动条各子控件按自绘几何给出
         * @details 重写 QProxyStyle::subControlRect()：滚动条的滑块与轨道取自
         *          Gui::scrollBarSliderRect()、Gui::scrollBarTrackRect()，翻页区是轨道去掉手柄后的两段，
         *          两端步进按钮返回空矩形（本应用的滚动条不画按钮，命中也不该落在那两条上）。
         *          其余复杂控件原样交给基类
         * @param control 待解的复杂控件
         * @param option 绘制参数
         * @param subControl 待解的子控件
         * @param widget 目标控件
         * @return 子控件矩形；不是滚动条滑块几何时由基类计算
         * @note 命中测试与 QScrollBar 的拖动换算都会问到这里：滑块矩形必须与
         *       Gui::ScrollBar::paintEvent() 画出来的一致，轨道必须与自绘的内轨道一致
         */
        QRect subControlRect(ComplexControl control, const QStyleOptionComplex *option, SubControl subControl, const QWidget *widget) const override;

        /**
         * @brief 滚动条与滑块的度量对齐自绘数值
         * @details 重写 QProxyStyle::pixelMetric()：PM_ScrollBarExtent 返回滚动条粗细、
         *          PM_ScrollBarSliderMin 返回手柄最短长度，滚动条的建议尺寸与原生风格里的其它换算
         *          都落在自绘用的同一档；Gui::Slider 的 PM_SliderLength 返回手柄边长，
         *          拖动换算与命中测试才会和画出来的手柄重合；其余度量原样交给基类
         * @param metric 待取的度量
         * @param option 绘制参数
         * @param widget 目标控件
         * @return 滚动条与滑块度量为自绘常量，其余由基类给出
         */
        int pixelMetric(PixelMetric metric, const QStyleOption *option, const QWidget *widget) const override;
    };
} // namespace GyanisScribe::Gui
