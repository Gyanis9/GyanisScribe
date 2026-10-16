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

    void HistoryCoordinator::record(const std::filesystem::path &filePath, const GyanisScribe::Core::HistoryKind kind, const QSize &size)
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
            emit messageForUser(QString::fromUtf8(GyanisScribe::Base::uiFormat(GyanisScribe::Base::uiTextUtf8("成品已保存，但没进历史清单：{}"), stored.error().message).c_str()),
                                GyanisScribe::Gui::ToastKind::Danger);
            return;
        }
        emit changed();
    }

    void HistoryCoordinator::remove(const std::filesystem::path &filePath)
    {
        if (auto removed = m_history.remove(filePath); !removed)
        {
            emit messageForUser(QString::fromUtf8(GyanisScribe::Base::uiFormat(GyanisScribe::Base::uiTextUtf8("这条历史没删掉：{}"), removed.error().message).c_str()),
                                GyanisScribe::Gui::ToastKind::Danger);
            return;
        }
        // 删记录不删文件：这句要让用户看见，否则他会以为图也被清掉了
        emit messageForUser(GyanisScribe::Base::uiText("已从历史里去掉，成品文件仍在 ") + GyanisScribe::Base::Paths::toDisplayText(filePath),
                            GyanisScribe::Gui::ToastKind::Success);
        emit changed();
    }

    void HistoryCoordinator::clear()
    {
        if (auto cleared = m_history.clear(); !cleared)
        {
            emit messageForUser(QString::fromUtf8(GyanisScribe::Base::uiFormat(GyanisScribe::Base::uiTextUtf8("历史清单没清空：{}"), cleared.error().message).c_str()),
                                GyanisScribe::Gui::ToastKind::Danger);
            return;
        }
        // 与单条删除同一条口径：动的是清单，不是用户磁盘上的成品
        emit messageForUser(GyanisScribe::Base::uiText("历史列表已清空，成品文件仍在各自目录里"), GyanisScribe::Gui::ToastKind::Success);
        emit changed();
    }

    void HistoryCoordinator::openContainingFolder(const std::filesystem::path &filePath)
    {
        std::error_code ignored;
        const auto      directory = filePath.parent_path();
        if (directory.empty() || !std::filesystem::exists(directory, ignored))
        {
            emit messageForUser(QString::fromUtf8(GyanisScribe::Base::uiFormat(GyanisScribe::Base::uiTextUtf8("打不开所在目录：{} 已经不在了。成品可能已被移动或删除"),
                                                                               GyanisScribe::Base::Paths::toDisplayText(directory).toStdString())
                                                          .c_str()),
                                GyanisScribe::Gui::ToastKind::Danger);
            return;
        }
        if (!QDesktopServices::openUrl(QUrl::fromLocalFile(QString::fromStdWString(directory.wstring()))))
        {
            emit messageForUser(QString::fromUtf8(GyanisScribe::Base::uiFormat(GyanisScribe::Base::uiTextUtf8("打不开目录 {}。请自己到设置里那个截图目录去找"),
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
