#include "Ui/KeyStep.h"

#include <Qt>

namespace GyanisScribe::Ui
{
    std::optional<QPoint> arrowStepFor(const int key)
    {
        switch (key)
        {
            case Qt::Key_Left:
                return QPoint{-1, 0};
            case Qt::Key_Right:
                return QPoint{1, 0};
            case Qt::Key_Up:
                return QPoint{0, -1};
            case Qt::Key_Down:
                return QPoint{0, 1};
            default:
                // 别的键交回给调用方继续分发：这里认下不认识的键，界面级快捷键就少了兜底
                return {};
        }
    }

} // namespace GyanisScribe::Ui
