#include "Record/FfmpegAudioDevices.h"

#include "Base/Logger.h"
#include "Base/Text.h"
#include "Record/FfmpegCommand.h"

#include <QProcess>
#include <QRegularExpression>
#include <QtGlobal>

#include <format>

namespace
{
    // 一台音频设备行：`[dshow @ 0x…] "名称" (audio)`。前面的地址与方括号各版本都不同，只认收尾这两段
    // 定界符 rx 是必需的：模式里有 )" 这两个相邻字符，不带定界符的 raw string 会在那里提前结束
    const QRegularExpression kAudioDeviceLine{QStringLiteral(R"rx(\]\s*"([^"]+)"\s*\(audio\)\s*$)rx")};
    // 稳定标识行：`[dshow @ 0x…]   Alternative name "@device_cm_{…}\wave_{…}"`，归上一条设备
    const QRegularExpression kAlternativeNameLine{QStringLiteral(R"rx(Alternative name "([^"]+)")rx")};

    /// ffmpeg 的报错很长，取末段就够定位
    QString tailOf(const QString &text, const qsizetype maximum = 240)
    {
        return text.size() <= maximum ? text : text.right(maximum);
    }
} // namespace

namespace GyanisScribe::Record
{
    QVector<AudioInputDevice> parseDshowAudioDevices(const QString &ffmpegOutput)
    {
        QVector<AudioInputDevice> devices;
        // 只按换行符切一行：ffmpeg 在 Windows 上打的是回车加换行，两处都切会把同一台设备列两遍
        for (const auto &rawLine: ffmpegOutput.split(QChar::LineFeed))
        {
            const auto line = rawLine.trimmed();

            if (const auto hit = kAudioDeviceLine.match(line); hit.hasMatch())
            {
                devices.push_back(AudioInputDevice{hit.captured(1), {}});
                continue;
            }
            // 稳定标识必须挂在刚列出的那台设备上：没有前置设备行时这行没有归属，跳过它
            if (!devices.isEmpty())
            {
                if (const auto alias = kAlternativeNameLine.match(line); alias.hasMatch())
                {
                    devices.back().alternativeName = alias.captured(1);
                }
            }
        }
        return devices;
    }

    Base::Expected<QVector<AudioInputDevice>> audioDevicesFromListing(const QString &ffmpegOutput)
    {
        const auto devices = parseDshowAudioDevices(ffmpegOutput);
        if (devices.isEmpty())
        {
            return std::unexpected(Base::Error{
                    .code    = Base::ErrorCode::InternalError,
                    .message = GyanisScribe::Base::uiFormat(
                            GyanisScribe::Base::uiTextUtf8("列不出音频设备：ffmpeg 的输出里没有认得出的设备行（末段：{}）。请更新 ffmpeg 后重试，录屏仍可静音进行"),
                            tailOf(ffmpegOutput).toStdString()),
            });
        }
        return devices;
    }

    FfmpegAudioDeviceEnumerator::FfmpegAudioDeviceEnumerator(QObject *parent) : QObject(parent)
    {
    }

    FfmpegAudioDeviceEnumerator::~FfmpegAudioDeviceEnumerator()
    {
        if (m_process != nullptr && m_process->state() != QProcess::NotRunning)
        {
            m_process->kill();
            m_process->waitForFinished(1000);
        }
    }

    void FfmpegAudioDeviceEnumerator::refresh()
    {
        // 一次只允许在飞一个查询：两个 ffmpeg 同时列设备，后回来的会把先回来的盖掉
        if (m_busy)
        {
            return;
        }

        const auto program = locateFfmpeg();
        if (!program)
        {
            emit failed(program.error());
            return;
        }

        m_process = std::make_unique<QProcess>();
        m_busy    = true;
        // 这份输出就是判据本身，退出码不算：dummy 这个"输入"注定打不开，ffmpeg 一定以非零退出
        m_process->setProcessChannelMode(QProcess::MergedChannels);
        QObject::connect(m_process.get(), &QProcess::finished, this, [this] { handleFinished(); });
        QObject::connect(m_process.get(), &QProcess::errorOccurred, this,
                         [this](QProcess::ProcessError)
                         {
                             m_busy = false;
                             emit failed(GyanisScribe::Base::Error{.code    = GyanisScribe::Base::ErrorCode::InternalError,
                                                                   .message = GyanisScribe::Base::uiTextUtf8("列音频设备的那一步起不动 ffmpeg。请确认它还能运行，或重新安装")});
                         });
        m_process->start(*program, {QStringLiteral("-hide_banner"), QStringLiteral("-loglevel"), QStringLiteral("info"), QStringLiteral("-f"), QStringLiteral("dshow"),
                                    QStringLiteral("-list_devices"), QStringLiteral("true"), QStringLiteral("-i"), QStringLiteral("dummy")});
    }

    void FfmpegAudioDeviceEnumerator::handleFinished()
    {
        m_busy = false;
        if (m_process == nullptr)
        {
            return;
        }

        const auto output = QString::fromUtf8(m_process->readAll());
        // 空清单不当成「这台机没有设备」交回界面：判据在 audioDevicesFromListing 那一条里
        const auto parsed = audioDevicesFromListing(output);
        if (!parsed)
        {
            emit failed(parsed.error());
            return;
        }
        emit updated(*parsed);
    }

} // namespace GyanisScribe::Record
