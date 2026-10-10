#include "Record/RecordingHud.h"

#include "Ui/DesignTokens.h"

#include "Core/Text.h"
#include "Ui/IconProvider.h"

#include <QFont>
#include <QHBoxLayout>
#include <QLabel>
#include <QMouseEvent>
#include <QPushButton>
#include <QVBoxLayout>

#include <algorithm>
#include <cmath>

namespace
{
    using GyanisScribe::Record::RecordingState;

    /// 一分钟的秒数
    constexpr int kSecondsPerMinute = 60;

    /// 一小时的秒数
    constexpr int kSecondsPerHour = 3600;

    /// 倒计时大字的字号，逻辑点：需求写的是「居中大字」，小到要凑近看就失去了意义
    constexpr int kCountdownFontPointSize = 34;

    /// 贴边时认为「已经靠边」的容差，逻辑像素
    constexpr int kEdgeSnapTolerance = 24;
} // namespace

namespace GyanisScribe::Record
{
    QString formatDuration(const int seconds)
    {
        const auto total  = std::max(0, seconds);
        const auto hour   = total / kSecondsPerHour;
        const auto minute = (total % kSecondsPerHour) / kSecondsPerMinute;
        const auto second = total % kSecondsPerMinute;

        if (hour > 0)
        {
            return QString::asprintf("%d:%02d:%02d", hour, minute, second);
        }
        return QString::asprintf("%02d:%02d", minute, second);
    }

    std::optional<QString> countdownTextFor(const int remainingSeconds)
    {
        if (remainingSeconds <= 0)
        {
            return {};
        }
        return QString::number(remainingSeconds);
    }

    QString hudStatusText(const RecordingState state, const int recordedSeconds)
    {
        switch (state)
        {
            case RecordingState::Idle:
                return GyanisScribe::Core::uiText("未在录制");
            case RecordingState::Countdown:
                return GyanisScribe::Core::uiText("准备开始");
            case RecordingState::Recording:
                return GyanisScribe::Core::uiText("录制中");
            case RecordingState::Paused:
                return GyanisScribe::Core::uiText("已暂停（时长不计）");
            case RecordingState::Done:
                return GyanisScribe::Core::uiText("已录 %1").arg(formatDuration(recordedSeconds));
            case RecordingState::Failed:
                return GyanisScribe::Core::uiText("已中止");
        }
        return GyanisScribe::Core::uiText("未在录制");
    }

    QRect snapHudRect(const QRect &draggedRect, const QRect &available)
    {
        if (available.isEmpty() || draggedRect.isEmpty())
        {
            return draggedRect;
        }

        // 先把整块夹进可用区：拖出屏幕的控制条会让用户再也停不下录制
        const auto maxX = std::max(available.left(), available.right() - draggedRect.width() + 1);
        const auto maxY = std::max(available.top(), available.bottom() - draggedRect.height() + 1);
        auto       left = std::clamp(draggedRect.left(), available.left(), maxX);
        auto       top  = std::clamp(draggedRect.top(), available.top(), maxY);

        // 挨着哪条边就贴哪条；上下左右都算，中间区域保持用户放的位置
        auto rect = QRect{left, top, draggedRect.width(), draggedRect.height()};
        if (std::abs(rect.left() - available.left()) <= kEdgeSnapTolerance)
        {
            rect.moveLeft(available.left() + kHudEdgeGap);
        } else if (std::abs(available.right() - rect.right() + 1) <= kEdgeSnapTolerance)
        {
            rect.moveLeft(available.right() - rect.width() - kHudEdgeGap + 1);
        }
        if (std::abs(rect.top() - available.top()) <= kEdgeSnapTolerance)
        {
            rect.moveTop(available.top() + kHudEdgeGap);
        } else if (std::abs(available.bottom() - rect.bottom() + 1) <= kEdgeSnapTolerance)
        {
            rect.moveTop(available.bottom() - rect.height() - kHudEdgeGap + 1);
        }
        return rect;
    }

