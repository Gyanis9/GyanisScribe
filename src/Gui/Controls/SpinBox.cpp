#include "Gui/Controls/SpinBox.h"

#include "Base/Text.h"

#include <QAbstractButton>
#include <QEnterEvent>
#include <QLineEdit>
#include <QPainter>
#include <QPainterPath>
#include <QResizeEvent>

namespace
{
    /// 自绘 chevron 的按钮：透明底、悬停一档浅底，不进 Tab 链
    class StepperButton final : public QAbstractButton
    {
    public:
        StepperButton(const bool pointsUp, QWidget *parent) : QAbstractButton(parent), m_pointsUp(pointsUp)
        {
            setCursor(Qt::ArrowCursor);
            setFocusPolicy(Qt::NoFocus);
            setAttribute(Qt::WA_Hover);
        }

    protected:
        void enterEvent(QEnterEvent *) override
        {
            update();
        }
        void leaveEvent(QEvent *) override
        {
            update();
        }

        void paintEvent(QPaintEvent *) override
        {
            QPainter painter{this};
            painter.setRenderHint(QPainter::Antialiasing);
            if (underMouse() && isEnabled())
            {
                painter.setPen(Qt::NoPen);
                painter.setBrush(palette().color(QPalette::AlternateBase));
                painter.drawRoundedRect(rect().adjusted(1, 1, -1, -1), 6, 6);
            }

            const auto    color       = isEnabled() ? palette().color(QPalette::Text) : palette().color(QPalette::Disabled, QPalette::Text);
            const auto    center      = rect().center();
            constexpr int kHalfWidth  = 3;
            constexpr int kHalfHeight = 2;

            QPainterPath path;
            if (m_pointsUp)
            {
                path.moveTo(center.x() - kHalfWidth, center.y() + kHalfHeight);
                path.lineTo(center.x(), center.y() - kHalfHeight);
                path.lineTo(center.x() + kHalfWidth, center.y() + kHalfHeight);
            } else
            {
                path.moveTo(center.x() - kHalfWidth, center.y() - kHalfHeight);
                path.lineTo(center.x(), center.y() + kHalfHeight);
                path.lineTo(center.x() + kHalfWidth, center.y() - kHalfHeight);
            }
            painter.setPen(QPen{color, 1.5, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin});
            painter.setBrush(Qt::NoBrush);
            painter.drawPath(path);
        }

    private:
        bool m_pointsUp = false; ///< true 画向上 chevron
    };
} // namespace

namespace GyanisScribe::Gui
{
    SpinBox::SpinBox(QWidget *parent) : QSpinBox(parent)
    {
        setObjectName(QStringLiteral("numericSpin"));
        setButtonSymbols(QAbstractSpinBox::NoButtons);
        // 文本让出右缘那一条：自绘按钮压在上面时数字不会钻到按钮底下
        if (auto *edit = lineEdit(); edit != nullptr)
        {
            edit->setTextMargins(0, 0, kStepperWidth + 4, 0);
        }

        m_up   = new StepperButton(true, this);
        m_down = new StepperButton(false, this);
        m_up->setObjectName(QStringLiteral("spinUp"));
        m_down->setObjectName(QStringLiteral("spinDown"));
        m_up->setAccessibleName(GyanisScribe::Base::uiText("增加"));
        m_down->setAccessibleName(GyanisScribe::Base::uiText("减少"));
        QObject::connect(m_up, &QAbstractButton::clicked, this, [this] { stepUp(); });
        QObject::connect(m_down, &QAbstractButton::clicked, this, [this] { stepDown(); });
    }

    void SpinBox::resizeEvent(QResizeEvent *event)
    {
        QSpinBox::resizeEvent(event);
        if (m_up == nullptr || m_down == nullptr)
        {
            return;
        }
        const auto strip = QRect{width() - kStepperWidth - 2, 2, kStepperWidth, height() - 4};
        if (strip.height() < 8)
        {
            return;
        }
        const auto half = strip.height() / 2;
        m_up->setGeometry(strip.left(), strip.top(), strip.width(), half);
        m_down->setGeometry(strip.left(), strip.top() + half, strip.width(), strip.height() - half);
    }

} // namespace GyanisScribe::Gui
