#include "Gui/Rewind/RewindPickerWindow.h"

#include "Base/Text.h"
#include "Gui/Controls/Button.h"
#include "Gui/Controls/ListWidget.h"

#include <QCloseEvent>
#include <QColor>
#include <QEvent>
#include <QHBoxLayout>
#include <QIcon>
#include <QImage>
#include <QKeyEvent>
#include <QLabel>
#include <QListView>
#include <QListWidget>
#include <QPainter>
#include <QPen>
#include <QPixmap>
#include <QPushButton>
#include <QScrollBar>
#include <QSize>
#include <QVariant>
#include <QVBoxLayout>
#include <QWheelEvent>

#include <algorithm>
#include <cstddef>

namespace
{
    /// 缩略图最大边，对应 spec 4.3 的「最大边 160px」
    constexpr int kThumbnailMaxSide = 160;
    /// 每格在缩略图之外留的横向余量
    constexpr int kCellPadding = 12;
    /// 一屏最多排几格，取 spec 4.3 的 8–12 格上限
    constexpr int kMaxVisibleCells = 12;
    /// 窗口与屏幕可用区边缘留的空隙
    constexpr int kScreenMargin = 24;
    /// 按 12 格定宽时给窗口左右留的余量，避免顶到屏边
    constexpr int kScreenSideReserve = 48;
    /// 候选圆点的半径与边距
    constexpr int kCandidateDotRadius = 5;
    constexpr int kCandidateDotMargin = 4;

    /// 按缩略图宽高比算 160 边长的图标盒，避免把 4:3 或竖屏缩略图压进固定 16:9 框
    QSize thumbnailBox(const QImage &thumbnail)
    {
        if (thumbnail.isNull() || thumbnail.width() <= 0 || thumbnail.height() <= 0)
        {
            return QSize{kThumbnailMaxSide, kThumbnailMaxSide * 9 / 16};
        }
        if (thumbnail.width() >= thumbnail.height())
        {
            return QSize{kThumbnailMaxSide, std::max(1, thumbnail.height() * kThumbnailMaxSide / thumbnail.width())};
        }
        return QSize{std::max(1, thumbnail.width() * kThumbnailMaxSide / thumbnail.height()), kThumbnailMaxSide};
    }

    /// 生成一格缩略图；候选帧在右上角画一枚带白边的绿色小圆点
    QPixmap composeThumbnail(const QImage &thumbnail, const QSize &box, const bool candidate)
    {
        QPixmap pixmap = QPixmap::fromImage(thumbnail.scaled(box, Qt::KeepAspectRatio, Qt::SmoothTransformation));
        if (!candidate)
        {
            return pixmap;
        }

        QPainter painter{&pixmap};
        painter.setRenderHint(QPainter::Antialiasing);
        const QPointF center{static_cast<qreal>(pixmap.width() - kCandidateDotMargin - kCandidateDotRadius), static_cast<qreal>(kCandidateDotMargin + kCandidateDotRadius)};
        painter.setPen(QPen{Qt::white, 1.5});
        painter.setBrush(QColor{0x22, 0xC5, 0x5E});
        painter.drawEllipse(center, kCandidateDotRadius, kCandidateDotRadius);
        return pixmap;
    }
} // namespace

namespace GyanisScribe::Gui
{
    QPoint RewindPickerWindow::positionFor(const QRect &available, const QSize &windowSize)
    {
        if (windowSize.isEmpty())
        {
            return available.center();
        }

        const int maxX = std::max(available.left(), available.right() - windowSize.width() + 1);
        const int x    = std::clamp(available.left() + (available.width() - windowSize.width()) / 2, available.left(), maxX);

        const int topY       = available.top() + kScreenMargin;
        const int bottomY    = available.bottom() - windowSize.height() - kScreenMargin + 1;
        const int preferredY = bottomY >= topY ? bottomY : topY;
        const int maxY       = std::max(available.top(), available.bottom() - windowSize.height() + 1);
        const int y          = std::clamp(preferredY, available.top(), maxY);
        return QPoint{x, y};
    }

