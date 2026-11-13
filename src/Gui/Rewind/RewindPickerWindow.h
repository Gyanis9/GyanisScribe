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

#include <optional>
#include <utility>

class QCloseEvent;
class QKeyEvent;
class QLabel;
class QListWidget;
class QPushButton;

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
        /// 列表项数据里的「在所选区间内」角色
        static constexpr int kRangeRole = kCandidateRole + 1;
        /// 列表项数据里的抓帧时刻（毫秒时间戳）
        static constexpr int kCapturedAtMsRole = Qt::UserRole + 3;

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

        /**
         * @brief 取当前标记区间的起点
         * @return 两端都标过时给交换后的较小样本号；只标了起点时给该起点；没标时为空
         */
        [[nodiscard]] std::optional<quint64> rangeStartForTesting() const;
        /**
         * @brief 取当前标记区间的终点
         * @return 两端都标过时给交换后的较大样本号；只标了终点时给该终点；没标时为空
         */
        [[nodiscard]] std::optional<quint64> rangeEndForTesting() const;

        /**
         * @brief 切换导出中外观
         * @param exporting 是否有一次片段导出在跑
         * @details 导出中按钮换成「正在导出…」并禁用：再点一次最多只换来一句提示，不如当场拦住。
         *          结束时按当前标记恢复按钮；`[`/`]` 在导出中仍可改标记，下一次导出按新区间走。
         */
        void setExporting(bool exporting);

        /// 当前是否处于导出中，供用例判按钮态与协调器的同步
        [[nodiscard]] bool isExportingForTesting() const noexcept;

    signals:
        /// 用户确认某一帧
        void frameChosen(quint64 id);
        /// 用户取消
        void dismissed();
        /// 用户要导出当前区间
        void clipRequested(quint64 fromId, quint64 toId);

    protected:
        /**
         * @brief 键盘操作：左右与 Home/End 移动光标，[ 与 ] 标起终点，回车确认，Esc 取消
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
        /// 把光标那一帧标成起点或终点，并刷新范围外观
        void markRangeBoundary(bool start);
        /// 按 m_rangeStart/m_rangeEnd 刷新区间外观、读数与按钮可用性
        void refreshRange();
        /// 区间内样本号的首尾（已纠正顺序）；不完整时为空
        [[nodiscard]] std::optional<std::pair<quint64, quint64>> validRange() const;

        QListWidget *m_list       = nullptr; ///< 胶片条
        QLabel      *m_status     = nullptr; ///< 暂停状态
        QLabel      *m_rangeLabel = nullptr; ///< 「已选 N 帧 · x.x 秒」
        QPushButton *m_exportClip = nullptr; ///< 导出片段…

        std::optional<quint64> m_rangeStart; ///< 起点样本号
        std::optional<quint64> m_rangeEnd;   ///< 终点样本号

        bool m_active    = false; ///< 是否处于一次展示中，保证取消只发一次
        bool m_exporting = false; ///< 是否有片段导出在跑，决定导出按钮的文案与可用性
    };
} // namespace GyanisScribe::Gui
