#include "Core/Image/ImageLoading.h"

#include "Base/Paths.h"
#include "Base/Text.h"

#include <QImageReader>

namespace GyanisScribe::Core
{
    Base::Expected<QImage> loadImageFile(const std::filesystem::path &filePath)
    {
        QImageReader reader{GyanisScribe::Base::Paths::toDisplayText(filePath)};
        reader.setAutoTransform(true);
        const auto image = reader.read();
        if (image.isNull())
        {
            return Base::Expected<QImage>{std::unexpected{Base::Error{
                    .code    = Base::ErrorCode::DependencyMissing,
                    .message = GyanisScribe::Base::uiFormat(GyanisScribe::Base::uiTextUtf8("读不出这张图：{}。文件可能已被移动或删掉，或这个构建里没有对应的图片解码插件"),
                                                            GyanisScribe::Base::Paths::toDisplayText(filePath).toStdString()),
            }}};
        }
        return Base::Expected<QImage>{image};
    }

} // namespace GyanisScribe::Core
