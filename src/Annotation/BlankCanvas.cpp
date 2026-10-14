#include "Annotation/BlankCanvas.h"

#include <QColor>
#include <QImage>

#include <algorithm>

namespace GyanisScribe::Annotation
{

    Core::CapturedImage makeBlankCanvas(const QSize &logicalSize, const qreal devicePixelRatio, const QString &screenName, const bool transparent)
    {
        // 非正的入参退回默认档而不是夹成 1×1：那会让画布看起来「存在」却什么都画不下
        const QSize logical = (logicalSize.width() > 0 && logicalSize.height() > 0) ? logicalSize : kBlankCanvasSize;
        const qreal ratio   = devicePixelRatio > 0.0 ? devicePixelRatio : 1.0;

        Core::CapturedImage canvas;
        canvas.image = QImage{std::max(1L, std::lround(logical.width() * ratio)), std::max(1L, std::lround(logical.height() * ratio)), QImage::Format_ARGB32_Premultiplied};
        // 白底是默认档：成品要能直接存出去、贴出去，谁都不用先想格式。透明底那一档由设置里那一格开，
        // 存 PNG/WebP 留得住，存 JPG 前保存那条路会先垫白，不会把透明区涂成黑
        canvas.image.fill(transparent ? Qt::transparent : Qt::white);
        canvas.image.setDevicePixelRatio(ratio);
        canvas.logicalSize      = logical;
        canvas.devicePixelRatio = ratio;
        canvas.screenName       = screenName;
        // 来源区域留空：这张不是从屏上哪一块抓来的，刷新那条路据此自己拒掉
        canvas.logicalRegion = {};
        return canvas;
    }

    QRect blankCanvasRegion(const QRect &available, const QSize &logicalSize)
    {
        if (available.isEmpty())
        {
            return {};
        }

        const QSize size{std::min(logicalSize.width(), available.width()), std::min(logicalSize.height(), available.height())};
        if (size.width() <= 0 || size.height() <= 0)
        {
            return available;
        }

        // 居中取整留给左上角：右下角那一头多出来的那一像素不影响「看得见整张画布」
        return QRect{available.topLeft() + QPoint{(available.width() - size.width()) / 2, (available.height() - size.height()) / 2}, size};
    }

} // namespace GyanisScribe::Annotation
