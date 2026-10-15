#include "Plugins/Recording/FfmpegCommand.h"

#include "Base/Paths.h"
#include "Base/Logger.h"
#include "Base/Text.h"
#include "Core/Record/RecordingSinks.h"
#include "Plugins/Recording/FfmpegVideoEncoders.h"

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QStandardPaths>

#include <QtGlobal>

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
        return QString::fromLocal8Bit(path.string().c_str()); // 例外：命令行参数要按本地码页交给子进程，这条路不丢字
    }

    /// 帧序列输入的命令行写法：目录 + 定宽补零的模板
    QString sequencePattern(const std::filesystem::path &frameDirectory)
    {
        return fromPath(frameDirectory / std::string{GyanisScribe::Core::kFrameFilePattern});
    }
} // namespace

namespace GyanisScribe::Plugins
{
    QString achievedFrameRateArgument(const int frameCount, const qint64 recordedDurationMs, const int nominalFrameRate)
    {
        // 量不出这一轮录了多久就退回目标帧率：按 0 时长除会得到一个说不出含义的分数
        if (frameCount <= 0 || recordedDurationMs <= 0)
        {
            return QString::number(nominalFrameRate);
        }
        // 分数写法让「帧数 / 时长」原样交给 ffmpeg，不做整数取整
        return QString::number(static_cast<qint64>(frameCount) * 1000) + QLatin1Char('/') + QString::number(recordedDurationMs);
    }

