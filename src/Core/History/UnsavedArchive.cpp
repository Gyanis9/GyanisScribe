#include "Core/History/UnsavedArchive.h"

#include "Base/Logger.h"
#include "Base/Paths.h"
#include "Base/Text.h"

#include <algorithm>
#include <filesystem>
#include <format>
#include <utility>
#include <vector>

namespace GyanisScribe::Core
{
    namespace
    {
        /// 这个目录只装本功能写下的 PNG：手放进去的别的文件不该被按张数裁掉
        bool isArchivedCapture(const std::filesystem::directory_entry &entry)
        {
            std::error_code ignored;
            return entry.is_regular_file(ignored) && entry.path().extension() == std::filesystem::path(L".png");
        }

        /**
         * @brief 裁到给定上限：按写入时刻从旧到新删，留下最近的 capacity 张
         * @details 读不出写入时刻的那一份直接让位（不删也不计）：猜错顺序删掉的可能是用户正要回看的那张。
         */
        void pruneToCapacity(const std::filesystem::path &directory, const std::size_t capacity)
        {
            std::vector<std::pair<std::filesystem::file_time_type, std::filesystem::path>> archived;
            std::error_code                                                                iteration;
            for (const auto &entry: std::filesystem::directory_iterator(directory, std::filesystem::directory_options::skip_permission_denied, iteration))
            {
                if (!isArchivedCapture(entry))
                {
                    continue;
                }
                std::error_code stamped;
                const auto      moment = std::filesystem::last_write_time(entry.path(), stamped);
                if (stamped)
                {
                    continue;
                }
                archived.emplace_back(moment, entry.path());
            }

            if (archived.size() <= capacity)
            {
                return;
            }
            std::ranges::sort(archived);
            const auto oldest = archived.size() - capacity;
            for (std::size_t index = 0; index < oldest; ++index)
            {
                std::error_code removed;
                std::filesystem::remove(archived[index].second, removed);
                if (removed)
                {
                    // 删不掉只让目录比上限大一张，不值得把已经写好的这一份也报成失败
                    Base::Logger::warning(std::format("未保存截图的旧份删不掉：{}", Base::Paths::toDisplayText(archived[index].second).toStdString()));
                }
            }
        }
    } // namespace

    Base::Expected<std::filesystem::path> archiveUnsavedCapture(const QImage &image, const QDateTime &moment, const std::filesystem::path &directory, const std::size_t capacity)
    {
        if (image.isNull())
        {
            // 空图写出去是一张 0×0 的 PNG，历史记录会指着它说「有这么一张」——宁可当场报错
            return std::unexpected(Base::Error{
                    .code    = Base::ErrorCode::InvalidArgument,
                    .message = GyanisScribe::Base::uiTextUtf8("这张截图是空的，没东西可留进历史。请重新截一次"),
            });
        }

        if (const auto ensured = Base::Paths::ensureDirectory(directory); !ensured)
        {
            return std::unexpected(ensured.error());
        }

        auto filePath = Base::Paths::uniqueFilePath(directory, Base::Paths::imageFileName(moment, "png"));
        if (!image.save(Base::Paths::toDisplayText(filePath), "PNG"))
        {
            return std::unexpected(Base::Error{
                    .code    = Base::ErrorCode::FileWriteFailed,
                    .message = GyanisScribe::Base::uiFormat(GyanisScribe::Base::uiTextUtf8("「{}」写不进去。请检查这个目录能不能写，或关掉「未保存的截图也进历史」这一档"),
                                                            Base::Paths::toDisplayText(filePath).toStdString()),
            });
        }

        pruneToCapacity(directory, capacity);
        return filePath;
    }

} // namespace GyanisScribe::Core
