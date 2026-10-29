#include "Plugins/Recording/FfmpegVideoEncoderEnumerator.h"

#include "Base/Logger.h"
#include "Base/Text.h"
#include "Plugins/Recording/FfmpegCommand.h"
#include "Plugins/Recording/FfmpegVideoEncoders.h"

#include <QProcess>

#include <format>
#include <utility>

namespace
{
    /// 单个候选的探测上限，毫秒：本机实测最慢的一档 1.6 秒（缺硬件运行库的那一类要等它自己失败），
    /// 15 秒是留给磁盘冷启动的余量
    constexpr int kEncoderProbeTimeoutMs = 15000;

    /// 强杀之后等进程真正退出的上限，毫秒：kill 是异步的，不等它退就把 QProcess 销毁会留下孤儿进程
    constexpr int kProbeKillWaitMs = 1000;

    /// 生产用的探测命令：程序取 locateFfmpeg，参数按取样档拼
    GyanisScribe::Base::Expected<GyanisScribe::Plugins::FfmpegVideoEncoderEnumerator::ProbeCommand> defaultProbeCommand(const QString &encoderName)
    {
        const auto program = GyanisScribe::Plugins::locateFfmpeg();
        if (!program)
        {
            return std::unexpected(program.error());
        }
        return GyanisScribe::Plugins::FfmpegVideoEncoderEnumerator::ProbeCommand{
                *program, GyanisScribe::Plugins::videoEncoderProbeArguments(encoderName.toStdString(), 0, GyanisScribe::Plugins::kEncoderProbeWidth,
                                                                            GyanisScribe::Plugins::kEncoderProbeHeight, GyanisScribe::Plugins::kEncoderProbeFrameRate)};
    }
} // namespace

namespace GyanisScribe::Plugins
{

    FfmpegVideoEncoderEnumerator::FfmpegVideoEncoderEnumerator(QObject *parent) :
        FfmpegVideoEncoderEnumerator([](const QString &encoderName) { return defaultProbeCommand(encoderName); }, kEncoderProbeTimeoutMs, parent)
    {
    }

    FfmpegVideoEncoderEnumerator::FfmpegVideoEncoderEnumerator(ProbeCommandBuilder builder, const int probeTimeoutMs, QObject *parent) :
        QObject(parent), m_probeCommandBuilder(std::move(builder)), m_probeTimeoutMs(probeTimeoutMs)
    {
        m_probeTimeout.setSingleShot(true);
        QObject::connect(&m_probeTimeout, &QTimer::timeout, this, &FfmpegVideoEncoderEnumerator::handleProbeTimeout);
    }

    FfmpegVideoEncoderEnumerator::~FfmpegVideoEncoderEnumerator()
    {
        m_probeTimeout.stop();
        if (m_process != nullptr && m_process->state() != QProcess::NotRunning)
        {
            m_process->kill();
            m_process->waitForFinished(kProbeKillWaitMs);
        }
    }

    void FfmpegVideoEncoderEnumerator::refresh()
    {
        // 一轮还没回来时不重开：两串 ffmpeg 同时跑只会互相抢 CPU，两份清单还会互相盖
        if (m_busy)
        {
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

        const auto candidate = m_remaining.constFirst();
        const auto command   = m_probeCommandBuilder(candidate);
        if (!command)
        {
            // 命令都拼不出来（生产实现里这就是 ffmpeg 不见了）：整轮按失败收口，不逐档重试同一个原因
            m_busy = false;
            emit failed(command.error());
            return;
        }

        // 探测参数与真编码同一形状（按取样档），末了输出到 null：判据是「这条命令真跑得通」
        Base::Logger::info(std::format("探编码器 {}（{}x{}@{}）", candidate.toStdString(), kEncoderProbeWidth, kEncoderProbeHeight, kEncoderProbeFrameRate));

        m_process = std::make_unique<QProcess>();
        m_process->setProcessChannelMode(QProcess::MergedChannels);
        QObject::connect(m_process.get(), &QProcess::finished, this, [this] { handleFinished(); });
        QObject::connect(m_process.get(), &QProcess::errorOccurred, this,
                         [this, candidate](QProcess::ProcessError)
                         {
                             // 起不动这一档按失败收口：再探别的候选只会得到同一条找不着程序的错误
                             m_probeTimeout.stop();
                             m_busy = false;
                             emit failed(Base::Error{
                                     .code    = Base::ErrorCode::InternalError,
                                     .message = Base::uiFormat(Base::uiTextUtf8("探测编码器 {} 的那一步起不动 ffmpeg。请确认它还能运行，或重新安装"), candidate.toStdString())});
                         });
        m_process->start(command->program, command->arguments);
        m_probeTimeout.start(m_probeTimeoutMs);
    }

    void FfmpegVideoEncoderEnumerator::handleFinished()
    {
        m_probeTimeout.stop();
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

    void FfmpegVideoEncoderEnumerator::handleProbeTimeout()
    {
        if (!m_busy || m_process == nullptr || m_remaining.isEmpty())
        {
            return;
        }

        const auto candidate = m_remaining.constFirst();
        Base::Logger::warning(std::format("探测编码器 {} 超过 {} 毫秒没返回，已强杀并跳过这一档", candidate.toStdString(), m_probeTimeoutMs));

        // 先断掉 finished 与 errorOccurred 再杀：waitForFinished 会把 finished 发出来，
        // 不先拆线它就会把这一档当成正常收尾再消费一次，清单少探一档
        QObject::disconnect(m_process.get(), nullptr, this, nullptr);
        m_process->kill();
        m_process->waitForFinished(kProbeKillWaitMs);
        m_process.reset();
        m_remaining.removeFirst();
        probeNext();
    }

} // namespace GyanisScribe::Plugins
