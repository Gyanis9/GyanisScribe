#include "App/HistoryCoordinator.h"

#include "Base/Logger.h"
#include "Base/Paths.h"
#include "Base/Text.h"
#include "Core/History/HistoryRules.h"
#include "Core/History/HistoryStore.h"
#include "Core/Image/ImageLoading.h"
#include "Core/Settings/ISettingsStore.h"

#include <QDateTime>
#include <QDesktopServices>
#include <QUrl>

#include <format>

namespace GyanisScribe::App
{
    HistoryCoordinator::HistoryCoordinator(GyanisScribe::Core::HistoryStore &history, GyanisScribe::Core::ISettingsStore &settings, QObject *parent) :
        QObject(parent), m_history(history), m_settings(settings)
    {
    }

    void HistoryCoordinator::record(const std::filesystem::path &filePath, const GyanisScribe::Core::HistoryKind kind, const QSize &size,
                                    const GyanisScribe::Core::CaptureSourceInfo &source)
    {
        const auto entry = GyanisScribe::Core::HistoryEntry{
                .moment   = QDateTime::currentDateTime(),
                .kind     = kind,
                .filePath = filePath,
                .size     = size,
        };
        if (auto stored = m_history.append(entry, GyanisScribe::Core::policyFromSettings(m_settings), QDateTime::currentDateTime()); !stored)
        {
            // 清单写不进去不该让「保存成功」变成假话：成品已经在盘上，只是下次打开找不到它
            GyanisScribe::Base::Logger::warning(std::format("成品没进历史清单：{}", stored.error().message));
            emit messageForUser(QString::fromUtf8(GyanisScribe::Base::uiFormat(GyanisScribe::Base::uiTextUtf8("输出文件已保存，但未写入历史记录：{}"), stored.error().message).c_str()),
                                GyanisScribe::Gui::ToastKind::Danger);
            return;
        }
        // 只有真的写进清单才报：记忆库按这一声收条目，失败路径报了就会索引一条不存在的成品
        emit recorded(filePath, kind, size, source);
        emit changed();
    }

    void HistoryCoordinator::remove(const std::filesystem::path &filePath)
    {
        if (auto removed = m_history.remove(filePath); !removed)
        {
            emit messageForUser(QString::fromUtf8(GyanisScribe::Base::uiFormat(GyanisScribe::Base::uiTextUtf8("无法删除该条历史记录：{}"), removed.error().message).c_str()),
                                GyanisScribe::Gui::ToastKind::Danger);
            return;
        }
        // 删记录不删文件：这句要让用户看见，否则他会以为图也被清掉了
        emit messageForUser(GyanisScribe::Base::uiText("已从历史记录中移除，输出文件仍保留在 ") + GyanisScribe::Base::Paths::toDisplayText(filePath),
                            GyanisScribe::Gui::ToastKind::Success);
        // 只在这一条真的从清单里消失之后报：记忆库按路径删行，失败路径不许删
        emit removed(filePath);
        emit changed();
    }

    void HistoryCoordinator::clear()
    {
        if (auto cleared = m_history.clear(); !cleared)
        {
            emit messageForUser(QString::fromUtf8(GyanisScribe::Base::uiFormat(GyanisScribe::Base::uiTextUtf8("无法清空历史记录：{}"), cleared.error().message).c_str()),
                                GyanisScribe::Gui::ToastKind::Danger);
            return;
        }
        // 与单条删除同一条口径：动的是清单，不是用户磁盘上的成品
        emit messageForUser(GyanisScribe::Base::uiText("历史记录已清空，输出文件仍保留在原目录中"), GyanisScribe::Gui::ToastKind::Success);
        // 记忆库据此清掉全部截图来源的行；成品文件同样不在这里碰
        emit cleared();
        emit changed();
    }

    void HistoryCoordinator::openContainingFolder(const std::filesystem::path &filePath)
    {
        std::error_code ignored;
        const auto      directory = filePath.parent_path();
        if (directory.empty() || !std::filesystem::exists(directory, ignored))
        {
            emit messageForUser(QString::fromUtf8(GyanisScribe::Base::uiFormat(GyanisScribe::Base::uiTextUtf8("无法打开所在目录：{} 已不存在，输出文件可能已被移动或删除"),
                                                                               GyanisScribe::Base::Paths::toDisplayText(directory).toStdString())
                                                          .c_str()),
                                GyanisScribe::Gui::ToastKind::Danger);
            return;
        }
        if (!QDesktopServices::openUrl(QUrl::fromLocalFile(QString::fromStdWString(directory.wstring()))))
        {
            emit messageForUser(QString::fromUtf8(GyanisScribe::Base::uiFormat(GyanisScribe::Base::uiTextUtf8("无法打开目录 {}。请前往设置中的截图输出目录查找"),
                                                                               GyanisScribe::Base::Paths::toDisplayText(directory).toStdString())
                                                          .c_str()),
                                GyanisScribe::Gui::ToastKind::Danger);
            return;
        }
        GyanisScribe::Base::Logger::info(std::format("已打开历史成品所在目录：{}", GyanisScribe::Base::Paths::toDisplayText(directory).toStdString()));
    }

    void HistoryCoordinator::requestPin(const std::filesystem::path &filePath)
    {
        auto image = GyanisScribe::Core::loadImageFile(filePath);
        if (!image)
        {
            emit messageForUser(QString::fromUtf8(image.error().message.c_str()), GyanisScribe::Gui::ToastKind::Danger);
            return;
        }
        // 盘上的原文件一并交过去：动图（GIF/APNG）贴上去要接着播，而不是停在首帧
        emit pinRequested(*image, GyanisScribe::Base::Paths::toDisplayText(filePath));
    }

} // namespace GyanisScribe::App
