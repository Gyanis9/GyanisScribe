#include "Capture/ScreenUtils.h"

#include "Core/Text.h"

#include <QApplication>
#include <QGuiApplication>
#include <QPainter>
#include <QScreen>

#include <cmath>
#include <format>
#include <charconv>
#include <vector>

namespace GyanisScribe::Capture
{

    Core::Expected<QRect> toPhysicalRect(const QRect &logicalRect, qreal devicePixelRatio)
    {
        if (devicePixelRatio <= 0.0)
        {
            return std::unexpected(
                    Core::Error{Core::ErrorCode::InvalidArgument,
                                GyanisScribe::Core::uiFormat(GyanisScribe::Core::uiTextUtf8("缩放因子必须大于 0，收到的是 {}。请检查这一帧来自哪块屏幕，dpr 不该由调用方自己猜"),
                                                             devicePixelRatio)});
        }

        // 逻辑矩形按「[起点, 起点+长度)」的连续区间处理：起点向下取整、终点向上取整，
        // 半像素的余量一律留给选区，用户框到的边不会被切掉
        const auto left            = static_cast<int>(std::floor(logicalRect.x() * devicePixelRatio));
        const auto top             = static_cast<int>(std::floor(logicalRect.y() * devicePixelRatio));
        const auto rightExclusive  = static_cast<int>(std::ceil((logicalRect.x() + logicalRect.width()) * devicePixelRatio));
        const auto bottomExclusive = static_cast<int>(std::ceil((logicalRect.y() + logicalRect.height()) * devicePixelRatio));

        return QRect(left, top, rightExclusive - left, bottomExclusive - top);
    }

    Core::Expected<Core::CapturedImage> cropToLogicalRect(const Core::CapturedImage &source, const QRect &logicalRegion)
    {
        const auto physical = toPhysicalRect(logicalRegion, source.devicePixelRatio);
        if (!physical)
        {
            return std::unexpected(physical.error());
        }

        const QRect frameRect(0, 0, source.image.width(), source.image.height());
        if (!frameRect.contains(*physical))
        {
            return std::unexpected(Core::Error{
                    Core::ErrorCode::ScreenUnavailable,
                    GyanisScribe::Core::uiFormat(GyanisScribe::Core::uiTextUtf8("选区超出了这一帧：帧是 {}×{}，换算后要到 {}。请确认这块屏幕有没有被拔掉，或缩放因子有没有变"),
                                                 frameRect.width(), frameRect.height(), physical->width())});
        }

        Core::CapturedImage cropped;
        cropped.image = source.image.copy(*physical);
        // 裁出来的那一块仍按同一缩放因子显示：像素尺寸与逻辑尺寸各算一次，两者必须互指同一个点
        cropped.image.setDevicePixelRatio(source.devicePixelRatio);
        cropped.devicePixelRatio = source.devicePixelRatio;
        cropped.logicalSize      = logicalRegion.size();
        cropped.screenName       = source.screenName;
        cropped.logicalRegion    = logicalRegion;
        return cropped;
    }

    QScreen *screenByName(const QString &screenName)
    {
        for (auto *screen: QGuiApplication::screens())
        {
            if (screen->name() == screenName)
            {
                return screen;
            }
        }
        return nullptr;
    }

    Core::CapturedImage frameFromGrabbedImage(const QImage &grabbed, const QScreen *screen, const QSize &logicalSize)
    {
        Core::CapturedImage frame;
        frame.image = grabbed.convertToFormat(QImage::Format_ARGB32_Premultiplied);
        // 抓回来的图按物理像素交回：Qt 有时会把 dpr 标在图上，这里统一清成 1，
        // 免得下游既乘一次又除一次
        frame.image.setDevicePixelRatio(1.0);
        frame.devicePixelRatio = screen != nullptr ? screen->devicePixelRatio() : 1.0;
        frame.logicalSize      = logicalSize;
        frame.screenName       = screen != nullptr ? screen->name() : QString();
        return frame;
    }

