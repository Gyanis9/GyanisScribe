#include "Core/Steps/StepSession.h"

#include "Base/Text.h"

#include <QFileInfo>

#include <algorithm>
#include <chrono>
#include <system_error>
#include <utility>

namespace GyanisScribe::Core
{
    namespace
    {
        /// 会话临时目录的保留时长；超过后由启动清理回收
        constexpr auto kSessionRetention = std::chrono::hours(24);

        QString fromPath(const std::filesystem::path &path)
        {
            return QString::fromStdWString(path.wstring());
        }

        std::filesystem::path toPath(const QString &text)
        {
            return std::filesystem::path{text.toStdWString()};
        }

        /**
         * @brief 去掉控制字符并把连续空白折成一个空格
         * @param text 用户或模板给出的说明
         * @return 适合单行显示与导出的文本；首尾空白不留
         * @details 说明会进审核窗单行编辑框、HTML 与 PDF，换行和制表符没有承载位置。
         */
        QString normalizeDescription(const QString &text)
        {
            QString normalized;
            normalized.reserve(text.size());
            bool pendingSpace = false;
            for (const QChar character: text)
            {
                if (character.isSpace() || character.category() == QChar::Other_Control)
                {
                    pendingSpace = !normalized.isEmpty();
                    continue;
                }
                if (pendingSpace)
                {
                    normalized.append(QLatin1Char(' '));
                    pendingSpace = false;
                }
                normalized.append(character);
            }
            return normalized;
        }

        /**
         * @brief 按步骤来源挑自动说明模板
         * @param step 尚未填说明的步骤
         * @return 对应类型的自动说明
         */
        QString automaticDescription(const RecordedStep &step)
        {
            switch (step.kind)
            {
                case StepKind::Click:
                    return clickStepDescription(step.elementName, step.controlType, step.globalLogicalPoint, step.rightButton);
                case StepKind::WindowChange:
                    return windowChangeStepDescription(step.windowTitle, step.processName);
                case StepKind::Manual:
                    return manualStepDescription();
            }
            return QString();
        }

        /// 越界错误的统一文案，带 1 起的序号与当前步数
        Base::Error outOfRangeError(const std::size_t index, const std::size_t count)
        {
            return Base::Error{Base::ErrorCode::InvalidArgument, Base::uiFormat(Base::uiTextUtf8("步骤序号 %1 越界，当前只有 %2 步"), index + 1, count)};
        }
    } // namespace

    Base::Expected<void> StepSession::begin(const std::filesystem::path &tempDirectory)
    {
        m_steps.clear();
        m_tempDirectory.clear();

        if (tempDirectory.empty())
        {
            return std::unexpected(Base::Error{Base::ErrorCode::InvalidArgument, Base::uiTextUtf8("步骤临时目录为空。请先准备可写的临时目录再开始记录")});
        }

        std::error_code error;
        std::filesystem::create_directories(tempDirectory, error);
        if (error)
        {
            return std::unexpected(Base::Error{Base::ErrorCode::FileWriteFailed, Base::uiFormat(Base::uiTextUtf8("无法创建步骤临时目录 %1（%2）。请检查目录权限后重试"),
                                                                                                fromPath(tempDirectory).toStdString(), error.message())});
        }
        m_tempDirectory = tempDirectory;
        return {};
    }

    Base::Expected<void> StepSession::append(RecordedStep step)
    {
        if (m_steps.size() >= static_cast<std::size_t>(kMaximumSteps))
        {
            return std::unexpected(Base::Error{Base::ErrorCode::Busy, Base::uiFormat(Base::uiTextUtf8("已经记满 %1 步，先导出或放弃这次记录"), kMaximumSteps)});
        }

        if (step.description.trimmed().isEmpty())
        {
            step.description = automaticDescription(step);
        }
        step.description = normalizeDescription(step.description).left(kMaximumStepDescriptionLength);

        m_steps.push_back(std::move(step));
        bumpIndices();
        return {};
    }

    Base::Expected<void> StepSession::attachImage(const std::size_t index, const QImage &image)
    {
        if (index >= m_steps.size())
        {
            return std::unexpected(outOfRangeError(index, m_steps.size()));
        }
        if (m_tempDirectory.empty())
        {
            const QString reason        = Base::uiText("还没有建立步骤临时目录，截图无处可写。请先开始记录再抓屏");
            m_steps[index].captureError = reason;
            return std::unexpected(Base::Error{Base::ErrorCode::FileWriteFailed, reason.toStdString()});
        }
        if (image.isNull())
        {
            const QString reason        = Base::uiText("这一张截图是空的，无法保存。请重试抓屏");
            m_steps[index].captureError = reason;
            return std::unexpected(Base::Error{Base::ErrorCode::FileWriteFailed, reason.toStdString()});
        }

        // 文件名以显示序号为起点；若该编号的图仍被别的步骤引用，就顺延到下一个空号，绝不覆盖别人的图。
        int                   number = m_steps[index].index > 0 ? m_steps[index].index : static_cast<int>(index) + 1;
        std::filesystem::path target;
        for (;; ++number)
        {
            target = m_tempDirectory / toPath(QStringLiteral("step-%1.png").arg(number, 3, 10, QLatin1Char('0')));
            if (!isImagePathInUse(target, index))
            {
                break;
            }
        }

        const QString targetText = fromPath(target);
        if (!image.save(targetText, "PNG"))
        {
            const QString reason        = Base::uiText("截图写入失败：%1。请检查临时目录权限与磁盘空间").arg(targetText);
            m_steps[index].captureError = reason;
            return std::unexpected(Base::Error{Base::ErrorCode::FileWriteFailed, reason.toStdString()});
        }

        m_steps[index].imagePath = targetText;
        m_steps[index].thumbnail = image.scaledToWidth(std::min(kStepThumbnailWidth, image.width()), Qt::SmoothTransformation);
        m_steps[index].captureError.clear();
        return {};
    }

