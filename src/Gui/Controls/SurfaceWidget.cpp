#include "Gui/Controls/SurfaceWidget.h"

#include "Gui/Theme/CornerStyle.h"
#include "Gui/Theme/DesignTokens.h"

#include <QPainter>
#include <QPen>

namespace
{
    /**
     * @brief 把铺底角色解成当前主题的语义色
     * @param role 铺底角色
     * @return 对应颜色；不铺底时返回透明
     */
    QColor fillColorFor(const GyanisScribe::Gui::SurfaceFillRole role)
    {
        const auto &palette = GyanisScribe::Gui::activePalette();
        switch (role)
        {
            case GyanisScribe::Gui::SurfaceFillRole::WindowBackground:
                return palette.windowBackground;
            case GyanisScribe::Gui::SurfaceFillRole::Surface:
                return palette.surface;
            case GyanisScribe::Gui::SurfaceFillRole::ControlFill:
                return palette.controlFill;
            case GyanisScribe::Gui::SurfaceFillRole::MutedFill:
                return palette.mutedFill;
            case GyanisScribe::Gui::SurfaceFillRole::AccentSoft:
                return palette.accentSoft;
            case GyanisScribe::Gui::SurfaceFillRole::None:
                break;
        }
        return Qt::transparent;
    }

    /**
     * @brief 画一块连续曲率的填充与描边
     * @param painter 目标画家
     * @param rect 目标矩形
     * @param shape 圆角形状
     * @param fill 填充色
     * @param border 描边色
     * @param borderWidth 描边宽度；0 表示不画
     */
    void paintSurface(QPainter &painter, const QRectF &rect, const GyanisScribe::Core::CornerShape &shape, const QColor &fill,
                      const QColor &border, const int borderWidth)
    {
        const QPen pen = borderWidth > 0 ? QPen{border, static_cast<qreal>(borderWidth)} : QPen{Qt::NoPen};
        GyanisScribe::Core::paintContinuousCorner(painter, rect, shape, fill, pen);
    }
} // namespace

namespace GyanisScribe::Gui
{
    SurfaceWidget::SurfaceWidget(const Core::CornerRole role, const SurfaceFillRole fillRole, QWidget *parent) :
        QWidget(parent), m_role(role), m_fillRole(fillRole)
    {
        setAutoFillBackground(false);
    }

    void SurfaceWidget::paintEvent(QPaintEvent *)
    {
        QPainter painter{this};
        QColor   border = m_borderColor;
        int      width  = m_borderWidth;
        // 高对比度档的自动边只补「调用方没要边」的那一档：显式描边的控件换的是颜色与状态，
        // 不能被一层静息黑边盖回去
        const bool hasExplicitBorder = m_borderColor.isValid() && m_borderWidth > 0;
        if (m_highContrastBorder && !hasExplicitBorder && activeThemeKind() == ThemeKind::HighContrast)
        {
            border = activePalette().borderStrong;
            width  = themeBorderWidth(ThemeKind::HighContrast);
        }
        // 卡片静息边：窗口底与卡片同值后，这圈线是圆角处唯一的边界
        if (m_outlineVisible && !hasExplicitBorder)
        {
            border = activePalette().borderStrong;
            width  = themeBorderWidth(activeThemeKind());
        }
        paintSurface(painter, QRectF{rect()}, Core::cornerShapeFor(m_role, QSizeF{size()}), fillColorFor(m_fillRole), border, width);
    }

    void SurfaceWidget::setFillRole(const SurfaceFillRole role)
    {
        m_fillRole = role;
        update();
    }

    void SurfaceWidget::setBorderColor(const QColor &color)
    {
        m_borderColor = color;
        update();
    }

    void SurfaceWidget::setBorderWidth(const int width)
    {
        m_borderWidth = width;
        update();
    }

    void SurfaceWidget::setHighContrastBorder(const bool enabled)
    {
        m_highContrastBorder = enabled;
        update();
    }

    void SurfaceWidget::setOutlineVisible(const bool visible)
    {
        m_outlineVisible = visible;
        update();
    }

    bool SurfaceWidget::outlineVisible() const
    {
        return m_outlineVisible;
    }

    SurfaceLabel::SurfaceLabel(const SurfaceFillRole fillRole, const Core::CornerRole role, QWidget *parent) :
        QLabel(parent), m_fillRole(fillRole), m_role(role)
    {
        setAutoFillBackground(false);
    }

    void SurfaceLabel::paintEvent(QPaintEvent *event)
    {
        QPainter painter{this};
        QColor   border = m_borderColor;
        int      width  = m_borderColor.isValid() ? themeBorderWidth(activeThemeKind()) : 0;
        // 显式描边优先：录制态的主色边要压过高对比度档的静息黑边，自动边只补没有显式边的那一档
        if (m_highContrastBorder && !m_borderColor.isValid() && activeThemeKind() == ThemeKind::HighContrast)
        {
            border = activePalette().borderStrong;
            width  = themeBorderWidth(ThemeKind::HighContrast);
        }
        paintSurface(painter, QRectF{rect()}, Core::cornerShapeFor(m_role), fillColorFor(m_fillRole), border, width);
        QLabel::paintEvent(event);
    }

    void SurfaceLabel::setFillRole(const SurfaceFillRole role)
    {
        m_fillRole = role;
        update();
    }

    void SurfaceLabel::setBorderColor(const QColor &color)
    {
        m_borderColor = color;
        update();
    }

    void SurfaceLabel::setHighContrastBorder(const bool enabled)
    {
        m_highContrastBorder = enabled;
        update();
    }
} // namespace GyanisScribe::Gui