    QScreen *screenAtGlobalPoint(const QPoint &globalPoint)
    {
        // 接缝上的点按屏幕声明顺序取第一块命中的：两屏共享一条边时必须有唯一答案，
        // 否则同一个坐标点两次抓图会落到不同的那块屏上
        const auto screens = QGuiApplication::screens();
        for (auto *screen: screens)
        {
            if (screen->geometry().contains(globalPoint))
            {
                return screen;
            }
        }
        return nullptr;
    }


    std::optional<QRect> regionAtPoint(const QPoint &point, const std::vector<QRect> &candidates)
    {
        // 先到先得：候选那份顺序就是压盖顺序，后面的已经被前面的挡住了
        for (const auto &candidate: candidates)
        {
            if (candidate.contains(point))
            {
                return candidate;
            }
        }
        return {};
    }

    QString formatSelectionHint(const QRect &logicalRegion)
    {
        return QString::fromUtf8(std::format("{} × {}    ({}, {})", logicalRegion.width(), logicalRegion.height(), logicalRegion.x(), logicalRegion.y()).c_str());
    }

    QString regionToText(const QRect &logicalRegion)
    {
        return QString::fromUtf8(std::format("{},{},{},{}", logicalRegion.x(), logicalRegion.y(), logicalRegion.width(), logicalRegion.height()).c_str());
    }

    Core::Expected<QRect> regionFromText(std::string_view text)
    {
        std::vector<std::string_view> fields;
        std::size_t                   position = 0;
        while (true)
        {
            const auto next = text.find(',', position);
            fields.push_back(text.substr(position, next == std::string_view::npos ? std::string_view::npos : next - position));
            if (next == std::string_view::npos)
            {
                break;
            }
            position = next + 1;
        }

        if (fields.size() != 4)
        {
            return std::unexpected(
                    Core::Error{Core::ErrorCode::InvalidArgument,
                                GyanisScribe::Core::uiFormat(
                                        GyanisScribe::Core::uiTextUtf8("上次选区的记录格式不对：收到「{}」，应为「x,y,width,height」四个整数。请删掉这一项，让程序重新记一次"),
                                        std::string(text))});
        }

        int numbers[4] = {0, 0, 0, 0};
        for (std::size_t index = 0; index < fields.size(); ++index)
        {
            const auto field     = fields[index];
            const auto begin     = field.find_first_not_of(" \t");
            const auto end       = field.find_last_not_of(" \t");
            const auto trimmed   = begin == std::string_view::npos ? std::string_view{} : field.substr(begin, end - begin + 1);
            const auto converted = std::from_chars(trimmed.data(), trimmed.data() + trimmed.size(), numbers[index]);
            if (trimmed.empty() || converted.ec != std::errc{} || converted.ptr != trimmed.data() + trimmed.size())
            {
                // 逐段点名是哪一段坏了：四个数字里任何一个被截断或取整都会把选区挪到别处
                return std::unexpected(
                        Core::Error{Core::ErrorCode::InvalidArgument,
                                    GyanisScribe::Core::uiFormat(GyanisScribe::Core::uiTextUtf8("上次选区的记录第 {} 段「{}」不是整数。请删掉这一项，让程序重新记一次"), index + 1,
                                                                 std::string(field))});
            }
        }

        if (numbers[2] <= 0 || numbers[3] <= 0)
        {
            return std::unexpected(
                    Core::Error{Core::ErrorCode::InvalidArgument,
                                GyanisScribe::Core::uiFormat(GyanisScribe::Core::uiTextUtf8("上次选区的宽高必须是正数，收到的是 {}×{}。请删掉这一项，让程序重新记一次"), numbers[2],
                                                             numbers[3])});
        }

        return QRect(numbers[0], numbers[1], numbers[2], numbers[3]);
    }

