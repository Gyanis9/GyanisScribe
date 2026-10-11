/**
 * @file RecordingHud.h
 * @brief 录屏控制条：状态、时长、倒计时大字，可拖动、可贴边、可最小化到托盘
 * @author Gyanis
 * @date 2026-10-10
 * @version 0.0.1
 * @copyright Copyright (c) . All rights reserved.
 */

#pragma once

#include "Record/RecordingSession.h"

#include <QObject>
#include <QPoint>
#include <QString>
#include <QWidget>

#include <optional>

class QLabel;
class QPushButton;

namespace GyanisScribe::Ui
{
    class IconProvider;
} // namespace GyanisScribe::Ui

namespace GyanisScribe::Record
{

    /// 贴边与拖动的边距，逻辑像素：紧贴屏幕边会让按钮有一半落在任务栏上
    inline constexpr int kHudEdgeGap = 12;

    /**
     * @brief 把录制秒数排成时钟文本
     * @param seconds 已录秒数，负数按 0 处理
     * @return 不足一小时是 mm:ss，超过则 h:mm:ss
     * @details 秒数直接写会让「9 秒」和「90 秒」看起来一样长，而控制条是用户决定何时停的依据。
     */
    [[nodiscard]] QString formatDuration(int seconds);

    /**
     * @brief 倒计时大字
     * @param remainingSeconds 剩余整秒
     * @return 3/2/1 的文本；不在倒计时里（0 或负数）返回空，控制条据此隐藏那层
     */
    [[nodiscard]] std::optional<QString> countdownTextFor(int remainingSeconds);

    /**
     * @brief 状态那一栏的文本
     * @param state 会话状态
     * @param recordedSeconds 已录秒数
     * @return 中文状态文本
     * @details 暂停与「没在录」必须一眼分得开：暂停还在占着屏幕区域，而 Idle 是彻底结束。
     */
    [[nodiscard]] QString hudStatusText(RecordingState state, int recordedSeconds);

    /**
     * @brief 松手之后把控制条摆到边上
     * @param draggedRect 用户拖到的位置，屏幕坐标
     * @param available 这一屏的可用区
     * @return 夹进可用区并贴到最近那条边的矩形
     * @details 先夹再贴：不夹住的话用户可以把它拖到屏幕外，之后再也没法停止录制。
     */
    [[nodiscard]] QRect snapHudRect(const QRect &draggedRect, const QRect &available);

    /**
     * @brief 录屏控制条
     * @details 控件自己不管录制的推进：按钮只发请求，状态由持有服务的控制器推回来，
     *          这样「按了暂停但服务拒绝」不会显示成一个没发生的状态。
     */
    class RecordingHud : public QWidget
    {
        Q_OBJECT

    public:
        /**
         * @brief 构造控制条
         * @param icons 图标供给器，四个按钮的图标由它取
         * @param screenAvailable 摆放用的可用区，屏幕坐标
         * @param parent Qt 父对象
         */
        RecordingHud(const Ui::IconProvider &icons, const QRect &screenAvailable, QWidget *parent = nullptr);

        /**
         * @brief 推一次状态
         * @param state 会话状态
         * @param countdownRemaining 倒计时剩余整秒，不在倒计时里传 0
         * @param recordedSeconds 已录秒数
         * @details 时长、大字、状态文本与暂停键的字面和可用态都由这一次调用刷新，控制器不必分别管几处。
         */
        void applyState(RecordingState state, int countdownRemaining, int recordedSeconds);

        /**
         * @brief 状态那一栏的文本
         * @return 与 hudStatusText 同源的当前文本
         */
        [[nodiscard]] QString statusText() const;

        /**
         * @brief 时长那一栏的文本
         * @return 时钟格式的已录时长
         */
        [[nodiscard]] QString elapsedText() const;

        /**
         * @brief 倒计时大字是否正在显示
         * @return true 表示这一刻处于倒计时里
         */
        [[nodiscard]] bool isCountdownVisible() const;

        /**
         * @brief 倒计时大字的内容
         * @return 「3」这样的数字文本；没在显示时为空串
         */
        [[nodiscard]] QString countdownText() const;

    signals:
        /// 用户按了暂停
        void pauseRequested();

        /// 用户按了继续
        void resumeRequested();

        /// 用户按了停止并保存
        void stopRequested();

        /// 用户按了取消：这一段不要了，不出成品
        void cancelRequested();

        /// 用户把控制条最小化到托盘
        void minimized();

    protected:
        void mousePressEvent(QMouseEvent *event) override;
        void mouseMoveEvent(QMouseEvent *event) override;
        void mouseReleaseEvent(QMouseEvent *event) override;

    private:
        /// 按当前状态决定暂停键是「暂停」还是「继续」，并只在这两态让它可按
        void reflectPauseAction();

        const Ui::IconProvider &m_icons;                              ///< 图标供给器
        QRect                   m_available;                          ///< 可用区，贴边与夹取都按它算
        QPoint                  m_dragOffset;                         ///< 拖动时指针相对窗口左上角的偏移
        RecordingState          m_state       = RecordingState::Idle; ///< 最近一次推回来的状态
        QLabel                 *m_status      = nullptr;              ///< 状态文本
        QLabel                 *m_elapsed     = nullptr;              ///< 时长文本
        QLabel                 *m_countdown   = nullptr;              ///< 倒计时大字，居中盖住整条
        QPushButton            *m_pauseButton = nullptr;              ///< 暂停/继续那一枚，随状态改口
    };

} // namespace GyanisScribe::Record
