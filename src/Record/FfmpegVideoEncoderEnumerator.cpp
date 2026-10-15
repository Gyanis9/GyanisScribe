#include "Record/FfmpegVideoEncoderEnumerator.h"

#include "Base/Logger.h"
#include "Base/Text.h"
#include "Record/FfmpegCommand.h"
#include "Record/FfmpegVideoEncoders.h"

#include <QProcess>

#include <format>

namespace GyanisScribe::Record
{

    FfmpegVideoEncoderEnumerator::FfmpegVideoEncoderEnumerator(QObject *parent) : QObject(parent)
    {
    }

    FfmpegVideoEncoderEnumerator::~FfmpegVideoEncoderEnumerator()
    {
        if (m_process != nullptr && m_process->state() != QProcess::NotRunning)
        {
            m_process->kill();
            m_process->waitForFinished(1000);
        }
    }

    void FfmpegVideoEncoderEnumerator::refresh()
    {
        // 一轮还没回来时不重开：两串 ffmpeg 同时跑只会互相抢 CPU，两份清单还会互相盖
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

        m_remaining.clear();
        m_usable.clear();
        for (const auto encoder: kPreferredVideoEncoders)
        {
            m_remaining << QString::fromUtf8(encoder.data(), static_cast<qsizetype>(encoder.size()));
        }
        m_busy = true;
        probeNext();
    }

    void FfmpegVideoEncoderEnumerator::probeNext()
    {
        if (m_remaining.isEmpty())
        {
            m_busy = false;
            if (m_usable.isEmpty())
            {
                emit failed(Base::Error{
                        .code    = Base::ErrorCode::DependencyMissing,
                        .message = Base::uiTextUtf8("你这份 ffmpeg 里一个候选编码器都开不起来（逐个真探过）。请换装一个带 libx264 的完整 ffmpeg 构建，"
                                                    "录屏仍可进行——编码时会再试一遍，全不行才报错"),
                });
                return;
            }
            emit updated(m_usable);
            return;
        }

        const auto program = locateFfmpeg();
        if (!program)
        {
            // 走到这里说明探测器建到一半 ffmpeg 不见了：按失败收口，已探过的结果不交（半份清单会被当成完整清单用）
            m_busy = false;
            emit failed(program.error());
            return;
        }

        const auto candidate = m_remaining.constFirst();
        // 探测参数与真编码同一形状（按取样档），末了输出到 null：判据是「这条命令真跑得通」
        const auto arguments = videoEncoderProbeArguments(candidate.toStdString(), 0, kEncoderProbeWidth, kEncoderProbeHeight, kEncoderProbeFrameRate);
        Base::Logger::info(std::format("探编码器 {}（{}x{}@{}）", candidate.toStdString(), kEncoderProbeWidth, kEncoderProbeHeight, kEncoderProbeFrameRate));

        m_process = std::make_unique<QProcess>();
        m_process->setProcessChannelMode(QProcess::MergedChannels);
        QObject::connect(m_process.get(), &QProcess::finished, this, [this] { handleFinished(); });
        QObject::connect(m_process.get(), &QProcess::errorOccurred, this,
                         [this, candidate](QProcess::ProcessError)
                         {
                             m_busy = false;
                             emit failed(Base::Error{
                                     .code    = Base::ErrorCode::InternalError,
                                     .message = Base::uiFormat(Base::uiTextUtf8("探测编码器 {} 的那一步起不动 ffmpeg。请确认它还能运行，或重新安装"), candidate.toStdString())});
                         });
        m_process->start(*program, arguments);
    }

    void FfmpegVideoEncoderEnumerator::handleFinished()
    {
        if (!m_busy)
        {
            // 起不动那一条已经按失败收过口：别再消费一个候选，把清单探歪
            return;
        }
        const auto candidate = m_remaining.takeFirst();
        if (m_process != nullptr && m_process->exitStatus() == QProcess::NormalExit && m_process->exitCode() == 0)
        {
            m_usable << candidate;
        }
        m_process.reset();
        probeNext();
    }

} // namespace GyanisScribe::Record
