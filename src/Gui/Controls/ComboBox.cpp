#include "Gui/Controls/ComboBox.h"

#include "Core/Geometry/ContinuousCorner.h"
#include "Gui/Controls/FontItemDelegate.h"
#include "Gui/Controls/ItemDelegate.h"
#include "Gui/Controls/ListView.h"
#include "Gui/Theme/ControlPalette.h"
#include "Gui/Theme/DesignTokens.h"
#include "Gui/Theme/PopupSurface.h"

#include <QApplication>
#include <QFontMetrics>
#include <QPainter>
#include <QPen>
#include <QVariantAnimation>

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

    /// 弹层出现的过渡时长与上浮距离：短到不挡操作，长到看得出方向
    constexpr int kPopupRevealMilliseconds = 120;
    constexpr int kPopupRiseDistance       = 6;

    /**
     * @brief 让刚显示的弹层从下方 6px 淡入回位
     * @param popup 弹层窗口
     * @details 动画只在系统开启「界面动效」时播放；窗口透明度会强制 Windows 走分层窗口，
     *          圆角掩膜已由 PopupSurface 在首帧前落好，二者互不改写
     */
    void playPopupReveal(QWidget *popup)
    {
        if (popup == nullptr || !QApplication::isEffectEnabled(Qt::UI_AnimateCombo))
        {
            return;
        }
        const QPoint target = popup->pos();
        popup->move(target + QPoint{0, kPopupRiseDistance});
        auto *animation = new QVariantAnimation{popup};
        animation->setDuration(kPopupRevealMilliseconds);
        animation->setStartValue(0.0);
        animation->setEndValue(1.0);
        animation->setEasingCurve(QEasingCurve::OutCubic);
        QObject::connect(animation, &QVariantAnimation::valueChanged, popup,
                         [popup, target](const QVariant &value)
                         {
                             const qreal progress = value.toReal();
                             popup->setWindowOpacity(progress);
                             popup->move(target + QPoint{0, qRound((1.0 - progress) * kPopupRiseDistance)});
                         });
        QObject::connect(animation, &QVariantAnimation::finished, popup,
                         [popup, target]
                         {
                             popup->setWindowOpacity(1.0);
                             popup->move(target);
                         });
        animation->start(QAbstractAnimation::DeleteWhenStopped);
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
        // 首帧必须是透明的：先置 0 再显示，避免淡入之前闪一帧不透明弹层
        QWidget *popupWindow = view() != nullptr ? view()->window() : nullptr;
        if (popupWindow != nullptr && QApplication::isEffectEnabled(Qt::UI_AnimateCombo))
        {
            popupWindow->setWindowOpacity(0.0);
        }
        QComboBox::showPopup();
        preparePopupWindow(*this);
        if (popupWindow != nullptr)
        {
            playPopupReveal(popupWindow);
        }
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
        QWidget *popupWindow = view() != nullptr ? view()->window() : nullptr;
        if (popupWindow != nullptr && QApplication::isEffectEnabled(Qt::UI_AnimateCombo))
        {
            popupWindow->setWindowOpacity(0.0);
        }
        QFontComboBox::showPopup();
        preparePopupWindow(*this);
        if (popupWindow != nullptr)
        {
            playPopupReveal(popupWindow);
        }
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
