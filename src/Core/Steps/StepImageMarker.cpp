#include "Core/Steps/StepImageMarker.h"

#include <QColor>
#include <QFont>
#include <QFontMetricsF>
#include <QGuiApplication>
#include <QPainter>
#include <QPen>
#include <QPointF>

#include <algorithm>
#include <cmath>

namespace GyanisScribe::Core
{
    namespace
    {
        /// 圆环半径、线宽与序号字号，都是逻辑像素；落图前乘 dpr
        constexpr qreal kClickRingRadius    = 14.0;
        constexpr qreal kClickRingPenWidth  = 3.0;
        constexpr qreal kStepNumberFontSize = 18.0;

        /// 序号圆牌相对圆心的偏移，按逻辑像素计
        constexpr qreal kStepNumberOffset = 20.0;

        /// 序号圆牌白色描边的线宽，按逻辑像素计；夹进画面时要把它算进占位
        constexpr qreal kStepBadgeOutlineWidth = 2.0;

        /// 把圆牌中心限制在 [low, high]；画面小到装不下圆牌时贴在 high 一侧，不留负数
        [[nodiscard]] qreal clampBadgeCoordinate(const qreal value, const qreal margin, const qreal extent)
        {
            const qreal high = std::max(0.0, extent - 1.0 - margin);
            const qreal low  = std::min(margin, high);
            return std::clamp(value, low, high);
        }
    } // namespace

    QImage annotateStepClick(const QImage &screen, const QPoint &logicalPointInScreen, const qreal devicePixelRatio, const int stepNumber)
    {
        QImage marked = screen.copy();
        if (marked.isNull())
        {
            return marked;
        }

        const qreal   ratio  = devicePixelRatio > 0.0 ? devicePixelRatio : 1.0;
        const QPointF center = QPointF{logicalPointInScreen} * ratio;
        // 右下边界是开区间：宽 100 的图只有 0 到 99 这一列像素，点落在 100 上算画面外
        if (center.x() < 0.0 || center.y() < 0.0 || center.x() >= marked.width() || center.y() >= marked.height())
        {
            return marked;
        }

        QPainter painter{&marked};
        painter.setRenderHint(QPainter::Antialiasing, true);
        painter.setBrush(Qt::NoBrush);
        painter.setPen(QPen{QColor{0xE5, 0x48, 0x4D}, kClickRingPenWidth * ratio});
        painter.drawEllipse(center, kClickRingRadius * ratio, kClickRingRadius * ratio);

        // 字体后端没有应用对象会一直等初始化：只画圆环，不让调用的线程挂在这里
        if (QGuiApplication::instance() != nullptr)
        {
            QFont font;
            font.setBold(true);
            font.setPixelSize(std::max(1, static_cast<int>(std::lround(kStepNumberFontSize * ratio))));
            painter.setFont(font);

            const QFontMetricsF metrics{font};
            const QString       numberText  = QString::number(stepNumber);
            const qreal         badgeRadius = std::max(metrics.height(), metrics.horizontalAdvance(numberText)) / 2.0 + 4.0 * ratio;
            // 点击贴着上边或右边时，圆牌会有一半落到图外；整块夹回画面，序号才始终看得见
            const qreal   margin = badgeRadius + kStepBadgeOutlineWidth * ratio;
            const QPointF badgeCenter{clampBadgeCoordinate(center.x() + kStepNumberOffset * ratio, margin, marked.width()),
                                      clampBadgeCoordinate(center.y() - kStepNumberOffset * ratio, margin, marked.height())};

            painter.setPen(QPen{QColor{0xFF, 0xFF, 0xFF}, kStepBadgeOutlineWidth * ratio});
            painter.setBrush(QColor{0x1F, 0x23, 0x28});
            painter.drawEllipse(badgeCenter, badgeRadius, badgeRadius);

            painter.setPen(Qt::white);
            const qreal textX    = badgeCenter.x() - metrics.horizontalAdvance(numberText) / 2.0;
            const qreal baseline = badgeCenter.y() + (metrics.ascent() - metrics.descent()) / 2.0;
            painter.drawText(QPointF{textX, baseline}, numberText);
        }

        painter.end();
        return marked;
    }

    QImage annotatedStepImage(const RecordedStep &step, const int displayNumber)
    {
        if (step.imagePath.isEmpty())
        {
            return {};
        }

        QImage image;
        if (!image.load(step.imagePath))
        {
            return {};
        }
        // 窗口切换与手动补步没有真实点击点，原样返回；点击类按导出时的最终序号现画
        if (step.kind != StepKind::Click || step.clickPointInImage.x() < 0 || step.clickPointInImage.y() < 0)
        {
            return image;
        }
        return annotateStepClick(image, step.clickPointInImage, step.imageDevicePixelRatio, displayNumber);
    }

} // namespace GyanisScribe::Core
