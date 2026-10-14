#include "Annotation/AnnotationTool.h"

#include "Annotation/ShapeTransform.h"
#include "Annotation/TextFont.h"
#include "Annotation/TextLayout.h"

#include <QFont>

#include <algorithm>
#include <cmath>
#include <QFontMetricsF>
#include <QImage>
#include <QPainter>

namespace GyanisScribe::Annotation
{
    namespace
    {
        // 命中测试的最小容差：1-2 像素的线很细，不给余量就点不中
        constexpr int kMinimumHitTolerance = 8;
    } // namespace

    AnnotationTool::AnnotationTool(ToolKind kind) : m_kind(kind), m_color(255, 0, 0), m_lineWidth(3), m_fontSize(16)
    {
    }

    ToolKind AnnotationTool::kind() const noexcept
    {
        return m_kind;
    }

    bool AnnotationTool::isActive() const noexcept
    {
        return m_isActive;
    }

    void AnnotationTool::setActive(bool active) noexcept
    {
        // 半途的拖拽在切走时丢掉：留下一条谁都没看见的图形，撤销栈里就会出现「没画过却能撤」的一条
        if (!active)
        {
            cancel();
        }
        m_isActive = active;
    }

    void AnnotationTool::begin(const QPoint &point)
    {
        m_startPoint                   = point;
        m_pending                      = AnnotationShape{};
        m_pending.color                = m_color;
        m_pending.lineWidth            = m_lineWidth;
        m_pending.fillStyle            = m_fillStyle;
        m_pending.lineStyle            = m_lineStyle;
        m_pending.fontSize             = m_fontSize;
        m_pending.roundedRect          = m_roundedRect;
        m_pending.doubleHeaded         = m_doubleHeaded;
        m_pending.arrowReversed        = m_arrowReversed;
        m_pending.elliptical           = m_elliptical;
        m_pending.mosaicBlockSize      = m_mosaicBlockSize;
        m_pending.highlightAlpha       = m_highlightAlpha;
        m_pending.highlightWidthFactor = m_highlightWidthFactor;
        m_pending.arrowHeadStyle       = m_arrowHeadStyle;
        m_pending.arrowHeadScale       = m_arrowHeadScale;
        m_pending.fontFamily           = m_fontFamily;
        m_pending.textBold             = m_textBold;
        m_pending.textItalic           = m_textItalic;
        m_pending.textOutline          = m_textOutline;
        m_pending.textBackground       = m_textBackground;
        m_pending.textStrokeColorStep  = m_textStrokeColorStep;
        m_pending.textOutlineWidthStep = m_textOutlineWidthStep;
        m_pending.textPlateColorStep   = m_textPlateColorStep;
        m_pending.position             = point;
        m_pending.rect                 = QRect(point, point);
        m_pending.points               = {point, point};
    }

    void AnnotationTool::update(const QPoint &point)
    {
        m_pending.rect   = QRect(m_startPoint, point).normalized();
        m_pending.points = {m_startPoint, point};
    }

    std::optional<AnnotationShape> AnnotationTool::end()
    {
        if (!shapeIsUsable(m_pending))
        {
            cancel();
            return std::nullopt;
        }

        // 交出去之后再清在途：引擎要把这条图形放进撤销栈
        const AnnotationShape committed = m_pending;
        cancel();
        return committed;
    }

    void AnnotationTool::cancel() noexcept
    {
        m_pending = AnnotationShape{};
    }

    int AnnotationTool::shapeCount() const noexcept
    {
        return static_cast<int>(m_shapes.size());
    }

    AnnotationShape AnnotationTool::shapeAt(int index) const
    {
        if (index < 0 || index >= m_shapes.size())
        {
            return {};
        }
        return m_shapes[index];
    }

    void AnnotationTool::insertShape(int index, const AnnotationShape &shape)
    {
        // 索引钳到末尾：撤销「删除」时传的正是取走时那个位置，越界只会来自调用方算错
        const int clamped = index < 0 ? 0 : (index > m_shapes.size() ? m_shapes.size() : index);
        m_shapes.insert(clamped, shape);
    }

