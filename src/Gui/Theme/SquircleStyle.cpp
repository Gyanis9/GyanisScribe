#include "Gui/Theme/SquircleStyle.h"

#include <QApplication>
#include <QPalette>
#include <QStyleOptionMenuItem>

namespace
{
    /// 进程内只装一次：重复包装会把上一次的代理套进新代理里
    bool s_installed = false;
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
        // 并在圆角外补一块方底。提示面板在后续切片里另由本样式接管，这里只有菜单这两档
        if (element == PE_PanelMenu || element == PE_FrameMenu)
        {
            return;
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
} // namespace GyanisScribe::Gui
