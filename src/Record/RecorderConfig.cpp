#include "Record/RecorderConfig.h"

#include "Core/Text.h"

#include <format>

namespace
{
    /// 校验失败统一走这一个出口：原因里必须点出是哪个字段
    GyanisScribe::Core::Expected<void> reject(std::string_view field, std::string_view advice)
    {
        return std::unexpected(GyanisScribe::Core::Error{
                .code    = GyanisScribe::Core::ErrorCode::InvalidArgument,
                .message = GyanisScribe::Core::uiFormat(GyanisScribe::Core::uiTextUtf8("录屏参数 {}：{}"), std::string{field}, std::string{advice}),
        });
    }
} // namespace

namespace GyanisScribe::Record
{
    Core::Expected<void> validateRecorderConfig(const RecorderConfig &config)
    {
        // 屏幕名先判：拔屏之后每一帧都会失败，那种逐帧报错不如开始前一句说清
        if (config.screenName.isEmpty())
        {
            return reject("screenName", GyanisScribe::Core::uiTextUtf8("要写清录哪块屏幕。请在录屏面板重新选一次目标屏幕"));
        }

        // 帧率按输出格式分档：GIF 的上限比 MP4 低，理由见 kMaximumGifFrameRate 的注释
        const auto maximumFrameRate = config.format == OutputFormat::Gif ? kMaximumGifFrameRate : kMaximumFrameRate;
        if (config.frameRate < 1 || config.frameRate > maximumFrameRate)
        {
            return reject("frameRate", GyanisScribe::Core::uiFormat(GyanisScribe::Core::uiTextUtf8("取值要在 1 到 {} 之间（{} 格式的上限），当前是 {}。帧率越高文件越大、编码越慢"),
                                                                    maximumFrameRate, config.format == OutputFormat::Gif ? "GIF" : "MP4", config.frameRate));
        }

        if (config.maximumDurationSeconds < 1 || config.maximumDurationSeconds > kMaximumDurationSeconds)
        {
            return reject("maximumDurationSeconds",
                          GyanisScribe::Core::uiFormat(GyanisScribe::Core::uiTextUtf8("取值要在 1 到 {} 之间，当前是 {}。留一个上限是防止录屏忘了关，真需要更长就分两段录"),
                                                       kMaximumDurationSeconds, config.maximumDurationSeconds));
        }

        if (config.outputDirectory.empty())
        {
            return reject("outputDirectory", GyanisScribe::Core::uiTextUtf8("不能为空。请在设置的录屏页选一个可写目录"));
        }

        // 模式相关的必填项各自判自己的，别用「region 为空就是全屏」这种默认：
        // 用户以为在录选区、实际录了整屏，这段就废了
        if (config.mode == RecordMode::Region && (config.region.width() <= 0 || config.region.height() <= 0))
        {
            return reject("region", GyanisScribe::Core::uiTextUtf8("选区模式需要一个非空矩形，当前是空矩形。请重新框选，或改用整屏模式"));
        }
        if (config.mode == RecordMode::Window && config.windowHandle == 0)
        {
            return reject("windowHandle", GyanisScribe::Core::uiTextUtf8("窗口模式需要一个有效句柄，当前是 0。请在目标窗口还在的时候重新选一次"));
        }
        return {};
    }

} // namespace GyanisScribe::Record
