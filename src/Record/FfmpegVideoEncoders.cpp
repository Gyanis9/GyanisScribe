#include "Record/FfmpegVideoEncoders.h"

#include "Core/Text.h"

#include <array>

namespace GyanisScribe::Record
{
    QStringList videoEncoderQualityArguments(const std::string_view encoder, const int videoBitrateKbps)
    {
        // 码率给了就走按码率控，没给才走各家的质量档
        if (videoBitrateKbps > 0)
        {
            const auto bitrate = QStringLiteral("%1k").arg(videoBitrateKbps);
            if (encoder == "libx264")
            {
                return {QStringLiteral("-preset"), QStringLiteral("veryfast"), QStringLiteral("-b:v"), bitrate};
            }
            if (encoder == "h264_nvenc")
            {
                return {QStringLiteral("-preset"), QStringLiteral("p4"), QStringLiteral("-b:v"), bitrate};
            }
            // qsv 与 amf 的质量档名各家不同且本机没有对应硬件，只给 ffmpeg 自己认得的通用参数：
            // 探测那一步会告诉我们这一档在这台机器上到底开不开得起来
            return {QStringLiteral("-b:v"), bitrate};
        }

        if (encoder == "libx264")
        {
            return {QStringLiteral("-preset"), QStringLiteral("veryfast"), QStringLiteral("-crf"), QStringLiteral("23")};
        }
        if (encoder == "h264_nvenc")
        {
            // -cq 是 NVENC 那一侧对应 CRF 的说法，实测与 -b:v 两种形状都开得起来
            return {QStringLiteral("-preset"), QStringLiteral("p4"), QStringLiteral("-cq"), QStringLiteral("23")};
        }
        if (encoder == "mpeg4")
        {
            // mpeg4 没有 CRF 一档，-q:v 越小越好，5 是屏幕内容的可用档
            return {QStringLiteral("-q:v"), QStringLiteral("5")};
        }
        return {};
    }

    QStringList videoEncoderProbeArguments(const std::string_view encoder, const int videoBitrateKbps, const int width, const int height, const int frameRate)
    {
        QStringList arguments{
                QStringLiteral("-hide_banner"),
                QStringLiteral("-loglevel"),
                QStringLiteral("error"),
                QStringLiteral("-y"),
                QStringLiteral("-f"),
                QStringLiteral("lavfi"),
                // 尺寸与帧率都按真录屏那一档给：硬件编码器有帧尺寸上限，小图探出来的「能用」不作数
                // 尺寸与帧率都按真录屏那一档给：硬件编码器有帧尺寸上限，小图探出来的「能用」不作数
                QStringLiteral("-i"),
                QStringLiteral("color=c=black:s=%1x%2:r=%3").arg(width).arg(height).arg(frameRate),
                QStringLiteral("-frames:v"),
                QStringLiteral("3"),
                QStringLiteral("-c:v"),
                QString::fromStdString(std::string{encoder}),
        };
        arguments << videoEncoderQualityArguments(encoder, videoBitrateKbps);
        // 像素格式与真编码同一档：只有编码器能配 yuv420p 时才值得把这段序列编下去
        arguments << QStringLiteral("-pix_fmt") << QStringLiteral("yuv420p") << QStringLiteral("-f") << QStringLiteral("null") << QStringLiteral("-");
        return arguments;
    }

    Core::Expected<std::string> selectVideoEncoder(const std::function<bool(const QStringList &)> &run, const int width, const int height, const int frameRate,
                                                   const int videoBitrateKbps)
    {
        // h264_mf 不在候选里：本机同一条件连跑五次，四次是退出码 139 自己崩了，只留一个 48 字节的空壳
        for (const auto encoder: kPreferredVideoEncoders)
        {
            if (run(videoEncoderProbeArguments(encoder, videoBitrateKbps, width, height, frameRate)))
            {
                return std::string{encoder};
            }
        }

        QStringList tried;
        for (const auto encoder: kPreferredVideoEncoders)
        {
            tried << QString::fromUtf8(encoder.data(), static_cast<qsizetype>(encoder.size()));
        }
        return std::unexpected(Core::Error{
                .code    = Core::ErrorCode::DependencyMissing,
                .message = Core::uiFormat(Core::uiTextUtf8("你这份 ffmpeg 里没有能开起来的视频编码器（试过 {}）。请换装一个带 libx264 的完整 ffmpeg 构建，"
                                                           "或者先把这一帧序列自己用 ffmpeg 编一次看报什么"),
                                          tried.join(QStringLiteral("、")).toStdString()),
        });
    }

} // namespace GyanisScribe::Record
