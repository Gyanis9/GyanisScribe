#include "Record/CursorHighlight.h"

#include "Base/Text.h"
#include "Ui/DesignTokens.h"

#include <QPainter>
#include <QPen>
#include <QRect>
#include <QSize>

#include <cmath>
#include <format>
#include <string>

namespace
{
    using GyanisScribe::Base::Error;
    using GyanisScribe::Base::ErrorCode;
    using GyanisScribe::Base::Expected;

    /**
     * @brief 把物理像素尺寸换成逻辑尺寸
     * @param physical 帧的像素宽高
     * @param devicePixelRatio 缩放因子
     * @return 逻辑宽高
     * @details 用四舍五入而不是截断：200% 缩放下 1919 像素这种奇数读数很常见，
     *          截断会让右边缘那一列在「帧内」与「帧外」之间反复跳。
     */
    QSize toLogicalSize(const QSize &physical, const qreal devicePixelRatio)
    {
        return QSize{static_cast<int>(std::lround(physical.width() / devicePixelRatio)), static_cast<int>(std::lround(physical.height() / devicePixelRatio))};
    }
} // namespace

namespace GyanisScribe::Record
{
    Base::Expected<bool> paintCursorHighlight(QImage &frame, const QPoint &cursorGlobalLogical, const QPoint &frameGlobalOriginLogical, const qreal devicePixelRatio)
    {
        // 缩放因子是这里唯一的换算依据：0 或负数会把整圈折到原点上，画出一个「光标永远在左上角」的假象
        if (devicePixelRatio <= 0.0)
        {
            return std::unexpected(
                    Error{ErrorCode::InvalidArgument,
                          GyanisScribe::Base::uiFormat(GyanisScribe::Base::uiTextUtf8("光标高亮的缩放因子必须是正数，收到的是 {}。请传这一块屏幕的 devicePixelRatio"),
                                                       devicePixelRatio)});
        }
        if (frame.isNull())
        {
            return std::unexpected(
                    Error{ErrorCode::InvalidArgument, GyanisScribe::Base::uiTextUtf8("光标高亮的目标帧是空的：抓帧没成功时不该来画圈。请先确认抓到的帧，再调用这一步")});
        }

        // 全局读数减去帧左上角，得到光标在这一帧里的逻辑位置；两个读数必须来自同一套全局坐标
        const QPoint offsetInFrameLogical = cursorGlobalLogical - frameGlobalOriginLogical;
        if (!QRect{QPoint{}, toLogicalSize(frame.size(), devicePixelRatio)}.contains(offsetInFrameLogical))
        {
            // 光标在这块屏之外（多显示器或选区没罩住它）：这一帧本来就没有它，什么都不画
            return false;
        }

        const QPointF centerInPhysical{static_cast<qreal>(offsetInFrameLogical.x()) * devicePixelRatio, static_cast<qreal>(offsetInFrameLogical.y()) * devicePixelRatio};
        const qreal   radiusInPhysical = GyanisScribe::Ui::kCursorHighlightRadius * devicePixelRatio;

        // 一律按物理像素画：整屏帧不带缩放标记、选区裁出的帧带，画之前先把标记清成 1，画完原样还回去，
        // 否则同一份坐标在两种帧上一个乘一次不乘，圈会偏出一半
        const qreal taggedRatio = frame.devicePixelRatio();
        frame.setDevicePixelRatio(1.0);
        {
            QPainter painter{&frame};
            painter.setRenderHint(QPainter::Antialiasing);
            painter.setBrush(Qt::NoBrush);

            // 衬圈先画、主圈压在它上面：两侧各露一半白边，深色与浅色画面上都读得出圈的存在
            painter.setPen(QPen{GyanisScribe::Ui::kCursorHighlightHaloColor, GyanisScribe::Ui::kCursorHighlightHaloPenWidth * devicePixelRatio});
            painter.drawEllipse(centerInPhysical, radiusInPhysical, radiusInPhysical);
            painter.setPen(QPen{GyanisScribe::Ui::kCursorHighlightColor, GyanisScribe::Ui::kCursorHighlightPenWidth * devicePixelRatio});
            painter.drawEllipse(centerInPhysical, radiusInPhysical, radiusInPhysical);
        }
        frame.setDevicePixelRatio(taggedRatio);
        return true;
    }

} // namespace GyanisScribe::Record