    QPoint squareLockedPoint(const QPoint &anchor, const QPoint &current, const bool locked, const QRect &limit)
    {
        if (!locked)
        {
            return current;
        }

        const int dx     = current.x() - anchor.x();
        const int dy     = current.y() - anchor.y();
        const int extent = std::max(std::abs(dx), std::abs(dy));
        // 偏移为 0 的方向没有符号：按正方向走，正方形总是往起手点的右/下展开
        const int signX = dx < 0 ? -1 : 1;
        const int signY = dy < 0 ? -1 : 1;

        const int clampedX = std::clamp(anchor.x() + signX * extent, limit.left(), limit.right());
        const int clampedY = std::clamp(anchor.y() + signY * extent, limit.top(), limit.bottom());
        // 夹取可能把等边破坏掉：按两轴里更小的那个偏移量收回来，仍是正方形
        const int size = std::min(std::abs(clampedX - anchor.x()), std::abs(clampedY - anchor.y()));
        return QPoint(anchor.x() + signX * size, anchor.y() + signY * size);
    }

    QRect nudgeRegion(const QRect &region, const QPoint &step, const QRect &limit)
    {
        if (region.isEmpty() || limit.isEmpty())
        {
            return region;
        }

        // 逐轴算允许区间：往正方向最多走到「贴上 limit 的右边」，再往前就整块出界了。
        // 选区本身比这一档还宽时原地不动——夹无可夹，硬夹会把宽高也改了
        const auto clampAxis = [](int current, int delta, int low, int high, int span)
        {
            const auto upper = high - span + 1;
            return upper < low ? current : std::clamp(current + delta, low, upper);
        };

        const auto left = clampAxis(region.left(), step.x(), limit.left(), limit.right(), region.width());
        const auto top  = clampAxis(region.top(), step.y(), limit.top(), limit.bottom(), region.height());
        return QRect{QPoint{left, top}, region.size()};
    }

    QRect resizeRegion(const QRect &region, const QPoint &step, const QRect &limit)
    {
        if (region.isEmpty() || limit.isEmpty())
        {
            return region;
        }

        // 顶边不动，只挪右下角；下限钉在顶边那一格，收到 0 宽就不是「一块图」了
        const auto right  = std::max(region.left(), std::min(region.right() + step.x(), limit.right()));
        const auto bottom = std::max(region.top(), std::min(region.bottom() + step.y(), limit.bottom()));
        return QRect{region.topLeft(), QPoint{right, bottom}};
    }

    SelectionGrip gripAt(const QRect &region, const QPoint &point, const int tolerance)
    {
        if (region.isEmpty() || tolerance < 0)
        {
            return SelectionGrip::None;
        }

        // 切比雪夫距离：鼠标移动本来就是按轴向看的，用欧氏距离会在斜角上把边点判成角点
        const auto near = [tolerance](const QPoint &a, const QPoint &b) { return std::max(std::abs(a.x() - b.x()), std::abs(a.y() - b.y())) <= tolerance; };

        // 四角先认：贴着角的那一点同时也在两条边上，先认边就会把「拉这个角」变成「拉这条边」
        if (near(point, region.topLeft()))
        {
            return SelectionGrip::TopLeft;
        }
        if (near(point, QPoint{region.right(), region.top()}))
        {
            return SelectionGrip::TopRight;
        }
        if (near(point, region.bottomRight()))
        {
            return SelectionGrip::BottomRight;
        }
        if (near(point, QPoint{region.left(), region.bottom()}))
        {
            return SelectionGrip::BottomLeft;
        }

        // 四条边：垂直方向贴到那条线，沿边方向还得落在边的范围内
        const auto withinVerticalExtent   = point.y() >= region.top() && point.y() <= region.bottom();
        const auto withinHorizontalExtent = point.x() >= region.left() && point.x() <= region.right();
        if (withinVerticalExtent && std::abs(point.x() - region.left()) <= tolerance)
        {
            return SelectionGrip::Left;
        }
        if (withinVerticalExtent && std::abs(point.x() - region.right()) <= tolerance)
        {
            return SelectionGrip::Right;
        }
        if (withinHorizontalExtent && std::abs(point.y() - region.top()) <= tolerance)
        {
            return SelectionGrip::Top;
        }
        if (withinHorizontalExtent && std::abs(point.y() - region.bottom()) <= tolerance)
        {
            return SelectionGrip::Bottom;
        }

        return region.contains(point) ? SelectionGrip::Inside : SelectionGrip::None;
    }

