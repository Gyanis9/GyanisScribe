#include "Record/RecorderConfig.h"

#include "Base/Choices.h"
#include "Base/Text.h"

#include <format>

namespace
{
    /// 校验失败统一走这一个出口：原因里必须点出是哪个字段
    GyanisScribe::Base::Expected<void> reject(std::string_view field, std::string_view advice)
    {
        return std::unexpected(GyanisScribe::Base::Error{
                .code    = GyanisScribe::Base::ErrorCode::InvalidArgument,
                .message = GyanisScribe::Base::uiFormat(GyanisScribe::Base::uiTextUtf8("录屏参数 {}：{}"), std::string{field}, std::string{advice}),
        });
    }

} // namespace

namespace GyanisScribe::Record
{
    Base::Expected<void> validateRecorderConfig(const RecorderConfig &config)
    {
        // 屏幕名先判：拔屏之后每一帧都会失败，那种逐帧报错不如开始前一句说清
        if (config.screenName.isEmpty())
        {
            return reject("screenName", GyanisScribe::Base::uiTextUtf8("要写清录哪块屏幕。请在录屏面板重新选一次目标屏幕"));
        }

        // 帧率按输出格式分档：GIF 的上限比 MP4 低，理由见 kMaximumGifFrameRate 的注释
        const auto maximumFrameRate = config.format == OutputFormat::Gif ? kMaximumGifFrameRate : kMaximumFrameRate;
        if (config.frameRate < 1 || config.frameRate > maximumFrameRate)
        {
            return reject("frameRate", GyanisScribe::Base::uiFormat(GyanisScribe::Base::uiTextUtf8("取值要在 1 到 {} 之间（{} 格式的上限），当前是 {}。帧率越高文件越大、编码越慢"),
                                                                    maximumFrameRate, config.format == OutputFormat::Gif ? "GIF" : "MP4", config.frameRate));
        }

        // 码率只在 MP4 那一档起作用（GIF 走两遍调色板），但范围在这里就判掉：
        // 负值会让 ffmpeg 报「无效参数」而那句不带字段名，用户在界面上看不出是这一项写歪了
        if (config.videoBitrateKbps < kDefaultVideoBitrateKbps || config.videoBitrateKbps > kMaximumVideoBitrateKbps)
        {
            return reject("videoBitrateKbps", GyanisScribe::Base::uiFormat(GyanisScribe::Base::uiTextUtf8("取值要在 0 到 {} 之间（0 表示按质量优先），当前是 {}。码率越高文件越大"),
                                                                           kMaximumVideoBitrateKbps, config.videoBitrateKbps));
        }

        if (config.maximumDurationSeconds < 1 || config.maximumDurationSeconds > kMaximumDurationSeconds)
        {
            return reject("maximumDurationSeconds",
                          GyanisScribe::Base::uiFormat(GyanisScribe::Base::uiTextUtf8("取值要在 1 到 {} 之间，当前是 {}。留一个上限是防止录屏忘了关，真需要更长就分两段录"),
                                                       kMaximumDurationSeconds, config.maximumDurationSeconds));
        }

        if (config.outputDirectory.empty())
        {
            return reject("outputDirectory", GyanisScribe::Base::uiTextUtf8("不能为空。请在设置的录屏页选一个可写目录"));
        }

        // 模式相关的必填项各自判自己的，别用「region 为空就是全屏」这种默认：
        // 用户以为在录选区、实际录了整屏，这段就废了
        if (config.mode == RecordMode::Region && (config.region.width() <= 0 || config.region.height() <= 0))
        {
            return reject("region", GyanisScribe::Base::uiTextUtf8("选区模式需要一个非空矩形，当前是空矩形。请重新框选，或改用整屏模式"));
        }
        if (config.mode == RecordMode::Window && config.windowHandle == 0)
        {
            return reject("windowHandle", GyanisScribe::Base::uiTextUtf8("窗口模式需要一个有效句柄，当前是 0。请在目标窗口还在的时候重新选一次"));
        }
        return {};
    }

    Base::Expected<int> parseFrameRateSetting(const int stored)
    {
        // 只报「不合法」不够：帧率是用户自己填的，范围与后果都要说清（越高越大越慢）
        if (stored >= 1 && stored <= kMaximumFrameRate)
        {
            return stored;
        }
        return Base::Expected<int>{std::unexpected{Base::Error{
                .code    = Base::ErrorCode::InvalidArgument,
                .message = GyanisScribe::Base::uiFormat(
                        GyanisScribe::Base::uiTextUtf8("默认帧率 \"{}\" 不在可用范围（1 到 {}）里：请在设置的录屏页重选一个值，或删掉 Record/DefaultFrameRate 这一项回到默认档"),
                        stored, kMaximumFrameRate),
        }}};
    }

