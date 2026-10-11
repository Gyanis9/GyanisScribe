#include "Platform/Windows/Win32Autostart.h"

namespace GyanisScribe::Platform
{
    QString autostartCommandLine(const QString &executablePath)
    {
        const auto trimmed = executablePath.trimmed();
        if (trimmed.isEmpty())
        {
            return {};
        }
        return QLatin1Char('"') + trimmed + QLatin1Char('"');
    }
} // namespace GyanisScribe::Platform