    AnnotationShape AnnotationTool::takeShapeAt(int index)
    {
        if (index < 0 || index >= m_shapes.size())
        {
            return {};
        }
        const AnnotationShape taken = m_shapes[index];
        m_shapes.remove(index);
        return taken;
    }

    bool AnnotationTool::translateShapeBy(int index, const QPoint &delta)
    {
        if (index < 0 || index >= m_shapes.size())
        {
            return false;
        }

        translateShape(m_shapes[index], delta);
        return true;
    }

    bool AnnotationTool::restyleShapeAt(const int index, const PenParameters &parameters)
    {
        if (index < 0 || index >= m_shapes.size())
        {
            return false;
        }

        // 只碰这些样式字段：位置、轨迹点与文本各自属于移动与内容命令，混进来会让撤销分不开
        auto &shape                = m_shapes[index];
        shape.color                = parameters.color;
        shape.lineWidth            = parameters.lineWidth;
        shape.fontSize             = parameters.fontSize;
        shape.fillStyle            = parameters.fillStyle;
        shape.roundedRect          = parameters.roundedRect;
        shape.lineStyle            = parameters.lineStyle;
        shape.doubleHeaded         = parameters.doubleHeaded;
        shape.arrowReversed        = parameters.arrowReversed;
        shape.elliptical           = parameters.elliptical;
        shape.mosaicBlockSize      = parameters.mosaicBlockSize;
        shape.highlightAlpha       = parameters.highlightAlpha;
        shape.highlightWidthFactor = parameters.highlightWidthFactor;
        shape.arrowHeadStyle       = parameters.arrowHeadStyle;
        shape.arrowHeadScale       = parameters.arrowHeadScale;
        shape.fontFamily           = parameters.fontFamily;
        shape.textBold             = parameters.textBold;
        shape.textItalic           = parameters.textItalic;
        shape.textOutline          = parameters.textOutline;
        shape.textBackground       = parameters.textBackground;
        shape.textStrokeColorStep  = parameters.textStrokeColorStep;
        shape.textOutlineWidthStep = parameters.textOutlineWidthStep;
        shape.textPlateColorStep   = parameters.textPlateColorStep;
        return true;
    }

    bool AnnotationTool::setTextAt(const int index, const QString &text)
    {
        if (index < 0 || index >= m_shapes.size())
        {
            return false;
        }

        // 只碰文本：位置与字号属于移动与改样式那两条命令，混进来撤销时就分不开这两步
        m_shapes[index].text = text;
        return true;
    }

    bool AnnotationTool::replaceShapeAt(const int index, const AnnotationShape &shape)
    {
        if (index < 0 || index >= m_shapes.size())
        {
            return false;
        }
        m_shapes[index] = shape;
        return true;
    }

    int AnnotationTool::hitTest(const QPoint &point) const
    {
        // 倒序遍历：工具内部后画的图形压在上面，用户点的就是看得见的那一条；
        // 跨工具的层叠由引擎按全局层叠号排序后再问，这里只管本工具这一摞
        for (int index = static_cast<int>(m_shapes.size()) - 1; index >= 0; --index)
        {
            if (hitTestShapeAt(index, point))
            {
                return index;
            }
        }
        return -1;
    }

    bool AnnotationTool::hitTestShapeAt(const int index, const QPoint &point) const
    {
        if (index < 0 || index >= m_shapes.size())
        {
            return false;
        }

        const auto &shape     = m_shapes[index];
        const int   tolerance = shape.lineWidth > kMinimumHitTolerance ? shape.lineWidth : kMinimumHitTolerance;
        // 转过的图形：事件点先绕中心反向转回来，再走与未旋转时完全相同的那条判据
        if (shape.rotationDeg != 0.0)
        {
            const QPointF center = QRectF{localBoundingBoxOf(shape)}.center();
            const QPointF local  = unrotatedPoint(QPointF{point}, center, shape.rotationDeg);
            return hitTestShape(shape, QPoint{static_cast<int>(std::lround(local.x())), static_cast<int>(std::lround(local.y()))}, tolerance);
        }
        return hitTestShape(shape, point, tolerance);
    }