    Base::Expected<int> parseVideoBitrateSetting(const int stored)
    {
        // 0 是「按质量优先」这一档，不是没填：负值与超出上限的大值才是手改配置写歪了
        if (stored >= kDefaultVideoBitrateKbps && stored <= kMaximumVideoBitrateKbps)
        {
            return stored;
        }
        return Base::Expected<int>{std::unexpected{Base::Error{
                .code    = Base::ErrorCode::InvalidArgument,
                .message = GyanisScribe::Base::uiFormat(
                        GyanisScribe::Base::uiTextUtf8(
                                "画面码率 \"{}\" 不在可用范围（0 到 {} kbps，0 表示按质量优先）里：请在设置的录屏页重选，或删掉 Record/VideoBitrateKbps 这一项回到默认档"),
                        stored, kMaximumVideoBitrateKbps),
        }}};
    }

    Base::Expected<int> parseMaximumDurationSetting(const int stored)
    {
        if (stored >= 1 && stored <= kMaximumDurationSeconds)
        {
            return stored;
        }
        return Base::Expected<int>{std::unexpected{Base::Error{
                .code = Base::ErrorCode::InvalidArgument,
                .message =
                        GyanisScribe::Base::uiFormat(GyanisScribe::Base::uiTextUtf8("最长录制时长「{}」不在可用范围（1 到 {} 秒，即最多 {} 分钟）里：请在设置的录屏页重选，或删掉 "
                                                                                    "Record/MaximumDurationSeconds 这一项回到默认的 {} 秒"),
                                                     stored, kMaximumDurationSeconds, kMaximumDurationSeconds / 60, kDefaultMaximumDurationSeconds),
        }}};
    }

    Base::Expected<int> parseCountdownSetting(const int stored)
    {
        // 0 是「立即开始」这一档，不是没填：负值与超出上限的大值才是手改配置写歪了
        if (stored >= 0 && stored <= kMaximumCountdownSeconds)
        {
            return stored;
        }
        return Base::Expected<int>{std::unexpected{Base::Error{
                .code    = Base::ErrorCode::InvalidArgument,
                .message = GyanisScribe::Base::uiFormat(
                        GyanisScribe::Base::uiTextUtf8(
                                "倒计时「{}」不在可用范围（0 到 {} 秒，0 表示立即开始）里：请在设置的录屏页重选，或删掉 Record/CountdownSeconds 这一项回到默认档"),
                        stored, kMaximumCountdownSeconds),
        }}};
    }

    std::string_view recordScopeText(const RecordMode mode) noexcept
    {
        for (const auto &choice: kRecordScopeChoices)
        {
            if (choice.mode == mode)
            {
                return choice.token;
            }
        }
        // 词表里没有这一档：新加枚举值时会走到这里，交回空由调用方决定报法（表格与枚举不同解就是缺陷）
        return {};
    }

    Base::Expected<RecordMode> parseRecordScope(const QString &stored)
    {
        const auto wanted = stored.toStdString();
        for (const auto &choice: kRecordScopeChoices)
        {
            if (wanted == choice.token)
            {
                return choice.mode;
            }
        }
        // 不替用户猜一档：悄悄按整屏开始录，录下来的画面与他想要的完全不是一回事
        return std::unexpected(Base::unknownTokenError(kRecordScopeChoices, stored, Base::uiTextUtf8("录制范围"), Base::uiTextUtf8("录屏页"), "Record/Scope"));
    }

    std::string_view recordFormatText(const OutputFormat format) noexcept
    {
        for (const auto &choice: kRecordFormatChoices)
        {
            if (choice.format == format)
            {
                return choice.token;
            }
        }
        // 词表里没有这一档：新加枚举值时会走到这里，交回空由调用方决定报法（表格与枚举不同解就是缺陷）
        return {};
    }

    Base::Expected<OutputFormat> parseRecordFormat(const QString &stored)
    {
        const auto wanted = stored.toStdString();
        for (const auto &choice: kRecordFormatChoices)
        {
            if (wanted == choice.token)
            {
                return choice.format;
            }
        }
        // 格式定不下来就不开录：猜错的那一种会把已经抓完的帧按错的容器压出去，成品直接废掉
        return std::unexpected(Base::unknownTokenError(kRecordFormatChoices, stored, Base::uiTextUtf8("输出格式"), Base::uiTextUtf8("录屏页"), "Record/Format"));
    }

} // namespace GyanisScribe::Record
