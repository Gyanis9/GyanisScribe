#include "Gui/Controls/ComboBox.h"

#include "Core/Geometry/ContinuousCorner.h"
#include "Gui/Controls/FontItemDelegate.h"
#include "Gui/Controls/ItemDelegate.h"
#include "Gui/Controls/ListView.h"
#include "Gui/Theme/ControlPalette.h"
#include "Gui/Theme/DesignTokens.h"
#include "Gui/Theme/PopupSurface.h"

#include <QFontMetrics>
#include <QPainter>
#include <QPen>

namespace
{
    /**
     * @brief 画下拉框的连续曲率底与状态边界
     * @param combo 目标下拉框
     * @param painter 目标画家
     * @note 两个下拉框的收起态只差基类文字，底色与描边同解，因此共用这一处
     */
    void paintComboChrome(QComboBox &combo, QPainter &painter)
    {
        const auto colors = GyanisScribe::Gui::inputColors(GyanisScribe::Gui::activePalette(), GyanisScribe::Gui::activeThemeKind(), combo.isEnabled(), false,
                                                          combo.hasFocus(), GyanisScribe::Gui::usesInlineFocusBorder(combo));
        const QPen pen    = colors.borderWidth > 0 ? QPen{colors.border, static_cast<qreal>(colors.borderWidth)} : QPen{Qt::NoPen};
        GyanisScribe::Core::paintContinuousCorner(painter, QRectF{combo.rect()}, GyanisScribe::Core::cornerShapeFor(GyanisScribe::Core::CornerRole::Control),
                                                  colors.fill, pen);
    }

    /**
     * @brief 让弹层窗口不铺系统底色，视图的圆角外才能真的透明
     * @param combo 目标下拉框
     * @details 弹层容器是显示时才建的原生窗口；透明属性必须在表面创建前落下，
     *          这里连同建好句柄的兜底重建一起交给 PopupSurface 处理
     */
    void preparePopupWindow(QComboBox &combo)
    {
        QWidget *popupWindow = combo.view() != nullptr ? combo.view()->window() : nullptr;
        if (popupWindow == nullptr || popupWindow == &combo)
        {
            return;
        }
        GyanisScribe::Gui::preparePopupSurface(popupWindow, GyanisScribe::Core::CornerRole::Control);
    }
} // namespace

namespace GyanisScribe::Gui
{
    ComboBox::ComboBox(QWidget *parent) : QComboBox(parent)
    {
        setProperty("squircle", true);
        setView(new ListView{this});
        preparePopupWindow(*this);
    }

    void ComboBox::showPopup()
    {
        QComboBox::showPopup();
        preparePopupWindow(*this);
        // 委托在视图构造时就已装好，Qt 打开弹层时不会改回来；这里只做类型核对，
        // 弹层被换成别的委托才补装，正常打开不再新建
        auto *popupView = view();
        if (popupView != nullptr && qobject_cast<ItemDelegate *>(popupView->itemDelegate()) == nullptr)
        {
            popupView->setItemDelegate(new ItemDelegate{popupView});
        }
    }

    void ComboBox::paintEvent(QPaintEvent *event)
    {
        {
            QPainter painter{this};
            paintComboChrome(*this, painter);
        }
        QComboBox::paintEvent(event);
    }

    FontComboBox::FontComboBox(QWidget *parent) : QFontComboBox(parent)
    {
        setProperty("squircle", true);
        setView(new ListView{this});
        // 视图构造时装的是通用委托；字体弹层要按族名预览，构造时就换成字体委托
        setItemDelegate(new FontItemDelegate{this});
        // 可编辑档会带出原生行编辑的方框与箭头；这里只做选字体，不需要输入过滤
        setEditable(false);
        // 与同一排的 QComboBox/QSpinBox 同高：自绘后没有原生行编辑撑高，补回 26
        setMinimumHeight(26);
        preparePopupWindow(*this);
    }

    void FontComboBox::showPopup()
    {
        QFontComboBox::showPopup();
        preparePopupWindow(*this);
        // 同 ComboBox：字体委托在构造时就已装好，只在被换掉时兜底补装，字体预览不会丢
        auto *popupView = view();
        if (popupView != nullptr && qobject_cast<FontItemDelegate *>(popupView->itemDelegate()) == nullptr)
        {
            popupView->setItemDelegate(new FontItemDelegate{popupView});
        }
    }

    void FontComboBox::paintEvent(QPaintEvent *event)
    {
        Q_UNUSED(event);
        QPainter painter{this};
        paintComboChrome(*this, painter);

        // 原生 QFontComboBox 的 paint 会再画一层行编辑方框与系统箭头；这一枚只取它的模型与弹层，
        // 收起态的文字与 chevron 在这里按其余输入类同一套样式自绘
        painter.setPen(activePalette().text);
        painter.setFont(font());
        const QRect textRect = rect().adjusted(12, 0, -26, 0);
        painter.drawText(textRect, Qt::AlignVCenter | Qt::AlignLeft, fontMetrics().elidedText(currentText(), Qt::ElideRight, textRect.width()));

        QPen chevron{activePalette().icon, 1.5};
        chevron.setCapStyle(Qt::RoundCap);
        painter.setPen(chevron);
        const int centerX = rect().right() - 15;
        const int centerY = rect().center().y();
        painter.drawLine(centerX - 3, centerY - 2, centerX, centerY + 2);
        painter.drawLine(centerX, centerY + 2, centerX + 3, centerY - 2);
    }
} // namespace GyanisScribe::Gui
