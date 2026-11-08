#include "Core/Rewind/RewindFrameCodec.h"

#include "Base/Text.h"

#include <QBuffer>

namespace GyanisScribe::Core
{
    namespace
    {
        constexpr const char *kRewindImageFormat = "PNG";
    } // namespace

    Base::Expected<RewindEncodedFrame> encodeRewindFrame(const QImage &image)
    {
        // 空图编码出来只会是一段打不开的数据，按调用方 bug 就地拒绝
        if (image.isNull())
        {
            return std::unexpected(Base::Error{Base::ErrorCode::InvalidArgument, Base::uiTextUtf8("回溯帧编码失败：源图是空的")});
        }

        QByteArray bytes;
        QBuffer    buffer(&bytes);
        // 内存缓冲打不开与写不进去分开报：两者的处置不同，混成一条就分不出是内存还是格式问题
        if (!buffer.open(QIODevice::WriteOnly))
        {
            return std::unexpected(Base::Error{Base::ErrorCode::InternalError, Base::uiTextUtf8("回溯帧编码失败：内存缓冲打不开")});
        }
        if (!image.save(&buffer, kRewindImageFormat))
        {
            return std::unexpected(Base::Error{Base::ErrorCode::InternalError, Base::uiTextUtf8("回溯帧编码失败：PNG 写不进去")});
        }
        return RewindEncodedFrame{bytes, makeRewindThumbnail(image)};
    }

    Base::Expected<QImage> decodeRewindFrame(const QByteArray &encodedPixels)
    {
        QImage image;
        // 空数据不走 Qt 的探测；损坏数据由 loadFromData 返回 false，两种都归到同一份可重试文案
        if (encodedPixels.isEmpty() || !image.loadFromData(encodedPixels) || image.isNull())
        {
            return std::unexpected(Base::Error{Base::ErrorCode::InternalError, Base::uiTextUtf8("回溯帧解码失败：数据损坏或格式不支持。请重新选一帧")});
        }
        return image;
    }

    QImage makeRewindThumbnail(const QImage &image, const int maximumEdge)
    {
        // 上限非正时缩放结果没有意义，返回空图让调用方看见入参问题，而不是悄悄改成某个档位
        if (image.isNull() || maximumEdge <= 0)
        {
            return {};
        }
        QImage thumbnail = image;
        if (image.width() > maximumEdge || image.height() > maximumEdge)
        {
            // 只缩不放：放大既糊又白占内存，选择器按原尺寸显示更清楚
            thumbnail = image.scaled(maximumEdge, maximumEdge, Qt::KeepAspectRatio, Qt::FastTransformation);
        }
        // 缩略图按 1:1 显示，清除原图的缩放因子，否则高分屏上会被再次拉伸
        thumbnail.setDevicePixelRatio(1.0);
        return thumbnail;
    }
} // namespace GyanisScribe::Core