    bool AnnotationTool::hasPending() const noexcept
    {
        return !m_pending.rect.isNull() || !m_pending.text.isEmpty() || !m_pending.points.isEmpty();
    }

    bool AnnotationTool::hitTestShape(const AnnotationShape &shape, const QPoint &point, const int tolerance) const
    {
        const auto box = localBoundingBoxOf(shape).adjusted(-tolerance, -tolerance, tolerance, tolerance);
        return box.contains(point);
    }

    bool AnnotationTool::shapeIsUsable(const AnnotationShape &shape) const
    {
        return shape.rect.width() > 1 && shape.rect.height() > 1;
    }

    QRect AnnotationTool::localBoundingBoxOf(const AnnotationShape &shape) const
    {
        QRect box;

        if (!shape.rect.isNull())
        {
            box = box.united(shape.rect);
        }
        for (const auto &point: shape.points)
        {
            box = box.united(QRect(point, QSize(1, 1)));
        }

        if (!shape.text.isEmpty())
        {
            // 文字的外接框按同一份字形量出来，锚点与画字同解（口径见 TextLayout.h 的 textBoxOf）：
            // 首行基线落在 position 上（drawText 的口径），框顶要从基线往上退一个 ascent，
            // 多行内容按行数长高。按左上角估会让选中框整体压在字底下约一行（渲染探针核对过）。
            // 粗细、斜体、字体族都会动字宽，所以量这里必须与画那边共用 textFontFor
            const QFontMetricsF metrics(textFontFor(shape));
            const QRectF        textBox = textBoxOf(shape, metrics);
            box = box.united(QRect(static_cast<int>(textBox.left()), static_cast<int>(textBox.top()), static_cast<int>(textBox.width()), static_cast<int>(textBox.height())));
        }

        return box;
    }

    QRect AnnotationTool::boundingBoxOf(const AnnotationShape &shape) const
    {
        const QRect local = localBoundingBoxOf(shape);
        if (shape.rotationDeg == 0.0)
        {
            return local;
        }
        return rotatedBoxBounds(QRectF{local}, shape.rotationDeg).toAlignedRect();
    }

    std::array<QPointF, 4> AnnotationTool::frameCornersOf(const AnnotationShape &shape) const
    {
        const QRectF  local{localBoundingBoxOf(shape)};
        const QPointF center = local.center();
        if (shape.rotationDeg == 0.0)
        {
            return {local.topLeft(), local.topRight(), local.bottomRight(), local.bottomLeft()};
        }
        return {rotatedPoint(local.topLeft(), center, shape.rotationDeg), rotatedPoint(local.topRight(), center, shape.rotationDeg),
                rotatedPoint(local.bottomRight(), center, shape.rotationDeg), rotatedPoint(local.bottomLeft(), center, shape.rotationDeg)};
    }

    void AnnotationTool::paintShapeAt(const int index, QPainter &painter, const PaintContext &context) const
    {
        if (index < 0 || index >= m_shapes.size())
        {
            return;
        }

        const auto &shape = m_shapes[index];
        if (shape.rotationDeg == 0.0)
        {
            paintShape(painter, shape, context);
            return;
        }

        // 转过的话把画家绕外接框中心摆正：paintShape 照旧按未旋转的坐标画，屏显与导出同一处生效
        const QPointF center = QRectF{localBoundingBoxOf(shape)}.center();
        painter.save();
        painter.translate(center);
        painter.rotate(shape.rotationDeg);
        painter.translate(-center);
        paintShape(painter, shape, context);
        painter.restore();
    }

    void AnnotationTool::paintPending(QPainter &painter, const PaintContext &context) const
    {
        // 在途图形也画：用户要立刻看到自己正在框的东西，但它不进最终导出
        if (isActive() && hasPending())
        {
            paintShape(painter, m_pending, context);
        }
    }

    void AnnotationTool::setPenColor(const QColor &color) noexcept
    {
        m_color = color;
    }