    bool StepSession::remove(const std::size_t index)
    {
        if (index >= m_steps.size())
        {
            return false;
        }

        const QString imagePath = m_steps[index].imagePath;
        if (!imagePath.isEmpty())
        {
            std::error_code error;
            std::filesystem::remove(toPath(imagePath), error);
        }
        m_steps.erase(m_steps.begin() + static_cast<std::ptrdiff_t>(index));
        bumpIndices();
        return true;
    }

    bool StepSession::moveUp(const std::size_t index)
    {
        if (index == 0 || index >= m_steps.size())
        {
            return false;
        }
        std::swap(m_steps[index - 1], m_steps[index]);
        bumpIndices();
        return true;
    }

    bool StepSession::moveDown(const std::size_t index)
    {
        if (index >= m_steps.size() || index + 1 >= m_steps.size())
        {
            return false;
        }
        std::swap(m_steps[index], m_steps[index + 1]);
        bumpIndices();
        return true;
    }

    Base::Expected<void> StepSession::setDescription(const std::size_t index, const QString &text)
    {
        if (index >= m_steps.size())
        {
            return std::unexpected(outOfRangeError(index, m_steps.size()));
        }
        m_steps[index].description = normalizeDescription(text).left(kMaximumStepDescriptionLength);
        return {};
    }

    std::size_t StepSession::count() const noexcept
    {
        return m_steps.size();
    }

    const std::vector<RecordedStep> &StepSession::steps() const noexcept
    {
        return m_steps;
    }

    const std::filesystem::path &StepSession::tempDirectory() const noexcept
    {
        return m_tempDirectory;
    }

    void StepSession::cleanup()
    {
        if (!m_tempDirectory.empty())
        {
            std::error_code error;
            std::filesystem::remove_all(m_tempDirectory, error);
            m_tempDirectory.clear();
        }
        m_steps.clear();
    }

    bool StepSession::shouldMerge(const RecordedStep &previous, const RecordedStep &next) const noexcept
    {
        if (previous.kind != StepKind::Click || next.kind != StepKind::Click)
        {
            return false;
        }
        if (!previous.at.isValid() || !next.at.isValid())
        {
            return false;
        }

        const qint64 elapsed = previous.at.msecsTo(next.at);
        if (elapsed < 0 || elapsed > kClickMergeWindowMs)
        {
            return false;
        }
        if ((next.globalLogicalPoint - previous.globalLogicalPoint).manhattanLength() > kClickMergeDistancePixels)
        {
            return false;
        }
        return previous.elementName == next.elementName && previous.windowTitle == next.windowTitle;
    }

    void StepSession::cleanupOldSessions(const std::filesystem::path &root, const QDateTime &now)
    {
        if (root.empty() || !now.isValid())
        {
            return;
        }

        std::error_code                     error;
        std::filesystem::directory_iterator iterator{root, error};
        if (error)
        {
            return;
        }

        const std::chrono::system_clock::time_point nowSystem{std::chrono::milliseconds{now.toMSecsSinceEpoch()}};
        const auto                                  nowFile = std::chrono::clock_cast<std::chrono::file_clock>(nowSystem);

        const std::filesystem::directory_iterator end;
        std::vector<std::filesystem::path>        expired;
        while (iterator != end)
        {
            std::error_code entryError;
            if (iterator->is_directory(entryError) && !entryError)
            {
                const auto lastWrite = std::filesystem::last_write_time(iterator->path(), entryError);
                if (!entryError && nowFile - lastWrite > kSessionRetention)
                {
                    expired.push_back(iterator->path());
                }
            }
            iterator.increment(error);
            if (error)
            {
                break;
            }
        }

        for (const std::filesystem::path &path: expired)
        {
            std::error_code removeError;
            std::filesystem::remove_all(path, removeError);
        }
    }

    void StepSession::bumpIndices() noexcept
    {
        int displayIndex = 1;
        for (RecordedStep &step: m_steps)
        {
            step.index = displayIndex;
            ++displayIndex;
        }
    }

    bool StepSession::isImagePathInUse(const std::filesystem::path &path, const std::size_t exceptIndex) const
    {
        const QString candidate = QString::fromStdWString(path.filename().wstring());
        for (std::size_t index = 0; index < m_steps.size(); ++index)
        {
            if (index == exceptIndex || m_steps[index].imagePath.isEmpty())
            {
                continue;
            }
            if (QFileInfo(m_steps[index].imagePath).fileName() == candidate)
            {
                return true;
            }
        }
        return false;
    }

} // namespace GyanisScribe::Core
