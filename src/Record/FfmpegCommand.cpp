#include "Record/FfmpegCommand.h"

#include "Core/Logger.h"

#include <QCoreApplication>
#include <QStandardPaths>

#include <format>

namespace
{
    /**
     * @brief 把本地路径换成命令行里的文本
     * @param path 目标路径
     * @details 走 fromLocal8Bit 而不是 fromUtf8：命令行参数要按操作系统的本地编码交给子进程，
     *          Windows 上中文目录名只有这条路不丢字。
     */
    QString fromPath(const std::filesystem::path &path)
    {
        return QString::fromLocal8Bit(path.string().c_str());
    }

    /// 帧序列输入的命令行写法：目录 + 定宽补零的模板
    QString sequencePattern(const std::filesystem::path &frameDirectory)
    {
        return fromPath(frameDirectory / std::string{GyanisScribe::Record::kFrameFilePattern});
    }
} // namespace

namespace GyanisScribe::Record
{
    std::filesystem::path frameFileName(const int index)
    {
        // 定宽补零：ffmpeg 按同一个模板认序列，序号不补零的话第 10 帧会排到第 2 帧前面
        return std::filesystem::path{std::format("{:06d}.png", index)};
    }

    QStringList mp4Arguments(const RecorderConfig &config, const std::filesystem::path &frameDirectory, const std::filesystem::path &outputFile,
                             const std::filesystem::path &audioWav)
    {
        QStringList arguments{
                QStringLiteral("-y"),
                QStringLiteral("-framerate"),
                QString::number(config.frameRate),
                QStringLiteral("-i"),
                sequencePattern(frameDirectory),
                // x264 要求宽高都是偶数：截图经常给出奇数高度，不这条缩放滤镜就直接报错退出
                QStringLiteral("-vf"),
                QStringLiteral("scale=trunc(iw/2)*2:trunc(ih/2)*2"),
                QStringLiteral("-c:v"),
                QStringLiteral("libx264"),
                QStringLiteral("-preset"),
                QStringLiteral("veryfast"),
                QStringLiteral("-crf"),
                QStringLiteral("23"),
                QStringLiteral("-pix_fmt"),
                QStringLiteral("yuv420p"),
                // faststart 把 moov 搬到文件头：不搬的话录完立刻上传会被播放器抱怨「正在下载」
                QStringLiteral("-movflags"),
                QStringLiteral("+faststart"),
        };

        if (audioWav.empty())
        {
            // 没声音是决定，不是意外：显式 -an，成品里就一条声轨都没有，而不是「看运气」
            arguments << QStringLiteral("-an");
        } else
        {
            arguments << QStringLiteral("-i") << fromPath(audioWav) << QStringLiteral("-c:a") << QStringLiteral("aac") << QStringLiteral("-b:a")
                      << QStringLiteral("128k")
                      // 视频与音频长度不一致时以短的为准：音频多出来的那一段会变成一段静音画面
                      << QStringLiteral("-shortest");
        }

        arguments << fromPath(outputFile);
        return arguments;
    }

    QStringList gifPaletteArguments(const RecorderConfig &config, const std::filesystem::path &frameDirectory, const std::filesystem::path &paletteFile)
    {
        return {
                QStringLiteral("-y"),
                QStringLiteral("-framerate"),
                QString::number(config.frameRate),
                QStringLiteral("-i"),
                sequencePattern(frameDirectory),
                // stats_mode=diff 按画面变化取色：截图里大块的静态背景不会把 256 个色位占满
                QStringLiteral("-vf"),
                QStringLiteral("fps=%1,palettegen=stats_mode=diff").arg(config.frameRate),
                fromPath(paletteFile),
        };
    }

    QStringList gifEncodeArguments(const RecorderConfig &config, const std::filesystem::path &frameDirectory, const std::filesystem::path &paletteFile,
                                   const std::filesystem::path &outputFile)
    {
        return {
                QStringLiteral("-y"),
                QStringLiteral("-framerate"),
                QString::number(config.frameRate),
                QStringLiteral("-i"),
                sequencePattern(frameDirectory),
                QStringLiteral("-i"),
                fromPath(paletteFile),
                QStringLiteral("-lavfi"),
                // 两遍法：第一遍出的调色板作为第二路输入，滤镜图里先把帧率对上再取色
                QStringLiteral("fps=%1[x];[x][1:v]paletteuse=dither=bayer:bayer_scale=3").arg(config.frameRate),
                fromPath(outputFile),
        };
    }

    Core::Expected<QString> locateFfmpeg()
    {
        if (const auto found = QStandardPaths::findExecutable(QStringLiteral("ffmpeg")); !found.isEmpty())
        {
            return found;
        }

        // 报「怎么装」而不是只报「没找到」：Windows 用户未必知道 ffmpeg 是一个要单独存在的命令行程序。
        // 这里不写具体网址与包名，避免哪天源变了这条提示就成了误导
        const auto hint = QStringLiteral("没找到 ffmpeg 命令行程序，录屏的编码这一步要用它。"
                                         "请安装 ffmpeg 的 Windows 构建并把所在目录加入 PATH（winget 里搜 ffmpeg 也有），"
                                         "装好后重启本程序");
        Core::Logger::warning(hint.toStdString());
        return std::unexpected(Core::Error{Core::ErrorCode::DependencyMissing, hint.toStdString()});
    }

} // namespace GyanisScribe::Record
