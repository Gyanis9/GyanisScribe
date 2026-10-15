#include "Annotation/AnnotationToolbarGeometry.h"

#include "Base/Text.h"

#include <algorithm>
#include <format>

namespace GyanisScribe::Annotation
{
    Base::Expected<ToolbarPlacement> placeToolbar(const QRect &screenAvailable, const QSize &toolbarSize, const QRect &selection)
    {
        // 尺寸与选区先判：这三项为 0 或负数时后面的加减还能跑，但算出来的是一个看不见的工具栏，
        // 那种结果传下去比当场拒掉更难查
        if (toolbarSize.width() <= 0 || toolbarSize.height() <= 0)
        {
            return Base::Expected<ToolbarPlacement>{std::unexpected{Base::Error{
                    .code    = Base::ErrorCode::InvalidArgument,
                    .message = GyanisScribe::Base::uiFormat(GyanisScribe::Base::uiTextUtf8("工具栏尺寸 {}x{} 不成立，摆放前要先有有效的 sizeHint"), toolbarSize.width(), toolbarSize.height()),
            }}};
        }
        if (selection.width() <= 0 || selection.height() <= 0)
        {
            return Base::Expected<ToolbarPlacement>{std::unexpected{Base::Error{
                    .code    = Base::ErrorCode::InvalidArgument,
                    .message = GyanisScribe::Base::uiFormat(GyanisScribe::Base::uiTextUtf8("选区 {}x{} 是空的，没有可参照的位置"), selection.width(), selection.height()),
            }}};
        }
        if (screenAvailable.width() <= 0 || screenAvailable.height() <= 0)
        {
            return Base::Expected<ToolbarPlacement>{std::unexpected{Base::Error{
                    .code = Base::ErrorCode::ScreenUnavailable,
                    .message =
                            GyanisScribe::Base::uiFormat(GyanisScribe::Base::uiTextUtf8("屏幕可用区 {}x{} 放不下任何东西，请确认目标屏幕还在"), screenAvailable.width(), screenAvailable.height()),
            }}};
        }

        // 宽度按可用区夹住：比屏幕还宽的工具栏会被裁，界面层拿到这个宽度自己收缩布局
        const auto width  = std::min(toolbarSize.width(), screenAvailable.width());
        const auto height = toolbarSize.height();

        // 横向以选区中心对齐，再夹进可用区：靠右边缘的选区不该把工具栏推出屏幕。
        // 用 left + (选区宽 - 工具栏宽)/2 而不是 QRect::center()：后者的中心点会向低一位取整，
        // 偶数宽度的选区会整体偏 1 像素，手算的期望值也就对不上
        auto x = selection.left() + (selection.width() - width) / 2;
        x      = std::clamp(x, screenAvailable.left(), screenAvailable.right() - width + 1);

        // QRect 的下边是闭区间（bottom = top + height - 1），所以「放得下」判的是 bottom + 1
        const auto belowTop = selection.bottom() + 1 + kToolbarGap;
        const auto aboveTop = selection.top() - kToolbarGap - height;

        // 优先下方：右手持鼠标时上方那只手更少被挡住
        if (belowTop + height <= screenAvailable.bottom() + 1)
        {
            return ToolbarPlacement{QRect{x, belowTop, width, height}, ToolbarSide::Below};
        }
        if (aboveTop >= screenAvailable.top())
        {
            return ToolbarPlacement{QRect{x, aboveTop, width, height}, ToolbarSide::Above};
        }

        // 两侧都不够高：挑空隙更大的一侧贴边放，纵坐标夹回可用区。
        // 这里宁可压住选区边缘，也不让工具栏缺一角——缺角的那几个按钮就点不到了
        const auto belowRoom = screenAvailable.bottom() + 1 - belowTop;
        const auto aboveRoom = selection.top() - kToolbarGap - screenAvailable.top();
        const auto chosenTop = belowRoom >= aboveRoom ? belowTop : aboveTop;
        const auto side      = belowRoom >= aboveRoom ? ToolbarSide::Below : ToolbarSide::Above;

        // 屏幕比工具栏还矮时 maxY 会小于 top，用 max 兜住上界，避免 clamp 收到 lo > hi
        const auto maxY = std::max(screenAvailable.top(), screenAvailable.bottom() + 1 - height);
        const auto y    = std::clamp(chosenTop, screenAvailable.top(), maxY);
        return ToolbarPlacement{QRect{x, y, width, height}, side};
    }

} // namespace GyanisScribe::Annotation
