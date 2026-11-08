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

#include <QPoint>
#include <QRect>
#include <QSize>
#include <QString>
#include <QWidget>

#include <vector>

class QCloseEvent;
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
        /// 列表项数据里的候选标记角色
        static constexpr int kCandidateRole = Qt::UserRole + 1;

        /**
         * @brief 算选择器该摆在哪儿
         * @param available 目标屏可用区，全局逻辑坐标
         * @param windowSize 选择器自己的尺寸
         * @return 底部居中的左上角；底部空间不足时改顶部居中；最后把坐标夹在可用区内
         */
        [[nodiscard]] static QPoint positionFor(const QRect &available, const QSize &windowSize);

        /**
         * @brief 构造选择器
         * @param parent Qt 父对象
         */
        explicit RewindPickerWindow(QWidget *parent = nullptr);

        /**
         * @brief 展示一批样本
         * @param frames 样本视图；空表时清空并收起，不弹空窗口
         * @param screenAvailable 目标屏可用区，用于定宽与摆放
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

        /**
         * @brief 窗口被关闭时按取消处理，并收起
         * @param event 关闭事件，处理完接受
         * @details 无边框工具窗被 Alt+F4 或系统关闭时走这里，不能只隐藏而不通知协调器。
         */
        void closeEvent(QCloseEvent *event) override;

        /**
         * @brief 把胶片条上的竖向滚轮转成横向滚动
         * @param watched 事件接收者
         * @param event 事件
         * @return 已处理返回 true，其余交回基类
         */
        bool eventFilter(QObject *watched, QEvent *event) override;

    private:
        /// 取消：收起并通知一次，重复调用不再发信号
        void dismiss();
        /// 确认当前光标所在的样本并收起
        void chooseCurrent();
        /// 按 delta 移动光标，夹在首尾之间
        void moveCurrent(int delta);

        QListWidget *m_list   = nullptr; ///< 胶片条
        QLabel      *m_status = nullptr; ///< 暂停状态
        bool         m_active = false;   ///< 是否处于一次展示中，保证取消只发一次
    };
} // namespace GyanisScribe::Gui
