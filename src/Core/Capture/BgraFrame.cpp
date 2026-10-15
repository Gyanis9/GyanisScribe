#include "Core/Capture/BgraFrame.h"

#include <cstddef>
#include <cstring>

namespace GyanisScribe::Core
{
    QImage imageFromBgraRows(const uchar *data, const int pitch, const QSize &size)
    {
        if (data == nullptr || size.width() <= 0 || size.height() <= 0 || pitch < size.width() * 4)
        {
            return {};
        }

        // Format_RGB32 的字节序就是 B、G、R、忽略：源缓冲同一序，按行整拷即可
        QImage     image(size, QImage::Format_RGB32);
        const auto rowBytes = static_cast<std::size_t>(size.width()) * 4;
        for (int y = 0; y < size.height(); ++y)
        {
            std::memcpy(image.scanLine(y), data + static_cast<std::ptrdiff_t>(y) * pitch, rowBytes);
        }
        return image;
    }

} // namespace GyanisScribe::Core
