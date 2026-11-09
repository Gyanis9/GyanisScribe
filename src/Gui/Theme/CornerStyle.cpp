#include "Gui/Theme/CornerStyle.h"

namespace GyanisScribe::Gui
{
    int themeBorderWidth(const ThemeKind kind) noexcept
    {
        return kind == ThemeKind::HighContrast ? 2 : 1;
    }
} // namespace GyanisScribe::Gui
