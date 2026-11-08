/**
 * @file RewindPickerWindow.h
 * @brief 回溯截图的时间轴选择器
 * @author Gyanis
 * @date 2026-11-08
 * @version 0.0.1
 * @copyright Copyright (c) . All rights reserved.
 */

#pragma once

#include "Core/Rewind/RewindTypes.h"

#include <QRect>
#include <QString>
#include <QWidget>

#include <vector>

class QKeyEvent;
class QLabel;
class QListWidget;

namespace GyanisScribe::Gui
{
    /**
     * @brief 横向胶片条：展示回溯缓冲里的样本
     * @details 只展示缩略图与元数据，不抓帧、不拥有缓冲；确认后把样本号交回协调器。
     */
    class RewindPickerWindow : public QWidget
    {
        Q_OBJECT

    public:
        /**
         * @brief 构造选择器
         * @param parent Qt 父对象
         */
        explicit RewindPickerWindow(QWidget *parent = nullptr);

        /**
         * @brief 展示一批样本
         * @param frames 样本视图，空表时不调用
         * @param screenAvailable 目标屏可用区，用于摆放
         * @param screenName 目标屏名，仅用于窗口标识
         * @param paused 当前是否暂停
         * @param pauseReason 暂停原因，可为空
         */
        void showFrames(const std::vector<Core::RewindFrameView> &frames, const QRect &screenAvailable, const QString &screenName, bool paused, const QString &pauseReason);

    signals:
        /// 用户确认某一帧
        void frameChosen(quint64 id);
        /// 用户取消
        void dismissed();

    protected:
        /**
         * @brief 键盘操作：左右与 Home/End 移动光标，回车确认，Esc 取消
         * @param event 键盘事件
         */
        void keyPressEvent(QKeyEvent *event) override;

    private:
        /// 确认当前光标所在的样本并收起
        void chooseCurrent();
        /// 按 delta 移动光标，夹在首尾之间
        void moveCurrent(int delta);

        QListWidget *m_list   = nullptr; ///< 胶片条
        QLabel      *m_status = nullptr; ///< 暂停状态
    };
} // namespace GyanisScribe::Gui