    QStringList mp4Arguments(const Core::RecorderConfig &config, const int frameCount, const qint64 recordedDurationMs, const std::filesystem::path &frameDirectory,
                             const std::filesystem::path &outputFile, const std::filesystem::path &audioWav, const std::string_view videoEncoder)
    {
        QStringList arguments{
                QStringLiteral("-y"),
                QStringLiteral("-framerate"),
                achievedFrameRateArgument(frameCount, recordedDurationMs, config.frameRate),
                QStringLiteral("-i"),
                sequencePattern(frameDirectory),
                // H.264 一类编码器都要求宽高是偶数：截图经常给出奇数高度，不这条缩放滤镜就直接报错退出
                QStringLiteral("-vf"),
                QStringLiteral("scale=trunc(iw/2)*2:trunc(ih/2)*2"),
                QStringLiteral("-c:v"),
                QString::fromUtf8(videoEncoder.data(), static_cast<qsizetype>(videoEncoder.size())),
        };

        // 编码器由调用方探出来再交进来：拼参数这一层不知道这台机器上谁开得起来。
        // 质量那一档也是它家的参数形状（CRF 只有 x264 认，NVENC 说的是 -cq），所以同表取
        arguments << videoEncoderQualityArguments(videoEncoder, config.videoBitrateKbps);

        arguments << QStringLiteral("-pix_fmt")
                  << QStringLiteral("yuv420p")
                  // faststart 把 moov 搬到文件头：不搬的话录完立刻上传会被播放器抱怨「正在下载」
                  << QStringLiteral("-movflags") << QStringLiteral("+faststart");

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

    QStringList gifPaletteArguments(const Core::RecorderConfig &config, const int frameCount, const qint64 recordedDurationMs, const std::filesystem::path &frameDirectory,
                                    const std::filesystem::path &paletteFile)
    {
        // 同一个达成率要出现在两处：输入按它铺开，滤镜按它抽帧，两处不同解就会让调色板与画面错位
        const auto rate = achievedFrameRateArgument(frameCount, recordedDurationMs, config.frameRate);
        return {
                QStringLiteral("-y"),
                QStringLiteral("-framerate"),
                rate,
                QStringLiteral("-i"),
                sequencePattern(frameDirectory),
                // stats_mode=diff 按画面变化取色：截图里大块的静态背景不会把 256 个色位占满
                QStringLiteral("-vf"),
                QStringLiteral("fps=%1,palettegen=stats_mode=diff").arg(rate),
                fromPath(paletteFile),
        };
    }

    QStringList gifEncodeArguments(const Core::RecorderConfig &config, const int frameCount, const qint64 recordedDurationMs, const std::filesystem::path &frameDirectory,
                                   const std::filesystem::path &paletteFile, const std::filesystem::path &outputFile)
    {
        const auto rate = achievedFrameRateArgument(frameCount, recordedDurationMs, config.frameRate);
        return {
                QStringLiteral("-y"),
                QStringLiteral("-framerate"),
                rate,
                QStringLiteral("-i"),
                sequencePattern(frameDirectory),
                QStringLiteral("-i"),
                fromPath(paletteFile),
                QStringLiteral("-lavfi"),
                // 两遍法：第一遍出的调色板作为第二路输入，滤镜图里先把帧率对上再取色
                QStringLiteral("fps=%1[x];[x][1:v]paletteuse=dither=bayer:bayer_scale=3").arg(rate),
                fromPath(outputFile),
        };
    }

    Base::Expected<QStringList> audioCaptureArguments(const QString &microphoneDevice, const QString &systemSoundDevice, const std::filesystem::path &wavFile)
    {
        // 顺序固定：麦克风在前、系统声在后，滤镜图里的 [0:a]/[1:a] 就是按这个次序数出来的
        QStringList devices;
        if (!microphoneDevice.isEmpty())
        {
            devices << microphoneDevice;
        }
        if (!systemSoundDevice.isEmpty())
        {
            devices << systemSoundDevice;
        }
        if (devices.isEmpty())
        {
            return std::unexpected(Base::Error{
                    .code    = Base::ErrorCode::InvalidArgument,
                    .message = GyanisScribe::Base::uiTextUtf8("要取声音却一路设备都没给。请在设置的录屏页选一路，或把取声关掉"),
            });
        }

        QStringList arguments{QStringLiteral("-hide_banner"), QStringLiteral("-loglevel"), QStringLiteral("error"), QStringLiteral("-y")};
        for (const auto &device: devices)
        {
            // 每一路自带 -f dshow：ffmpeg 的 -f 只作用于紧跟其后的那一个 -i
            arguments << QStringLiteral("-f") << QStringLiteral("dshow") << QStringLiteral("-i") << QStringLiteral("audio=%1").arg(device);
        }

        if (devices.size() > 1)
        {
            // dropout_transition=0：一路先结束时不要把混音音量渐变掉，剩下的那路要照常录完
            arguments << QStringLiteral("-filter_complex") << QStringLiteral("[0:a][1:a]amix=inputs=2:duration=longest:dropout_transition=0[a]") << QStringLiteral("-map")
                      << QStringLiteral("[a]");
        }

        // 中间格式定 pcm_s16le 而不是一上来就 aac：取声失败时画面那一半还能独立出片
        arguments << QStringLiteral("-acodec") << QStringLiteral("pcm_s16le") << QStringLiteral("-ar") << QStringLiteral("48000") << QStringLiteral("-ac") << QStringLiteral("2")
                  << fromPath(wavFile);
        return arguments;
    }

    QString bundledFfmpegPath(const QString &applicationDirectory)
    {
        if (applicationDirectory.isEmpty())
        {
            return {};
        }

#ifdef Q_OS_WIN
        constexpr const char *kProgramFileName = "ffmpeg.exe";
#else
        constexpr const char *kProgramFileName = "ffmpeg";
#endif
        const QFileInfo candidate{QDir{applicationDirectory}.filePath(QLatin1String(kProgramFileName))};
        return candidate.isFile() ? candidate.absoluteFilePath() : QString{};
    }

    Base::Expected<QString> locateFfmpeg()
    {
        // 先要随包那份：软件装完即用，不要求用户再装 ffmpeg 或把它加进 PATH
        if (const auto bundled = bundledFfmpegPath(QCoreApplication::applicationDirPath()); !bundled.isEmpty())
        {
            return bundled;
        }

        if (const auto found = QStandardPaths::findExecutable(QStringLiteral("ffmpeg")); !found.isEmpty())
        {
            return found;
        }

        // 两处都没有：安装包本该自带这一件，所以第一句是「重装」，第二句才留给自带构建的用户。
        // 不写具体网址与包名，避免哪天源变了这条提示就成了误导
        // 一条字面量：拆成相邻片段会让翻译目录只收到第一片（ctest 用例 Style.TranslationCatalog 会当场报红）
        const auto hint = GyanisScribe::Base::uiText(
                "没找到 ffmpeg 命令行程序，录屏的编码这一步要用它。它本该随安装包一起装好，请重新安装本程序；也可以自行安装一份并把所在目录加入 PATH，然后重启本程序");
        Base::Logger::warning(hint.toStdString());
        return std::unexpected(Base::Error{Base::ErrorCode::DependencyMissing, hint.toStdString()});
    }

} // namespace GyanisScribe::Plugins
