#include "Gui/Theme/SquircleStyle.h"

#include <QAbstractScrollArea>
#include <QApplication>
#include <QPalette>
#include <QStyleOptionMenuItem>
#include <QWidget>

namespace
{
    /// 进程内只装一次：重复包装会把上一次的代理套进新代理里
    bool s_installed = false;

    /**
     * @brief 控件是否由自己铺底并画边界
     * @param widget 待判定控件
     * @return 控件自身或直接父控件带 squircle 属性时为真
     * @details 输入类在构造时打上 squircle 属性；微调框与可编辑下拉框内部的行编辑是子控件，
     *          它们自己没打属性，但原生面板同样会盖住外层的自绘底，所以父级判定也要算。
     */
    bool paintsItsOwnChrome(const QWidget *widget)
    {
        if (widget == nullptr)
        {
            return false;
        }
        if (widget->property("squircle").toBool())
        {
            return true;
        }
        const auto *parent = widget->parentWidget();
        return parent != nullptr && parent->property("squircle").toBool();
    }
} // namespace

namespace GyanisScribe::Gui
{
    void SquircleStyle::install()
    {
        if (!s_installed)
        {
            QApplication::setStyle(new SquircleStyle);
            s_installed = true;
        }
    }

    void SquircleStyle::drawPrimitive(const PrimitiveElement element, const QStyleOption *option, QPainter *painter, const QWidget *widget) const
    {
        // 菜单的面板与边框都由 Gui::Menu 按连续曲率自绘；原生风格照常落笔就会盖住高亮、
        // 并在圆角外补一块方底
        if (element == PE_PanelMenu || element == PE_FrameMenu)
        {
            return;
        }
        // 输入类的连续曲率底同样不能被原生输入面板、输入边框与滚动区边框盖掉
        if (paintsItsOwnChrome(widget))
        {
            if (element == PE_PanelLineEdit || element == PE_FrameLineEdit)
            {
                return;
            }
            if (element == PE_Frame && qobject_cast<const QAbstractScrollArea *>(widget) != nullptr)
            {
                return;
            }
        }
        QProxyStyle::drawPrimitive(element, option, painter, widget);
    }

    void SquircleStyle::drawControl(const ControlElement element, const QStyleOption *option, QPainter *painter, const QWidget *widget) const
    {
        if (element == CE_MenuEmptyArea)
        {
            return;
        }
        if (element == CE_MenuItem)
        {
            if (const auto *item = qstyleoption_cast<const QStyleOptionMenuItem *>(option); item != nullptr)
            {
                // 原生风格会整块铺 Button/Highlight 两种不透明底；这里清掉选中态与两种画刷，
                // 只留文字、勾、图标与子菜单箭头，底下由 Gui::Menu 画的连续曲率高亮才看得见
                QStyleOptionMenuItem content{*item};
                content.state &= ~(State_Selected | State_Sunken);
                content.palette.setBrush(QPalette::Button, Qt::NoBrush);
                content.palette.setBrush(QPalette::Highlight, Qt::NoBrush);
                QProxyStyle::drawControl(element, &content, painter, widget);
                return;
            }
        }
        QProxyStyle::drawControl(element, option, painter, widget);
    }

    void SquircleStyle::drawComplexControl(const ComplexControl control, const QStyleOptionComplex *option, QPainter *painter, const QWidget *widget) const
    {
        // 下拉框与微调框的主体是原生底加边框；自绘类已经铺好连续曲率底，这里整支跳过这两个复杂控件。
        // 样式表路径不会走到本方法：QStyleSheetStyle 自己画边框与 ::down-arrow。
        // CC_ComboBox 这一支里还带着原生下拉箭头，随主体一起跳过之后，不自绘箭头的下拉框就没有箭头；
        // 应用里的下拉框依赖三份主题 QSS 的 QComboBox::down-arrow 提供箭头，不套样式表的 QComboBox 不会自带箭头。
        if (paintsItsOwnChrome(widget) && (control == CC_ComboBox || control == CC_SpinBox))
        {
            return;
        }
        QProxyStyle::drawComplexControl(control, option, painter, widget);
    }
} // namespace GyanisScribe::Gui
