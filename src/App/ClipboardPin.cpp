#include "App/ClipboardPin.h"

#include "Core/Text.h"

#include <QMimeData>
#include <QVariant>

namespace GyanisScribe::App
{
    Core::Expected<QImage> pinnableImageFrom(const QMimeData *data)
    {
        if (data == nullptr || !data->hasImage())
        {
            return std::unexpected(Core::Error{Core::ErrorCode::InvalidArgument, Core::uiTextUtf8("剪贴板里没有图像：先复制一张图，或按截图热键截一张再来贴。这一条只认图像，"
                                                                                                  "文本与颜色还没有转贴图的做法")});
        }

        // 有图像那一路、却读不出像素：来源只挂了名目没给内容（受保护内容、跨会话拷贝的那类）
        const QImage image = qvariant_cast<QImage>(data->imageData());
        if (image.isNull())
        {
            return std::unexpected(
                    Core::Error{Core::ErrorCode::InvalidArgument, Core::uiTextUtf8("剪贴板里的图像读不出来（来源可能只挂了名目、没给像素）：换一张图复制，或先截图再来贴")});
        }
        return image;
    }
} // namespace GyanisScribe::App
