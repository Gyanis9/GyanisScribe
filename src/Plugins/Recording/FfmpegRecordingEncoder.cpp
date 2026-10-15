#include "Plugins/Recording/FfmpegRecordingEncoder.h"

#include "Base/Logger.h"
#include "Base/Paths.h"
#include "Base/Text.h"
#include "Core/Record/RecordingSinks.h"
#include "Plugins/Recording/FfmpegCommand.h"
#include "Plugins/Recording/FfmpegVideoEncoders.h"

#include <QDateTime>
#include <QImageReader>
#include <QProcess>
#include <QSize>

#include <format>
#include <utility>

namespace
{
    /// ffmpeg 的报错很长，取末段就够定位：它把关键的那句写在最后
    std::string tailOf(const QString &text)
    {
        const auto trimmed = text.trimmed();
        if (trimmed.size() <= 400)
        {
            return trimmed.toStdString();
        }
        return trimmed.right(400).toStdString();
    }

    /// 一个候选编码器的探测上限，毫秒：本机实测最慢的一档 1.6 秒（缺硬件运行库的那一类要等它自己失败），
    /// 15 秒是留给磁盘冷启动的余量
    constexpr int kEncoderProbeTimeoutMs = 15000;
} // namespace

namespace GyanisScribe::Plugins
{
    int FfmpegRecordingEncoder::encodeTimeoutMs(const int frameRate, const int frameCount)
    {
        // 按「实际时长 × 4」给余量，再垫 30 秒：几秒钟的短片不能因为余量算出来太小而被误杀
        const auto recordedSeconds = frameRate > 0 ? frameCount / frameRate : 0;
        return 30'000 + recordedSeconds * 4'000;
    }

    Base::Expected<QSize> FfmpegRecordingEncoder::firstFrameSize(const std::filesystem::path &frameDirectory)
    {
        QImageReader first{Base::Paths::toDisplayText(frameDirectory / Core::frameFileName(0))};
        const auto   size = first.size();
        // 首帧读不出就别往下探：尺寸是探的输入，拿 0x0 去问编码器只会得到一条与真因无关的失败
        if (size.isEmpty())
        {
            const auto message = GyanisScribe::Base::uiFormat(GyanisScribe::Base::uiTextUtf8("第一帧读不出尺寸（{}）。请确认录屏期间磁盘没满，或先用剪贴板交付这一帧序列"),
                                                              Base::Paths::toDisplayText(frameDirectory).toStdString());
            Base::Logger::error(message);
            return std::unexpected(Base::Error{Base::ErrorCode::FileWriteFailed, message});
        }
        return size;
    }

    Base::Expected<std::string> FfmpegRecordingEncoder::selectEncoder(const Core::RecorderConfig &config, const QSize &frameSize)
    {
        // 每个候选都真跑三帧：名单里有不等于开得起来（缺 Intel/AMD 运行库的那些照样在名单上）
        const auto probe = [](const QStringList &arguments) { return run(arguments, kEncoderProbeTimeoutMs, GyanisScribe::Base::uiTextUtf8("编码器探测")).has_value(); };

        const auto selected = selectVideoEncoder(probe, frameSize.width(), frameSize.height(), config.frameRate, config.videoBitrateKbps, config.videoEncoder);
        if (selected)
        {
            if (!config.videoEncoder.empty() && *selected != config.videoEncoder)
            {
                // 帧已经录完了，这里不中途失败：钉的那一档这次开不起来（驱动或运行库变了）按自动档编出来，原因写清楚
                Base::Logger::warning(std::format("设置里钉的编码器 {} 这次探不过，已按自动档 {} 编这一轮", config.videoEncoder, *selected));
            }
            Base::Logger::info(std::format("录屏编码器：{}（{}x{}，{} 帧/秒）", *selected, frameSize.width(), frameSize.height(), config.frameRate));
        }
        return selected;
    }

