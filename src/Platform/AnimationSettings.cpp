#include "Platform/AnimationSettings.h"

#ifdef _WIN32
#include <windows.h>
#endif

namespace GyanisScribe::Platform
{
    bool animationsEnabled()
    {
#ifdef _WIN32
        // 这一档跟着系统的「显示动画」开关走；查询失败时按允许处理，不把取不到当成用户关掉了动效
        BOOL enabled = TRUE;
        if (SystemParametersInfoW(SPI_GETCLIENTAREAANIMATION, 0, &enabled, 0))
        {
            return enabled != FALSE;
        }
#endif
        return true;
    }
} // namespace GyanisScribe::Platform
