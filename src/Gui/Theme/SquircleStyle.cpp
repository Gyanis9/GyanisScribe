#include "Gui/Theme/SquircleStyle.h"

#include "Core/Geometry/ContinuousCorner.h"
#include "Gui/Controls/Menu.h"
#include "Gui/Controls/ScrollBar.h"
#include "Gui/Theme/CornerStyle.h"
#include "Gui/Theme/DesignTokens.h"

#include <QAbstractScrollArea>
#include <QApplication>
#include <QPainter>
#include <QPalette>
#include <QPen>
#include <QRectF>
#include <QStyleOptionMenuItem>
#include <QStyleOptionSlider>
#include <QWidget>

namespace
{
    /// 进程内只装一次：重复包装会把上一次的代理套进新代理里
    bool s_installed = false;

    /**
     * @brief 控件是否是由本应用自绘的菜单
     * @param widget 待判定控件
     * @return 控件是 Gui::Menu 时为真
     * @details Gui::Menu 没有 Q_OBJECT，不能用 qobject_cast；菜单与代理样式同在 Gui 模块内，
     *          RTTI 判定足够。
     */
    bool isSquircleMenu(const QWidget *widget)
    {
        return dynamic_cast<const GyanisScribe::Gui::Menu *>(widget) != nullptr;
    }

    /**
     * @brief 是否压掉原生菜单面板与空区底
     * @param widget 目标控件
     * @return 目标不是 Qt 原生 QMenu 时为真
     * @details Gui::Menu 自绘菜单面板；QComboBox 的弹层容器也借 PE_PanelMenu 与 CE_MenuEmptyArea
     *          铺底，而它的面板由 Gui::ListView 自绘，同样要压掉。要放行的只有 Qt 自己造的
     *          原生 QMenu：系统文本控件的标准右键菜单属于这一类，面板与高亮必须落回原生风格。
     */
    bool suppressesNativeMenuChrome(const QWidget *widget)
    {
        return qobject_cast<const QMenu *>(widget) == nullptr || isSquircleMenu(widget);
    }

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
        // 自绘菜单的面板与边框都由 Gui::Menu 按连续曲率画；原生风格照常落笔就会盖住高亮、
        // 并在圆角外补一块方底。系统文本控件里的原生 QMenu 不是自绘菜单，必须交给原生风格画
        if ((element == PE_PanelMenu || element == PE_FrameMenu) && suppressesNativeMenuChrome(widget))
        {
            return;
        }
        // 提示弹层没有自绘类：面板与描边就在这里按紧凑档画出。QSS 的 QToolTip 规则只留文字色
        // 与内边距，不能有任何边框声明——样式表引擎只要判得出可画内容，就会自己画完这一层，
        // 本方法收不到这次调用
        if (element == PE_PanelTipLabel)
        {
            painter->save();
            painter->setRenderHint(QPainter::Antialiasing, true);
            Core::paintContinuousCorner(*painter, QRectF{option->rect}.adjusted(0.5, 0.5, -0.5, -0.5), Core::cornerShapeFor(Core::CornerRole::Compact), activePalette().surface,
                                        QPen{activePalette().borderStrong, static_cast<qreal>(themeBorderWidth(activeThemeKind()))});
            painter->restore();
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
        // 空区底只对自绘菜单与同样自绘面板的下拉弹层跳过；原生 QMenu 的空区照常交给原生风格
        if (element == CE_MenuEmptyArea && suppressesNativeMenuChrome(widget))
        {
            return;
        }
        // 菜单项的内容清理只对自绘菜单生效：原生菜单的选中高亮必须留住
        if (element == CE_MenuItem && isSquircleMenu(widget))
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

    QRect SquircleStyle::subControlRect(const ComplexControl control, const QStyleOptionComplex *option, const SubControl subControl, const QWidget *widget) const
    {
        if (control == CC_ScrollBar && widget != nullptr)
        {
            const auto *scrollBar = qstyleoption_cast<const QStyleOptionSlider *>(option);
            if (scrollBar != nullptr)
            {
                const QRect track    = scrollBarTrackRect(*scrollBar, *widget).toRect();
                const QRect slider   = scrollBarSliderRect(*scrollBar, *widget).toRect();
                const bool  vertical = scrollBar->orientation == Qt::Vertical;
                if (!track.isEmpty() && !slider.isEmpty())
                {
                    switch (subControl)
                    {
                        case SC_ScrollBarSlider:
                            return slider;
                        case SC_ScrollBarGroove:
                            return track;
                        case SC_ScrollBarSubLine:
                        case SC_ScrollBarAddLine:
                            // 自绘滚动条没有两端的步进按钮，命中也不该落在那两条上
                            return {};
                        case SC_ScrollBarSubPage:
                            return vertical ? QRect{track.left(), track.top(), track.width(), slider.top() - track.top()}
                                            : QRect{track.left(), track.top(), slider.left() - track.left(), track.height()};
                        case SC_ScrollBarAddPage:
                            return vertical ? QRect{track.left(), slider.top() + slider.height(), track.width(), track.top() + track.height() - slider.top() - slider.height()}
                                            : QRect{slider.left() + slider.width(), track.top(), track.left() + track.width() - slider.left() - slider.width(), track.height()};
                        default:
                            break;
                    }
                }
            }
        }
        return QProxyStyle::subControlRect(control, option, subControl, widget);
    }

    int SquircleStyle::pixelMetric(const PixelMetric metric, const QStyleOption *option, const QWidget *widget) const
    {
        switch (metric)
        {
            case PM_ScrollBarExtent:
                return 10;
            case PM_ScrollBarSliderMin:
                return 24;
            default:
                break;
        }
        return QProxyStyle::pixelMetric(metric, option, widget);
    }
} // namespace GyanisScribe::Gui