    RecordingHud::RecordingHud(const Ui::IconProvider &icons, const QRect &screenAvailable, QWidget *parent) :
        QWidget(parent), m_icons(icons), m_available(screenAvailable.isEmpty() ? QRect{0, 0, 1920, 1080} : screenAvailable)
    {
        // 工具窗：不进任务栏、不抢焦点，录制区里出现一个任务栏按钮会污染成品
        setWindowTitle(GyanisScribe::Core::uiText("录屏控制条"));
        setAccessibleName(GyanisScribe::Core::uiText("录屏控制条"));
        setWindowFlags(Qt::Tool | Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint);
        // 这一圈卡片底由样式表的 QWidget#hudRoot 给：无边框悬浮窗不铺底，字就直接压在桌面上
        setObjectName(QStringLiteral("hudRoot"));
        setAttribute(Qt::WA_StyledBackground, true);

        auto *root = new QVBoxLayout{this};
        root->setContentsMargins(GyanisScribe::Ui::kSpacingSmall, GyanisScribe::Ui::kSpacingTiny, GyanisScribe::Ui::kSpacingSmall, GyanisScribe::Ui::kSpacingTiny);
        root->setSpacing(GyanisScribe::Ui::kSpacingTiny);

        auto *line = new QHBoxLayout;
        line->setSpacing(GyanisScribe::Ui::kSpacingSmall);
        root->addLayout(line);

        auto *dot = new QLabel{QStringLiteral("●"), this};
        dot->setObjectName(QStringLiteral("recordingDot"));
        dot->setAccessibleName(GyanisScribe::Core::uiText("录制指示"));
        line->addWidget(dot);

        m_status = new QLabel{hudStatusText(RecordingState::Idle, 0), this};
        m_status->setAccessibleName(GyanisScribe::Core::uiText("录制状态"));
        GyanisScribe::Ui::applyTextRole(*m_status, GyanisScribe::Ui::TextRole::Body);
        line->addWidget(m_status);

        m_elapsed = new QLabel{formatDuration(0), this};
        m_elapsed->setObjectName(QStringLiteral("metricValue"));
        m_elapsed->setAccessibleName(GyanisScribe::Core::uiText("已录时长"));
        // 等宽数字：一秒一跳时这一列的宽度不能变，否则整条控制条每秒钟抖一次
        GyanisScribe::Ui::applyTextRole(*m_elapsed, GyanisScribe::Ui::TextRole::Metric);
        line->addWidget(m_elapsed);

        // 取消排在最左：它与「停止并保存」之间隔着暂停键，误点一格不会把录好的那段丢掉
        auto *cancelButton = new QPushButton{GyanisScribe::Core::uiText("取消"), this};
        cancelButton->setIcon(m_icons.icon("Close"));
        cancelButton->setToolTip(GyanisScribe::Core::uiText("放弃这一段，不保存成品"));
        cancelButton->setAccessibleName(GyanisScribe::Core::uiText("取消录屏"));
        m_pauseButton = new QPushButton{GyanisScribe::Core::uiText("暂停"), this};
        m_pauseButton->setIcon(m_icons.icon("RecordPause"));
        m_pauseButton->setAccessibleName(GyanisScribe::Core::uiText("暂停"));
        auto *stopButton = new QPushButton{GyanisScribe::Core::uiText("停止"), this};
        stopButton->setIcon(m_icons.icon("RecordStop"));
        stopButton->setToolTip(GyanisScribe::Core::uiText("停止并保存成品"));
        stopButton->setAccessibleName(GyanisScribe::Core::uiText("停止并保存"));
        auto *minimizeButton = new QPushButton{this};
        minimizeButton->setIcon(m_icons.icon("Tray"));
        minimizeButton->setToolTip(GyanisScribe::Core::uiText("最小化到托盘"));
        minimizeButton->setAccessibleName(GyanisScribe::Core::uiText("最小化到托盘"));

        line->addWidget(cancelButton);
        line->addWidget(m_pauseButton);
        line->addWidget(stopButton);
        line->addWidget(minimizeButton);

        m_countdown = new QLabel{this};
        m_countdown->setAccessibleName(GyanisScribe::Core::uiText("倒计时"));
        m_countdown->setAlignment(Qt::AlignCenter);
        m_countdown->hide();
        root->addWidget(m_countdown);

        QObject::connect(m_pauseButton, &QPushButton::clicked, this,
                         [this]
                         {
                             // 暂停与继续共用一个键：发哪个请求看当前状态，看按钮文本会随语言漂移
                             if (m_state == RecordingState::Paused)
                             {
                                 emit resumeRequested();
                                 return;
                             }
                             emit pauseRequested();
                         });
        QObject::connect(cancelButton, &QPushButton::clicked, this, &RecordingHud::cancelRequested);
        QObject::connect(stopButton, &QPushButton::clicked, this, &RecordingHud::stopRequested);
        QObject::connect(minimizeButton, &QPushButton::clicked, this,
                         [this]
                         {
                             emit minimized();
                             hide();
                         });

        const auto size = sizeHint().isValid() ? sizeHint() : QSize{260, 44};
        setGeometry(snapHudRect(QRect{m_available.center().x() - size.width() / 2, m_available.bottom() - size.height() - kHudEdgeGap, size.width(), size.height()}, m_available));
    }

