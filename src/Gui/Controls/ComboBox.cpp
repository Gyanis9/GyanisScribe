#include "Gui/Controls/ComboBox.h"

#include "Core/Geometry/ContinuousCorner.h"
#include "Gui/Controls/FontItemDelegate.h"
#include "Gui/Controls/ItemDelegate.h"
#include "Gui/Controls/ListView.h"
#include "Gui/Theme/ControlPalette.h"
#include "Gui/Theme/DesignTokens.h"

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
     * @details 弹层容器是独立的顶层窗口；面板与项高亮都由 Gui::ListView 与 ItemDelegate 画，
     *          容器自己不再填底。不打开透明背景时，视图的透明圆角会落在容器的方底上
     */
    void preparePopupWindow(QComboBox &combo)
    {
        QWidget *popupWindow = combo.view() != nullptr ? combo.view()->window() : nullptr;
        if (popupWindow == nullptr || popupWindow == &combo)
        {
            return;
        }
        popupWindow->setAttribute(Qt::WA_TranslucentBackground);
        popupWindow->setAutoFillBackground(false);
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
        // 弹层创建时 QComboBox 会装回自己的私有委托，基类返回后重设一次
        if (auto *popupView = view(); popupView != nullptr)
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
        preparePopupWindow(*this);
    }

    void FontComboBox::showPopup()
    {
        QFontComboBox::showPopup();
        preparePopupWindow(*this);
        // 弹层创建时基类会装回自己的私有委托，基类返回后重设一次，字体预览才不会丢
        if (auto *popupView = view(); popupView != nullptr)
        {
            popupView->setItemDelegate(new FontItemDelegate{popupView});
        }
    }

    void FontComboBox::paintEvent(QPaintEvent *event)
    {
        {
            QPainter painter{this};
            paintComboChrome(*this, painter);
        }
        QFontComboBox::paintEvent(event);
    }
} // namespace GyanisScribe::Gui
