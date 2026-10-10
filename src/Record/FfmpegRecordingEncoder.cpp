#include "Record/FfmpegRecordingEncoder.h"

#include "Core/Logger.h"
#include "Core/Paths.h"
#include "Core/Text.h"
#include "Record/FfmpegCommand.h"

#include <QDateTime>
#include <QProcess>

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
} // namespace

namespace GyanisScribe::Record
{
    int FfmpegRecordingEncoder::encodeTimeoutMs(const int frameRate, const int frameCount)
    {
        // 按「实际时长 × 4」给余量，再垫 30 秒：几秒钟的短片不能因为余量算出来太小而被误杀
        const auto recordedSeconds = frameRate > 0 ? frameCount / frameRate : 0;
        return 30'000 + recordedSeconds * 4'000;
    }

    Core::Expected<std::filesystem::path> FfmpegRecordingEncoder::encode(const RecorderConfig &config, const std::filesystem::path &frameDirectory, const int frameCount,
                                                                         const std::filesystem::path &audioWav)
    {
        const auto located = locateFfmpeg();
        if (!located)
        {
            return std::unexpected(located.error());
        }

        const auto extension  = config.format == OutputFormat::Mp4 ? "mp4" : "gif";
        const auto outputFile = config.outputDirectory / Core::Paths::imageFileName(QDateTime::currentDateTime(), extension).toStdString();
        const auto timeout    = encodeTimeoutMs(config.frameRate, frameCount);

        if (config.format == OutputFormat::Mp4)
        {
            if (auto ran = run(mp4Arguments(config, frameDirectory, outputFile, audioWav), timeout, GyanisScribe::Core::uiTextUtf8("MP4 编码")); !ran)
            {
                return std::unexpected(ran.error());
            }
        } else
        {
            // GIF 走两遍：先生成调色板再按它编码，一遍过会让渐变与文字边缘出现明显色带
            const auto paletteFile = frameDirectory / "palette.png";
            if (auto palette = run(gifPaletteArguments(config, frameDirectory, paletteFile), timeout, GyanisScribe::Core::uiTextUtf8("GIF 调色板")); !palette)
            {
                return std::unexpected(palette.error());
            }
            if (auto encoded = run(gifEncodeArguments(config, frameDirectory, paletteFile, outputFile), timeout, GyanisScribe::Core::uiTextUtf8("GIF 编码")); !encoded)
            {
                return std::unexpected(encoded.error());
            }
        }

        std::error_code error;
        const auto      size = std::filesystem::file_size(outputFile, error);
        // 退出码为 0 但成品不在或是空文件，是 ffmpeg 与磁盘之间的事，不能当成成功交回上层
        if (error || size == 0)
        {
            const auto message = GyanisScribe::Core::uiFormat(
                    GyanisScribe::Core::uiTextUtf8("ffmpeg 报告成功，但成品不可用：{}。请检查磁盘空间后重编码，或先用剪贴板交付这一帧序列"), outputFile.string());
            Core::Logger::error(message);
            return std::unexpected(Core::Error{Core::ErrorCode::FileWriteFailed, message});
        }

        Core::Logger::info(std::format("录屏编码完成：{}（{} 帧）", outputFile.string(), frameCount));
        return outputFile;
    }

    Core::Expected<void> FfmpegRecordingEncoder::run(const QStringList &arguments, const int timeoutMs, const std::string_view stepName)
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
            const auto message = GyanisScribe::Core::uiFormat(GyanisScribe::Core::uiTextUtf8("{} 起不动 ffmpeg（{}）。请确认该文件还能运行，或重新安装"), std::string{stepName},
                                                              located->toStdString());
            Core::Logger::error(message);
            return std::unexpected(Core::Error{Core::ErrorCode::DependencyMissing, message});
        }

        if (!process.waitForFinished(timeoutMs))
        {
            // 超时后必须收尸：留一个还在写文件的 ffmpeg 在后台，下一轮录屏会撞上同一个目录
            process.kill();
            process.waitForFinished(1000);
            const auto message = GyanisScribe::Core::uiFormat(GyanisScribe::Core::uiTextUtf8("{} 超时（{} 毫秒未完成）。可以缩短录屏时长，或先把成品交剪贴板"),
                                                              std::string{stepName}, timeoutMs);
            Core::Logger::error(message);
            return std::unexpected(Core::Error{Core::ErrorCode::Busy, message});
        }

        if (process.exitStatus() != QProcess::NormalExit || process.exitCode() != 0)
        {
            const auto output  = tailOf(QString::fromUtf8(process.readAll()));
            const auto message = GyanisScribe::Core::uiFormat(GyanisScribe::Core::uiTextUtf8("{} 失败（退出码 {}）：{}。完整输出在日志里，也可以自己敲一遍同样的命令看细节"),
                                                              std::string{stepName}, process.exitCode(), output);
            Core::Logger::error(message);
            return std::unexpected(Core::Error{Core::ErrorCode::InternalError, message});
        }
        return {};
    }

} // namespace GyanisScribe::Record