    RewindPickerWindow::RewindPickerWindow(QWidget *parent) : QWidget(parent, Qt::Tool | Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint)
    {
        setObjectName(QStringLiteral("RewindPickerWindow"));
        // 窗口自己收键盘：默认 NoFocus 会让 showFrames 末尾的 setFocus 落空，
        // 左右键与 Home/End 也就无法由本窗统一处理
        setFocusPolicy(Qt::StrongFocus);
        auto *layout = new QVBoxLayout{this};
        layout->setContentsMargins(10, 8, 10, 8);
        layout->setSpacing(6);

        auto *header = new QHBoxLayout;
        header->setSpacing(6);
        layout->addLayout(header);

        m_status = new QLabel{this};
        m_status->setObjectName(QStringLiteral("rewindStatus"));
        header->addWidget(m_status);
        header->addStretch(1);

        auto *cancel = new Button{GyanisScribe::Base::uiText("取消"), this};
        cancel->setObjectName(QStringLiteral("rewindCancel"));
        header->addWidget(cancel);

        m_list = new ListWidget{this};
        m_list->setObjectName(QStringLiteral("rewindFilmstrip"));
        m_list->setViewMode(QListView::IconMode);
        m_list->setFlow(QListView::LeftToRight);
        m_list->setWrapping(false);
        m_list->setResizeMode(QListView::Adjust);
        m_list->setSpacing(0);
        m_list->setUniformItemSizes(true);
        // 竖向滚动条会吞掉滚轮，横向滚动改由 eventFilter 映射；这里只保留横向滚动条
        m_list->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        m_list->setHorizontalScrollBarPolicy(Qt::ScrollBarAsNeeded);
        m_list->viewport()->installEventFilter(this);
        layout->addWidget(m_list);

        auto *hint = new QLabel{GyanisScribe::Base::uiText("← → 选择，Enter 确认，Esc 取消。下一步可拖框选区域，Ctrl+A 选整屏。"), this};
        hint->setObjectName(QStringLiteral("rewindHint"));
        layout->addWidget(hint);

        connect(m_list, &QListWidget::itemDoubleClicked, this, [this](QListWidgetItem *) { chooseCurrent(); });
        connect(cancel, &QPushButton::clicked, this, &RewindPickerWindow::dismiss);
        hide();
    }

    void RewindPickerWindow::showFrames(const std::vector<Core::RewindFrameView> &frames, const QRect &screenAvailable, const QString &screenName, const bool paused,
                                        const QString &pauseReason)
    {
        m_list->clear();
        if (frames.empty())
        {
            // 空表没有可选内容：显示中被清空也要先收起，不能留在屏幕上一片空白
            m_active = false;
            hide();
            return;
        }

        QSize iconBox{kThumbnailMaxSide, kThumbnailMaxSide * 9 / 16};
        for (const auto &frame: frames)
        {
            if (!frame.thumbnail.isNull())
            {
                iconBox = thumbnailBox(frame.thumbnail);
                break;
            }
        }
        const int cellWidth  = iconBox.width() + kCellPadding;
        const int cellHeight = iconBox.height() + m_list->fontMetrics().height() + kCellPadding;
        m_list->setIconSize(iconBox);
        m_list->setGridSize(QSize{cellWidth, cellHeight});

        for (const auto &frame: frames)
        {
            auto *item =
                    new QListWidgetItem{QIcon{composeThumbnail(frame.thumbnail, iconBox, frame.isCandidate)}, frame.capturedAt.toString(QStringLiteral("HH:mm:ss.zzz")), m_list};
            item->setData(Qt::UserRole, QVariant::fromValue(frame.id));
            item->setData(kCandidateRole, frame.isCandidate);
            if (frame.isCandidate)
            {
                item->setToolTip(GyanisScribe::Base::uiText("候选帧：画面或前台窗口发生变化"));
            }
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

        // 按 8–12 格定宽：宽屏排 12 格，窄屏让出两侧余量，最少也留一格可操作
        const int listWidth = std::max(cellWidth, std::min(screenAvailable.width() - kScreenSideReserve, kMaxVisibleCells * cellWidth));
        m_list->setFixedWidth(listWidth);
        // 横向滚动条即使按需出现也先留出高度，否则它出现时会把视口压矮而冒出竖向滚动条
        const int scrollbarHeight = m_list->horizontalScrollBar()->sizeHint().height();
        m_list->setFixedHeight(cellHeight + scrollbarHeight + 2 * m_list->frameWidth());

        adjustSize();
        move(positionFor(screenAvailable, size()));
        m_active = true;
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
                dismiss();
                return;
            default:
                QWidget::keyPressEvent(event);
                return;
        }
    }

    void RewindPickerWindow::closeEvent(QCloseEvent *event)
    {
        dismiss();
        event->accept();
    }

    bool RewindPickerWindow::eventFilter(QObject *watched, QEvent *event)
    {
        if (watched == m_list->viewport() && event->type() == QEvent::Wheel)
        {
            auto *wheel = static_cast<QWheelEvent *>(event);
            auto *bar   = m_list->horizontalScrollBar();
            if (bar->maximum() > 0)
            {
                int delta = wheel->angleDelta().y();
                if (delta == 0)
                {
                    delta = wheel->angleDelta().x();
                }
                if (delta != 0)
                {
                    bar->setValue(bar->value() - delta);
                    return true;
                }
            }
        }
        return QWidget::eventFilter(watched, event);
    }

    void RewindPickerWindow::dismiss()
    {
        if (!m_active)
        {
            return;
        }
        m_active = false;
        hide();
        emit dismissed();
    }

    void RewindPickerWindow::chooseCurrent()
    {
        auto *item = m_list->currentItem();
        if (item == nullptr)
        {
            return;
        }
        const auto id = item->data(Qt::UserRole).toULongLong();
        m_active      = false;
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
