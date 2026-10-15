#include "Core/Settings/OutputStyleRules.h"

#include "Base/Logger.h"
#include "Core/Image/OutputDecoration.h"
#include "Core/Settings/ISettingsStore.h"
#include "Core/Settings/SettingsKeys.h"

namespace GyanisScribe::Core
{

    Core::OutputDecorationStyle readOutputDecorationStyle(const ISettingsStore &settings)
    {
        Core::OutputDecorationStyle style;

        // 三格各读各的、各回各的档：某一格坏掉不该把另外两格一起拖回默认，那样用户会发现「只改了一个没生效」
        const auto border = Core::parseOutputBorderPixels(settings.intValue(kOutputBorderWidth, Core::kOutputBorderPixels));
        if (border)
        {
            style.borderPixels = *border;
        } else
        {
            Base::Logger::warning(border.error().message);
        }

        const auto shadow = Core::parseOutputShadowPixels(settings.intValue(kOutputShadowWidth, Core::kOutputShadowPixels));
        if (shadow)
        {
            style.shadowPixels = *shadow;
        } else
        {
            Base::Logger::warning(shadow.error().message);
        }

        // 颜色那一格没存过时 stringValue 给空串，parse 把空串当默认档（不是坏值）
        const auto color = Core::parseOutputBorderColor(settings.stringValue(kOutputBorderColor, QString()));
        if (color)
        {
            style.borderColor = *color;
        } else
        {
            Base::Logger::warning(color.error().message);
        }

        return style;
    }

} // namespace GyanisScribe::Core