    void RecordingHud::applyState(const RecordingState state, const int countdownRemaining, const int recordedSeconds)
    {
        m_state = state;
        m_status->setText(hudStatusText(state, recordedSeconds));
        m_elapsed->setText(formatDuration(recordedSeconds));

        const auto countdown = countdownTextFor(countdownRemaining);
        if (countdown)
        {
            QFont bigFont = m_countdown->font();
            bigFont.setPointSize(kCountdownFontPointSize);
            m_countdown->setFont(bigFont);
            m_countdown->setText(*countdown);
            m_countdown->show();
            m_countdown->raise();
        } else
        {
            m_countdown->hide();
        }
        reflectPauseAction();
    }

    QString RecordingHud::statusText() const
    {
        return m_status == nullptr ? QString{} : m_status->text();
    }

    QString RecordingHud::elapsedText() const
    {
        return m_elapsed == nullptr ? QString{} : m_elapsed->text();
    }

    bool RecordingHud::isCountdownVisible() const
    {
        return m_countdown != nullptr && m_countdown->isVisibleTo(const_cast<RecordingHud *>(this));
    }

    QString RecordingHud::countdownText() const
    {
        return m_countdown == nullptr ? QString{} : m_countdown->text();
    }

    void RecordingHud::reflectPauseAction()
    {
        // 按钮跟着状态改口：暂停中显示「继续」，否则用户会以为按下去是再暂停一次
        const auto label = m_state == RecordingState::Paused ? GyanisScribe::Core::uiText("继续") : GyanisScribe::Core::uiText("暂停");
        m_pauseButton->setText(label);
        m_pauseButton->setAccessibleName(label);
        m_pauseButton->setIcon(m_icons.icon(m_state == RecordingState::Paused ? "RecordStart" : "RecordPause"));
    }

    void RecordingHud::mousePressEvent(QMouseEvent *event)
    {
        if (event->button() == Qt::LeftButton)
        {
            m_dragOffset = event->position().toPoint();
        }
        QWidget::mousePressEvent(event);
    }

    void RecordingHud::mouseMoveEvent(QMouseEvent *event)
    {
        if ((event->buttons() & Qt::LeftButton) == 0)
        {
            return;
        }
        // 无边框窗没有系统标题栏，拖动只能自己接：位置按全局坐标减掉指针在窗内的偏移
        move(event->globalPosition().toPoint() - m_dragOffset);
    }

    void RecordingHud::mouseReleaseEvent(QMouseEvent *event)
    {
        if (event->button() == Qt::LeftButton)
        {
            setGeometry(snapHudRect(geometry(), m_available));
        }
        QWidget::mouseReleaseEvent(event);
    }

} // namespace GyanisScribe::Record
