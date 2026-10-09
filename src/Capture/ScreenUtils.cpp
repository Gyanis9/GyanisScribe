#include "Capture/ScreenUtils.h"

#include <QApplication>
#include <QGuiApplication>
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
            return std::unexpected(Core::Error{Core::ErrorCode::InvalidArgument,
                                               std::format("缩放因子必须大于 0，收到的是 {}。请检查这一帧来自哪块屏幕，dpr 不该由调用方自己猜", devicePixelRatio)});
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
            return std::unexpected(
                    Core::Error{Core::ErrorCode::ScreenUnavailable, std::format("选区超出了这一帧：帧是 {}×{}，换算后要到 {}。请确认这块屏幕有没有被拔掉，或缩放因子有没有变",
                                                                                frameRect.width(), frameRect.height(), physical->width())});
        }

        Core::CapturedImage cropped;
        cropped.image = source.image.copy(*physical);
        // 裁出来的那一块仍按同一缩放因子显示：像素尺寸与逻辑尺寸各算一次，两者必须互指同一个点
        cropped.image.setDevicePixelRatio(source.devicePixelRatio);
        cropped.devicePixelRatio = source.devicePixelRatio;
        cropped.logicalSize      = logicalRegion.size();
        cropped.screenName       = source.screenName;
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

    QRect virtualGeometryOfAllScreens()
    {
        QRect      bounds;
        const auto screens = QGuiApplication::screens();
        for (auto *screen: screens)
        {
            // 第一块屏要用 unite 建立基线：QRect() 的 unite 会给出另一块，这里靠 isEmpty 显式起头更稳
            bounds = bounds.isNull() ? screen->geometry() : bounds.united(screen->geometry());
        }
        return bounds;
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
                                std::format("上次选区的记录格式不对：收到「{}」，应为「x,y,width,height」四个整数。请删掉这一项，让程序重新记一次", std::string(text))});
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
                return std::unexpected(Core::Error{Core::ErrorCode::InvalidArgument,
                                                   std::format("上次选区的记录第 {} 段「{}」不是整数。请删掉这一项，让程序重新记一次", index + 1, std::string(field))});
            }
        }

        if (numbers[2] <= 0 || numbers[3] <= 0)
        {
            return std::unexpected(
                    Core::Error{Core::ErrorCode::InvalidArgument, std::format("上次选区的宽高必须是正数，收到的是 {}×{}。请删掉这一项，让程序重新记一次", numbers[2], numbers[3])});
        }

        return QRect(numbers[0], numbers[1], numbers[2], numbers[3]);
    }

} // namespace GyanisScribe::Capture
