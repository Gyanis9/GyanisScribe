#include "Ui/AppResources.h"

// Q_INIT_RESOURCE 的头文件位置在 6.8 挪过一次：6.4（Ubuntu 24.04 那份）只在 qglobal.h 里给，
// 6.8 起才有独立的 qtresource.h。少这一层条件，容器里连编都编不过
#include <QtCore/QtGlobal>
#if QT_VERSION >= QT_VERSION_CHECK(6, 8, 0)
#include <QtCore/qtresource.h>
#endif

#include <tuple>

/**
 * @brief 在全局作用域展开 Q_INIT_RESOURCE
 * @details 宏里的 extern 声明带着展开处的限定名，而 rcc 生成的是全局符号：写在任何名字空间里
 *          （含匿名名字空间）都会报 undefined reference to
 *          '<那个名字空间>::qInitResources_AppResources()'。MSVC 一侧恰好对得上，
 *          所以这条只能在容器里暴露——不是推测，是链接器给的原文。
 */
static void invokeResourceInit()
{
    Q_INIT_RESOURCE(AppResources);
}

namespace GyanisScribe::Ui
{
    void ensureApplicationResourcesRegistered()
    {
        // 只在第一次调用时初始化：资源注册本身是进程级动作，重复注册没有额外效果
        static const bool registered = []
        {
            invokeResourceInit();
            return true;
        }();
        std::ignore = registered;
    }

} // namespace GyanisScribe::Ui
