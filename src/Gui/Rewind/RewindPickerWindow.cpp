#include "Gui/Rewind/RewindPickerWindow.h"

#include "Base/Text.h"

#include <QIcon>
#include <QKeyEvent>
#include <QLabel>
#include <QListView>
#include <QListWidget>
#include <QPixmap>
#include <QSize>
#include <QVariant>
#include <QVBoxLayout>

#include <algorithm>
#include <cstddef>

namespace GyanisScribe::Gui
{
    RewindPickerWindow::RewindPickerWindow(QWidget *parent) : QWidget(parent, Qt::Tool | Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint)
    {
        setObjectName(QStringLiteral("RewindPickerWindow"));
        // 窗口自己收键盘：默认 NoFocus 会让 showFrames 末尾的 setFocus 落空，
        // 左右键与 Home/End 也就无法由本窗统一处理
        setFocusPolicy(Qt::StrongFocus);
        auto *layout = new QVBoxLayout{this};
        layout->setContentsMargins(10, 8, 10, 8);
        layout->setSpacing(6);

        m_status = new QLabel{this};
        m_status->setObjectName(QStringLiteral("rewindStatus"));
        layout->addWidget(m_status);

        m_list = new QListWidget{this};
        m_list->setObjectName(QStringLiteral("rewindFilmstrip"));
        m_list->setViewMode(QListView::IconMode);
        m_list->setFlow(QListView::LeftToRight);
        m_list->setWrapping(false);
        m_list->setResizeMode(QListView::Adjust);
        m_list->setIconSize(QSize(160, 90));
        m_list->setSpacing(6);
        m_list->setFixedHeight(130);
        layout->addWidget(m_list);

        auto *hint = new QLabel{GyanisScribe::Base::uiText("← → 选择，Enter 确认，Esc 取消。下一步可拖框选区域，Ctrl+A 选整屏。"), this};
        hint->setObjectName(QStringLiteral("rewindHint"));
        layout->addWidget(hint);

        connect(m_list, &QListWidget::itemDoubleClicked, this, [this](QListWidgetItem *) { chooseCurrent(); });
        hide();
    }

    void RewindPickerWindow::showFrames(const std::vector<Core::RewindFrameView> &frames, const QRect &screenAvailable, const QString &screenName, const bool paused,
                                        const QString &pauseReason)
    {
        m_list->clear();
        for (const auto &frame: frames)
        {
            auto *item = new QListWidgetItem{QIcon(QPixmap::fromImage(frame.thumbnail)), frame.capturedAt.toString(QStringLiteral("HH:mm:ss.zzz")), m_list};
            item->setData(Qt::UserRole, QVariant::fromValue(frame.id));
            if (frame.isCandidate)
            {
                item->setToolTip(GyanisScribe::Base::uiText("候选帧：画面或前台窗口发生变化"));
            }
        }
        if (m_list->count() == 0)
        {
            return;
        }

        int current = m_list->count() - 1;
        for (int index = m_list->count() - 1; index >= 0; --index)
        {
            if (frames[static_cast<std::size_t>(index)].isCandidate)
            {
                current = index;
                break;
            }
        }
        m_list->setCurrentRow(current);
        m_status->setText(paused ? GyanisScribe::Base::uiText("回溯缓冲已暂停：%1").arg(pauseReason) : GyanisScribe::Base::uiText("回溯缓冲已暂停"));
        setWindowTitle(GyanisScribe::Base::uiText("回溯截图 · %1").arg(screenName));

        adjustSize();
        move(screenAvailable.center().x() - width() / 2, screenAvailable.bottom() - height() - 24);
        show();
        activateWindow();
        setFocus();
    }

    void RewindPickerWindow::keyPressEvent(QKeyEvent *event)
    {
        switch (event->key())
        {
            case Qt::Key_Left:
                moveCurrent(-1);
                return;
            case Qt::Key_Right:
                moveCurrent(1);
                return;
            case Qt::Key_Home:
                m_list->setCurrentRow(0);
                return;
            case Qt::Key_End:
                m_list->setCurrentRow(m_list->count() - 1);
                return;
            case Qt::Key_Return:
            case Qt::Key_Enter:
                chooseCurrent();
                return;
            case Qt::Key_Escape:
                hide();
                emit dismissed();
                return;
            default:
                QWidget::keyPressEvent(event);
                return;
        }
    }

    void RewindPickerWindow::chooseCurrent()
    {
        auto *item = m_list->currentItem();
        if (item == nullptr)
        {
            return;
        }
        const auto id = item->data(Qt::UserRole).toULongLong();
        hide();
        emit frameChosen(id);
    }

    void RewindPickerWindow::moveCurrent(const int delta)
    {
        if (m_list->count() == 0)
        {
            return;
        }
        const int next = std::clamp(m_list->currentRow() + delta, 0, m_list->count() - 1);
        m_list->setCurrentRow(next);
    }
} // namespace GyanisScribe::Gui