    Base::Expected<std::filesystem::path> FfmpegRecordingEncoder::encode(const Core::RecorderConfig &config, const std::filesystem::path &frameDirectory, const int frameCount,
                                                                         const qint64 recordedDurationMs, const std::filesystem::path &audioWav)
    {
        const auto located = locateFfmpeg();
        if (!located)
        {
            return std::unexpected(located.error());
        }

        const auto extension = config.format == Core::OutputFormat::Mp4 ? "mp4" : "gif";
        // 名字按设置里那一格的模板拼，并让一次位：同一秒里连开两轮不会把上一轮悄悄盖掉
        const auto outputFile = Base::Paths::uniqueFilePath(config.outputDirectory, Base::Paths::imageFileName(QDateTime::currentDateTime(), extension, config.nameTemplate));
        const auto timeout    = encodeTimeoutMs(config.frameRate, frameCount);

        if (config.format == Core::OutputFormat::Mp4)
        {
            // 编码器要按真录屏的尺寸探：硬件编码器有帧尺寸上限，用小图探出来的「能用」不作数
            const auto frameSize = firstFrameSize(frameDirectory);
            if (!frameSize)
            {
                return std::unexpected(frameSize.error());
            }

            const auto selected = selectEncoder(config, *frameSize);
            if (!selected)
            {
                return std::unexpected(selected.error());
            }

            // 帧数与这一轮的时长一起交给命令拼装：机器跟不上目标帧率时靠这两份把成品时长拧回真实
            const auto arguments = mp4Arguments(config, frameCount, recordedDurationMs, frameDirectory, outputFile, audioWav, *selected);
            if (auto ran = run(arguments, timeout, GyanisScribe::Base::uiTextUtf8("MP4 编码")); !ran)
            {
                return std::unexpected(ran.error());
            }
        } else
        {
            // GIF 走两遍：先生成调色板再按它编码，一遍过会让渐变与文字边缘出现明显色带
            const auto paletteFile = frameDirectory / "palette.png";
            if (auto palette = run(gifPaletteArguments(config, frameCount, recordedDurationMs, frameDirectory, paletteFile), timeout, GyanisScribe::Base::uiTextUtf8("GIF 调色板"));
                !palette)
            {
                return std::unexpected(palette.error());
            }
            if (auto encoded = run(gifEncodeArguments(config, frameCount, recordedDurationMs, frameDirectory, paletteFile, outputFile), timeout,
                                   GyanisScribe::Base::uiTextUtf8("GIF 编码"));
                !encoded)
            {
                return std::unexpected(encoded.error());
            }
        }

        std::error_code error;
        const auto      size = std::filesystem::file_size(outputFile, error);
        // 退出码为 0 但成品不在或是空文件，是 ffmpeg 与磁盘之间的事，不能当成成功交回上层
        if (error || size == 0)
        {
            const auto message =
                    GyanisScribe::Base::uiFormat(GyanisScribe::Base::uiTextUtf8("ffmpeg 报告成功，但成品不可用：{}。请检查磁盘空间后重编码，或先用剪贴板交付这一帧序列"),
                                                 Base::Paths::toDisplayText(outputFile).toStdString());
            Base::Logger::error(message);
            return std::unexpected(Base::Error{Base::ErrorCode::FileWriteFailed, message});
        }

        Base::Logger::info(std::format("录屏编码完成：{}（{} 帧）", Base::Paths::toDisplayText(outputFile).toStdString(), frameCount));
        return outputFile;
    }

    Base::Expected<void> FfmpegRecordingEncoder::run(const QStringList &arguments, const int timeoutMs, const std::string_view stepName)
    {
        const auto located = locateFfmpeg();
        if (!located)
        {
            return std::unexpected(located.error());
        }

        QProcess process;
        process.setProcessChannelMode(QProcess::MergedChannels);
        process.start(*located, arguments);

        if (!process.waitForStarted(timeoutMs))
        {
            const auto message = GyanisScribe::Base::uiFormat(GyanisScribe::Base::uiTextUtf8("{} 起不动 ffmpeg（{}）。请确认该文件还能运行，或重新安装"), std::string{stepName},
                                                              located->toStdString());
            Base::Logger::error(message);
            return std::unexpected(Base::Error{Base::ErrorCode::DependencyMissing, message});
        }

        if (!process.waitForFinished(timeoutMs))
        {
            // 超时后必须收尸：留一个还在写文件的 ffmpeg 在后台，下一轮录屏会撞上同一个目录
            process.kill();
            process.waitForFinished(1000);
            const auto message = GyanisScribe::Base::uiFormat(GyanisScribe::Base::uiTextUtf8("{} 超时（{} 毫秒未完成）。可以缩短录屏时长，或先把成品交剪贴板"),
                                                              std::string{stepName}, timeoutMs);
            Base::Logger::error(message);
            return std::unexpected(Base::Error{Base::ErrorCode::Busy, message});
        }

        if (process.exitStatus() != QProcess::NormalExit || process.exitCode() != 0)
        {
            const auto output  = tailOf(QString::fromUtf8(process.readAll()));
            const auto message = GyanisScribe::Base::uiFormat(GyanisScribe::Base::uiTextUtf8("{} 失败（退出码 {}）：{}。完整输出在日志里，也可以自己敲一遍同样的命令看细节"),
                                                              std::string{stepName}, process.exitCode(), output);
            Base::Logger::error(message);
            return std::unexpected(Base::Error{Base::ErrorCode::InternalError, message});
        }
        return {};
    }

} // namespace GyanisScribe::Plugins