    QRect dragRegionByGrip(const QRect &region, const SelectionGrip grip, const QPoint &delta, const QRect &limit)
    {
        if (region.isEmpty() || grip == SelectionGrip::None)
        {
            return region;
        }
        if (grip == SelectionGrip::Inside)
        {
            return nudgeRegion(region, delta, limit);
        }

        const auto pullsLeft   = grip == SelectionGrip::TopLeft || grip == SelectionGrip::Left || grip == SelectionGrip::BottomLeft;
        const auto pullsRight  = grip == SelectionGrip::TopRight || grip == SelectionGrip::Right || grip == SelectionGrip::BottomRight;
        const auto pullsTop    = grip == SelectionGrip::TopLeft || grip == SelectionGrip::Top || grip == SelectionGrip::TopRight;
        const auto pullsBottom = grip == SelectionGrip::BottomLeft || grip == SelectionGrip::Bottom || grip == SelectionGrip::BottomRight;

        auto left   = region.left();
        auto top    = region.top();
        auto right  = region.right();
        auto bottom = region.bottom();

        // 每根被拖的线都夹在「对边」与「这块屏的边」之间：拖过头最多收成一条格，不许把选区翻面
        if (pullsLeft)
        {
            left = std::clamp(left + delta.x(), limit.left(), right);
        }
        if (pullsRight)
        {
            right = std::clamp(right + delta.x(), left, limit.right());
        }
        if (pullsTop)
        {
            top = std::clamp(top + delta.y(), limit.top(), bottom);
        }
        if (pullsBottom)
        {
            bottom = std::clamp(bottom + delta.y(), top, limit.bottom());
        }

        return QRect{QPoint{left, top}, QPoint{right, bottom}};
    }

    Qt::CursorShape cursorForGrip(const SelectionGrip grip)
    {
        auto shape = Qt::CursorShape::CrossCursor;
        switch (grip)
        {
            case SelectionGrip::None:
                shape = Qt::CursorShape::CrossCursor;
                break;
            case SelectionGrip::Inside:
                shape = Qt::CursorShape::SizeAllCursor;
                break;
            case SelectionGrip::TopLeft:
            case SelectionGrip::BottomRight:
                shape = Qt::CursorShape::SizeFDiagCursor;
                break;
            case SelectionGrip::TopRight:
            case SelectionGrip::BottomLeft:
                shape = Qt::CursorShape::SizeBDiagCursor;
                break;
            case SelectionGrip::Left:
            case SelectionGrip::Right:
                shape = Qt::CursorShape::SizeHorCursor;
                break;
            case SelectionGrip::Top:
            case SelectionGrip::Bottom:
                shape = Qt::CursorShape::SizeVerCursor;
                break;
        }
        return shape;
    }

    QRect cursorRectInPixels(const CursorSprite &sprite, qreal devicePixelRatio)
    {
        // 缩放因子只换算位置：形状本身就是系统按这块屏的 DPI 挑出来的物理像素，再乘一次指针会大一圈
        const auto ratio = devicePixelRatio > 0.0 ? devicePixelRatio : 1.0;
        // 尖端位置换算成物理像素后，形状要按热点回退，否则指针整体偏右下
        const auto topLeft = QPoint{static_cast<int>(std::lround(static_cast<double>(sprite.position.x()) * ratio)),
                                    static_cast<int>(std::lround(static_cast<double>(sprite.position.y()) * ratio))} -
                             sprite.hotspot;
        return QRect{topLeft, sprite.image.size()};
    }

    void drawCursorInto(Core::CapturedImage &frame, const CursorSprite &sprite)
    {
        if (sprite.image.isNull() || frame.image.isNull())
        {
            return;
        }

        const auto target = cursorRectInPixels(sprite, frame.devicePixelRatio);
        QPainter   painter{&frame.image};
        // 落点已经是物理像素：来源交回的图都按「image 的缩放因子清成 1」这一格约定（见 IScreenCaptureSource），
        // painter 因此按设备像素画，指针不会被再放大一次
        painter.drawImage(target.topLeft(), sprite.image);
    }

} // namespace GyanisScribe::Capture
