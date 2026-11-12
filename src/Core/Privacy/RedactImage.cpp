#include "Core/Privacy/RedactImage.h"

#include "Base/Text.h"
#include "Core/Annotation/AnnotationEngine.h"
#include "Core/Annotation/AnnotationShape.h"
#include "Core/Privacy/RedactionGeometry.h"

#include <utility>
#include <vector>

namespace GyanisScribe::Core::Privacy
{
    Base::Expected<RedactedImage> redactImage(const QImage &image, const std::span<const QRect> physicalBoxes, const GyanisScribe::Core::ToolKind tool,
                                              const QSize logicalSize, const int mosaicBlockSize)
    {
        if (image.isNull())
        {
            return std::unexpected(Base::Error{Base::ErrorCode::InvalidArgument, Base::uiTextUtf8("没有可打码的图。请先截图或打开一张图片")});
        }
        if (physicalBoxes.empty())
        {
            return std::unexpected(Base::Error{Base::ErrorCode::InvalidArgument, Base::uiTextUtf8("没有可打码的框")});
        }
        if (logicalSize.isEmpty())
        {
            return std::unexpected(Base::Error{Base::ErrorCode::InvalidArgument, Base::uiTextUtf8("画布逻辑尺寸为空，无法换算打码框")});
        }
        if (tool != GyanisScribe::Core::ToolKind::Mosaic && tool != GyanisScribe::Core::ToolKind::Blur)
        {
            return std::unexpected(Base::Error{Base::ErrorCode::InvalidArgument, Base::uiTextUtf8("只支持马赛克或高斯模糊两种打码方式")});
        }
        if (tool == GyanisScribe::Core::ToolKind::Mosaic && mosaicBlockSize <= 0)
        {
            return std::unexpected(Base::Error{Base::ErrorCode::InvalidArgument, Base::uiTextUtf8("马赛克粒度必须大于 0")});
        }

        // 模糊不看粒度：入参为 0 或负数时给一个正常值，免得请求里带个对引擎无意义的读数
        const int effectiveBlockSize =
                mosaicBlockSize > 0 ? mosaicBlockSize : GyanisScribe::Core::kMosaicBlockSizes[GyanisScribe::Core::kDefaultMosaicBlockStep];

        std::vector<GyanisScribe::Core::RedactionRequest> requests;
        requests.reserve(physicalBoxes.size());
        for (const auto &box: physicalBoxes)
        {
            if (const auto logical = logicalRedactionRect(box, image.size(), logicalSize); logical)
            {
                requests.push_back(GyanisScribe::Core::RedactionRequest{.logicalRect = *logical, .mosaicBlockSize = effectiveBlockSize});
            }
        }
        if (requests.empty())
        {
            return std::unexpected(Base::Error{Base::ErrorCode::InvalidArgument, Base::uiTextUtf8("所有打码框都在画布之外，未改动图片")});
        }

        Base::CapturedImage canvas;
        canvas.image            = image;
        canvas.logicalSize      = logicalSize;
        canvas.devicePixelRatio = logicalSize.width() > 0 ? static_cast<qreal>(image.width()) / static_cast<qreal>(logicalSize.width()) : 1.0;

        // 离屏引擎只活这一次调用：不改标注画布、不进撤销栈，也不留任何历史
        GyanisScribe::Core::AnnotationEngine engine;
        engine.loadCanvas(std::move(canvas));
        if (const auto added = engine.addRedactions(requests, tool); !added)
        {
            return std::unexpected(added.error());
        }
        QImage redacted = engine.exportImage();
        if (redacted.isNull())
        {
            return std::unexpected(Base::Error{Base::ErrorCode::Unknown, Base::uiTextUtf8("自动打码没有产出图像，请重试")});
        }
        return RedactedImage{std::move(redacted), static_cast<int>(requests.size())};
    }
} // namespace GyanisScribe::Core::Privacy
