#include "Gui/Controls/ComboBox.h"

#include "Core/Geometry/ContinuousCorner.h"
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
} // namespace

namespace GyanisScribe::Gui
{
    ComboBox::ComboBox(QWidget *parent) : QComboBox(parent)
    {
        setProperty("squircle", true);
    }

    void ComboBox::paintEvent(QPaintEvent *event)
    {
        QPainter painter{this};
        paintComboChrome(*this, painter);
        QComboBox::paintEvent(event);
    }

    FontComboBox::FontComboBox(QWidget *parent) : QFontComboBox(parent)
    {
        setProperty("squircle", true);
    }

    void FontComboBox::paintEvent(QPaintEvent *event)
    {
        QPainter painter{this};
        paintComboChrome(*this, painter);
        QFontComboBox::paintEvent(event);
    }
} // namespace GyanisScribe::Gui
