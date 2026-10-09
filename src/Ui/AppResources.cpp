#include "Ui/AppResources.h"

#include <QtCore/qtresource.h>

#include <tuple>

namespace GyanisScribe::Ui
{
    void ensureApplicationResourcesRegistered()
    {
        // 只在第一次调用时初始化：资源注册本身是进程级动作，重复注册没有额外效果
        static const bool registered = []
        {
            Q_INIT_RESOURCE(AppResources);
            return true;
        }();
        std::ignore = registered;
    }

} // namespace GyanisScribe::Ui
