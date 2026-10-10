#include "Record/RecordingSinks.h"

#include "Core/Paths.h"
#include "Core/Text.h"
#include "FfmpegCommand.h"

#include <format>
#include <utility>

namespace GyanisScribe::Record
{
    PngSequenceSink::PngSequenceSink(std::filesystem::path directory) : m_directory(std::move(directory))
    {
    }

    Core::Expected<void> PngSequenceSink::write(const QImage &frame, const int index)
    {
        if (frame.isNull())
        {
            return std::unexpected(Core::Error{Core::ErrorCode::InvalidArgument,
                                               GyanisScribe::Core::uiFormat(GyanisScribe::Core::uiTextUtf8("第 {} 帧是空图，写不了。请检查抓帧那一层有没有拿到画面"), index)});
        }

        const auto target = m_directory / frameFileName(index);
        // QImage::save 按后缀选格式，返回 false 时不给原因；这里补上路径，用户才知道是磁盘满还是没权限
        if (!frame.save(Core::Paths::toDisplayText(target), "PNG"))
        {
            return std::unexpected(Core::Error{Core::ErrorCode::FileWriteFailed,
                                               GyanisScribe::Core::uiFormat(GyanisScribe::Core::uiTextUtf8("录屏帧写入失败：{}。请检查该目录是否可写、磁盘是否已满"),
                                                                            Core::Paths::toDisplayText(target).toStdString())});
        }
        ++m_written;
        return {};
    }

    Core::Expected<void> PngSequenceSink::seal()
    {
        // 一帧都没有就别往下走：空目录交给 ffmpeg 只会得到一句没人看得懂的报错
        if (m_written == 0)
        {
            return std::unexpected(Core::Error{Core::ErrorCode::InternalError, GyanisScribe::Core::uiTextUtf8("这一轮一帧都没有存下来，可能是刚开始就按了停止")});
        }
        return {};
    }

    int PngSequenceSink::writtenFrames() const
    {
        return m_written;
    }

    const std::filesystem::path &PngSequenceSink::directory() const noexcept
    {
        return m_directory;
    }

} // namespace GyanisScribe::Record