    void AnnotationTool::setLineWidth(int width) noexcept
    {
        m_lineWidth = width;
    }

    void AnnotationTool::setFontSize(int size) noexcept
    {
        m_fontSize = size;
    }


    int AnnotationTool::lineWidth() const noexcept
    {
        return m_lineWidth;
    }

    void AnnotationTool::setFillStyle(const int style) noexcept
    {
        m_fillStyle = std::clamp(style, 0, 2);
    }

    int AnnotationTool::fillStyle() const noexcept
    {
        return m_fillStyle;
    }

    void AnnotationTool::setRoundedRect(const bool enabled) noexcept
    {
        m_roundedRect = enabled;
    }

    void AnnotationTool::setDoubleHeaded(const bool enabled) noexcept
    {
        m_doubleHeaded = enabled;
    }

    void AnnotationTool::setArrowReversed(const bool enabled) noexcept
    {
        m_arrowReversed = enabled;
    }

    void AnnotationTool::setElliptical(const bool enabled) noexcept
    {
        m_elliptical = enabled;
    }

    void AnnotationTool::setTextStrokeColorStep(const int step) noexcept
    {
        m_textStrokeColorStep = std::clamp(step, 0, kTextStrokeColorCount - 1);
    }

    void AnnotationTool::setTextOutlineWidthStep(const int step) noexcept
    {
        m_textOutlineWidthStep = std::clamp(step, 0, static_cast<int>(kTextOutlineWidths.size()) - 1);
    }

    void AnnotationTool::setTextPlateColorStep(const int step) noexcept
    {
        m_textPlateColorStep = std::clamp(step, 0, kTextPlateColorCount - 1);
    }

    void AnnotationTool::setMosaicBlockSize(const int blockSize) noexcept
    {
        // 0 会让分块除法除零，1 与不马赛克没差别；64 以上在大图上只剩几块色斑
        m_mosaicBlockSize = std::clamp(blockSize, 1, 64);
    }

    void AnnotationTool::setHighlightAlpha(const int alpha) noexcept
    {
        m_highlightAlpha = std::clamp(alpha, 0, 255);
    }

    void AnnotationTool::setLineStyle(const int style) noexcept
    {
        // 线型是三档里的一档：越界按最近的一档夹取，留一个界外的值只会画不出东西
        m_lineStyle = std::clamp(style, 0, static_cast<int>(kAnnotationLineStyles.size()) - 1);
    }

    void AnnotationTool::setHighlightWidthFactor(const int factor) noexcept
    {
        // 倍数取正：0 或负数画出来是一条不存在的带，命中测试也会跟着失效
        m_highlightWidthFactor = std::clamp(factor, 1, 16);
    }

    void AnnotationTool::setArrowHeadStyle(const int style) noexcept
    {
        // 越界按最近的一档夹取：样式是三档里的一档，留一个界外的值只会画不出东西
        m_arrowHeadStyle = std::clamp(style, kArrowHeadFilled, kArrowHeadStyleCount - 1);
    }

    void AnnotationTool::setArrowHeadScale(const qreal scale) noexcept
    {
        // 倍数取正且给个上限：0 画不出头，太大在短箭头上会被半程夹掉、只剩一个糊块
        m_arrowHeadScale = std::clamp(scale, 1.0, 10.0);
    }

    void AnnotationTool::setFontFamily(const QString &family)
    {
        m_fontFamily = family;
    }

    void AnnotationTool::setTextBold(const bool enabled) noexcept
    {
        m_textBold = enabled;
    }

    void AnnotationTool::setTextItalic(const bool enabled) noexcept
    {
        m_textItalic = enabled;
    }

    void AnnotationTool::setTextOutline(const bool enabled) noexcept
    {
        m_textOutline = enabled;
    }

    void AnnotationTool::setTextBackground(const bool enabled) noexcept
    {
        m_textBackground = enabled;
    }

    int AnnotationTool::fontSize() const noexcept
    {
        return m_fontSize;
    }

} // namespace GyanisScribe::Annotation
