#include "Gui/Controls/CheckBox.h"

#include "Core/Geometry/ContinuousCorner.h"
#include "Gui/Theme/CornerStyle.h"
#include "Gui/Theme/DesignTokens.h"

#include <QEnterEvent>
#include <QEvent>
#include <QPainter>
#include <QPainterPath>
#include <QStyle>
#include <QStyleOptionButton>

namespace GyanisScribe::Gui
{
    CheckBox::CheckBox(QWidget *parent) : QCheckBox(parent), m_spring(this)
    {
        m_spring.setSpec(kPopSpring);
        m_spring.setImmediate(1.0);
        // 弹簧只驱动绘制缩放：重绘范围收在指示器那一块，不带着整行文字一起重画
        QObject::connect(&m_spring, &SpringValue::valueChanged, this,
                         [this](const qreal value)
                         {
                             m_checkScale = value;
                             update(indicatorRect());
                         });
        QObject::connect(this, &QCheckBox::toggled, this, &CheckBox::onToggled);
    }

    void CheckBox::setAnimationsEnabled(const bool enabled)
    {
        // 关闭时 SpringValue 把进行中的过渡落到目标，勾随之回到满幅
        m_spring.setAnimationsEnabled(enabled);
    }

    qreal CheckBox::checkScale() const noexcept
    {
        return m_checkScale;
    }

    void CheckBox::setCheckScale(const qreal scale)
    {
        m_checkScale = scale;
        update(indicatorRect());
    }

    void CheckBox::advanceCheckAnimationForTesting(const int milliseconds)
    {
        m_spring.advanceBy(milliseconds);
    }

    QRect CheckBox::indicatorRectForTesting(const CheckBox &box)
    {
        return box.indicatorRect();
    }

    void CheckBox::onToggled(const bool checked)
    {
        if (!checked)
        {
            // 取消勾选不弹：勾已经消失，缩放动画没有可看的对象
            m_spring.stop();
            m_spring.setImmediate(1.0);
            return;
        }
        // 先落到 0.55 再弹回 1.0：从满幅起步看不出「弹」这一下
        m_spring.setImmediate(0.55);
        m_spring.animateTo(1.0);
    }

    QRect CheckBox::indicatorRect() const
    {
        // 与 QSS 同源：样式表接管 ::indicator 后，方框落点只能问 style 要
        QStyleOptionButton option;
        option.initFrom(this);
        option.rect = rect();
        return style()->subElementRect(QStyle::SE_CheckBoxIndicator, &option, this);
    }

    void CheckBox::paintEvent(QPaintEvent *event)
    {
        // 指示器底与描边按状态自绘；样式表的 ::indicator 只保留 18px 尺寸与透明边框占位。
        // 画笔先收在作用域里：基类 paintEvent 会在同一个控件上另开一支 QPainter
        {
            QPainter painter{this};
            painter.setRenderHint(QPainter::Antialiasing, true);
            const auto     &palette = activePalette();
            const ThemeKind kind    = activeThemeKind();
            const QRectF    box{indicatorRect()};

            // 状态次序与样式表的覆盖次序一致：禁用压过勾选，勾选压过悬停
            QColor fill = palette.controlFill;
            if (!isEnabled())
            {
                fill = palette.mutedFill;
            } else if (isChecked())
            {
                fill = palette.accent;
            } else if (underMouse())
            {
                fill = palette.hover;
            }

            QColor border;
            int    borderWidth = 0;
            if (kind == ThemeKind::HighContrast)
            {
                // 这一档不装焦点环覆盖层：可用时焦点、勾选与悬停靠 2px 主色边表达；
                // 静息与禁用是 2px borderStrong，禁用的黑边与旧样式表一致
                border      = (isEnabled() && (hasFocus() || isChecked() || underMouse())) ? palette.accent : palette.borderStrong;
                borderWidth = 2;
            }
            else if (!isChecked())
            {
                // 静息也画一圈边：未选中的指示器与分组卡底色接近，没有边就几乎看不见；
                // 悬停换更强的 borderStrong，禁用态留一档更淡的边交代边界
                border      = isEnabled() && underMouse() ? palette.borderStrong : palette.border;
                borderWidth = themeBorderWidth(kind);
            }

            Core::paintContinuousCorner(painter, box, Core::cornerShapeFor(Core::CornerRole::Indicator), fill,
                                        borderWidth > 0 ? QPen{border, static_cast<qreal>(borderWidth)} : QPen{Qt::NoPen});
        }

        QCheckBox::paintEvent(event);
        if (!isChecked())
        {
            return;
        }

        // 内缩给 1.5px 的勾线留出落点，线不压到方框描边上；高对比度档的描边是 2px，内缩跟着加一档
        const int     inset  = activeThemeKind() == ThemeKind::HighContrast ? 2 : 1;
        const QRect   box    = indicatorRect().adjusted(inset, inset, -inset, -inset);
        const qreal   scale  = m_checkScale;
        const QPointF center = QRectF{box}.center();

        QPainter painter{this};
        painter.setRenderHint(QPainter::Antialiasing);
        // 围绕方框中心缩放：缩小档是从中心收进去，而不是往右下角坠
        painter.translate(center);
        painter.scale(scale, scale);
        painter.translate(-center);

        // 折点按方框边长的二十四分格取位：短笔画落 5→9，长笔画收在 19/7
        QPainterPath path;
        path.moveTo(box.left() + box.width() * 5.0 / 24.0, box.top() + box.height() * 13.0 / 24.0);
        path.lineTo(box.left() + box.width() * 9.0 / 24.0, box.top() + box.height() * 17.0 / 24.0);
        path.lineTo(box.left() + box.width() * 19.0 / 24.0, box.top() + box.height() * 7.0 / 24.0);

        painter.setBrush(Qt::NoBrush);
        // 勾选且禁用时指示器底落在 mutedFill（高对比度档是白底），onAccent 会在浅底上看不见；
        // 这一档改用次要文本色，既读得出又与可操作状态拉开
        const auto checkColor = isEnabled() ? activePalette().onAccent : activePalette().secondaryText;
        painter.setPen(QPen{checkColor, 1.5, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin});
        painter.drawPath(path);
    }

    void CheckBox::enterEvent(QEnterEvent *event)
    {
        QCheckBox::enterEvent(event);
        update();
    }

    void CheckBox::leaveEvent(QEvent *event)
    {
        QCheckBox::leaveEvent(event);
        update();
    }
} // namespace GyanisScribe::Gui
