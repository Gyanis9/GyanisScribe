#include "Gui/Controls/CheckBox.h"

#include "Gui/Theme/DesignTokens.h"

#include <QPainter>
#include <QPainterPath>
#include <QStyle>
#include <QStyleOptionButton>

namespace GyanisScribe::Gui
{
    CheckBox::CheckBox(QWidget *parent) : QCheckBox(parent), m_spring(this)
    {
        // 动态属性让 QSS 关掉 ::indicator:checked 的静态勾图，方框的底色与圆角仍走样式表
        setProperty("animatedIndicator", true);
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
        QCheckBox::paintEvent(event);
        if (!isChecked())
        {
            return;
        }

        // 1px 内缩给 1.5px 的勾线留出落点，线不压到方框描边上
        const QRect   box    = indicatorRect().adjusted(1, 1, -1, -1);
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
        painter.setPen(QPen{activePalette().onAccent, 1.5, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin});
        painter.drawPath(path);
    }
} // namespace GyanisScribe::Gui
