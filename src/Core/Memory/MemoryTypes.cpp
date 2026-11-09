#include "Core/Memory/MemoryTypes.h"

namespace GyanisScribe::Core
{
    std::string_view memorySourceToken(const MemorySource source)
    {
        return source == MemorySource::Import ? "import" : "capture";
    }

    std::optional<MemorySource> parseMemorySource(const std::string_view token)
    {
        if (token == "capture")
        {
            return MemorySource::Capture;
        }
        if (token == "import")
        {
            return MemorySource::Import;
        }
        return std::nullopt;
    }
} // namespace GyanisScribe::Core
