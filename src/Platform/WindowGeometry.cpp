#include "Platform/WindowGeometry.h"

#include "Base/Text.h"

#include <cmath>
#include <format>
#include <optional>

namespace
{
    using GyanisScribe::Base::Error;
    using GyanisScribe::Base::ErrorCode;

    /**
     * @brief 缩放因子的唯一一处判据
     * @param devicePixelRatio 这块屏的缩放因子
     * @return 没问题返回空；非正返回中文拒因
     * @details 宁可当场拒绝，也不按 1 猜一个：按 1 猜会把 150% 的屏算成 2/3 大小，
     *          而调用方看到的仍然是一个「算出来了」的矩形，错处没人知道。
     */
    [[nodiscard]] std::optional<Error> scalingProblem(const qreal devicePixelRatio)
    {
        if (devicePixelRatio > 0.0)
        {
            return {};
        }
        return Error{ErrorCode::InvalidArgument,
                     GyanisScribe::Base::uiFormat(
                             GyanisScribe::Base::uiTextUtf8("换算窗口位置要有一个大于 0 的缩放因子，收到的是 {}。请把这块屏自己的读数（QScreen::devicePixelRatio）交进来"),
                             devicePixelRatio)};
    }

    /// 区间按「起点向下、终点向上」取整的一对：两头同向取整会让换算回来的矩形比原物小一格
    [[nodiscard]] int floorEdge(const double value)
    {
        return static_cast<int>(std::floor(value));
    }

    [[nodiscard]] int ceilEdge(const double value)
    {
        return static_cast<int>(std::ceil(value));
    }
} // namespace

namespace GyanisScribe::Platform
{
    Base::Expected<QRect> physicalToLogicalGlobal(const QRect &physicalGlobal, const MonitorMapping &monitor)
    {
        if (const auto problem = scalingProblem(monitor.devicePixelRatio); problem)
        {
            return std::unexpected(*problem);
        }

        // 先移到这块屏自己的原点再除：两块屏的物理原点与逻辑原点各在不同位置，全局直除会把偏移串到别处
        const auto ratio  = monitor.devicePixelRatio;
        const auto left   = floorEdge(static_cast<double>(physicalGlobal.x() - monitor.physicalGlobal.x()) / ratio);
        const auto top    = floorEdge(static_cast<double>(physicalGlobal.y() - monitor.physicalGlobal.y()) / ratio);
        const auto right  = ceilEdge(static_cast<double>(physicalGlobal.x() + physicalGlobal.width() - monitor.physicalGlobal.x()) / ratio);
        const auto bottom = ceilEdge(static_cast<double>(physicalGlobal.y() + physicalGlobal.height() - monitor.physicalGlobal.y()) / ratio);

        return QRect{monitor.logicalGlobal.x() + left, monitor.logicalGlobal.y() + top, right - left, bottom - top};
    }

    Base::Expected<QRect> logicalToPhysicalGlobal(const QRect &logicalGlobal, const MonitorMapping &monitor)
    {
        if (const auto problem = scalingProblem(monitor.devicePixelRatio); problem)
        {
            return std::unexpected(*problem);
        }

        // 与 physicalToLogicalGlobal 同一条相对原点的走法，只是乘回去：两条必须成对改，只改一边就是静默错位
        const auto ratio  = monitor.devicePixelRatio;
        const auto left   = floorEdge(static_cast<double>(logicalGlobal.x() - monitor.logicalGlobal.x()) * ratio);
        const auto top    = floorEdge(static_cast<double>(logicalGlobal.y() - monitor.logicalGlobal.y()) * ratio);
        const auto right  = ceilEdge(static_cast<double>(logicalGlobal.x() + logicalGlobal.width() - monitor.logicalGlobal.x()) * ratio);
        const auto bottom = ceilEdge(static_cast<double>(logicalGlobal.y() + logicalGlobal.height() - monitor.logicalGlobal.y()) * ratio);

        return QRect{monitor.physicalGlobal.x() + left, monitor.physicalGlobal.y() + top, right - left, bottom - top};
    }

    std::size_t monitorIndexForPoint(const QPoint &physicalPoint, const std::vector<MonitorMapping> &monitors)
    {
        // 接缝上的点按声明顺序取第一块命中的：与 Capture::screenAtGlobalPoint() 同一条规则，
        // 两处不一致会让同一个窗口两次算出不同的位置
        for (std::size_t index = 0; index < monitors.size(); ++index)
        {
            if (monitors[index].physicalGlobal.contains(physicalPoint))
            {
                return index;
            }
        }
        return monitors.size();
    }

    Base::Expected<QPoint> logicalPointToPhysicalGlobal(const QPoint &logicalPoint, const MonitorMapping &monitor)
    {
        if (const auto problem = scalingProblem(monitor.devicePixelRatio); problem)
        {
            return std::unexpected(*problem);
        }

        // 与 logicalToPhysicalGlobal 的起点那一条同解：先减这块屏自己的逻辑原点再乘缩放因子，最后取整向下
        const auto ratio = monitor.devicePixelRatio;
        const auto x     = floorEdge(static_cast<double>(logicalPoint.x() - monitor.logicalGlobal.x()) * ratio);
        const auto y     = floorEdge(static_cast<double>(logicalPoint.y() - monitor.logicalGlobal.y()) * ratio);
        return QPoint{monitor.physicalGlobal.x() + x, monitor.physicalGlobal.y() + y};
    }

} // namespace GyanisScribe::Platform
