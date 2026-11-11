/**
 * @file StepRecorderHud.h
 * @brief 步骤记录进行中的控制条：当前步数、暂停/继续与停止
 * @author Gyanis
 * @date 2026-11-11
 * @version 0.0.1
 * @copyright Copyright (c) . All rights reserved.
 */

#pragma once

#include <QColor>
#include <QPoint>
#include <QRect>
#include <QSize>
#include <QWidget>

class QLabel;
class QPushButton;

namespace GyanisScribe::Gui
{
    class ThemeManager;

    /**
     * @brief 录制步骤时浮在屏幕顶部中央的控制条
     * @details 工具窗不吃焦点：用户接下来要操作的是别的程序，控制条一抢焦点，第一次点击就落到它身上。
     *          摆位由 positionFor 算，调用方取 preferredSize() 在 show 之前定下几何。
     */
    class StepRecorderHud : public QWidget
    {
        Q_OBJECT

    public:
        /**
         * @brief 构造控制条
         * @param theme 主题来源，取面板、描边、正文与状态圆点的语义色
         * @param parent 父窗口，默认无
         */
        explicit StepRecorderHud(ThemeManager &theme, QWidget *parent = nullptr);

        /**
         * @brief 析构控制条
         */
        ~StepRecorderHud() override;

        /**
         * @brief 更新已记录的步数
         * @param stepCount 当前步数，负数按 0 显示
         */
        void setStepCount(int stepCount);

        /**
         * @brief 切换暂停态
         * @param paused 暂停时红点转灰、暂停按钮改成「继续」
         * @note 本类只显示状态，不改状态：按钮点击发 pauseToggled，由录制方调用本函数回写
         */
        void setPaused(bool paused);

        /**
         * @brief 上屏前用来定几何的建议尺寸
         * @return 高度固定 48，宽度按内容并保一个下限
         */
        [[nodiscard]] QSize preferredSize() const;

        /**
         * @brief 算控制条在可用区顶部居中的落点
         * @param available 当前屏幕去掉任务栏的可用区，全局逻辑坐标
         * @param hudSize 控制条自己的尺寸
         * @return 横坐标按可用区宽度与自身宽度取中，纵坐标贴可用区顶边
         */
        [[nodiscard]] static QPoint positionFor(const QRect &available, const QSize &hudSize);

    signals:
        /**
         * @brief 用户按了暂停/继续：由录制方翻转状态后再调 setPaused
         */
        void pauseToggled();

        /**
         * @brief 用户按了停止：结束录制并进入审核
         */
        void stopRequested();

    protected:
        /**
         * @brief 画控制条的连续曲率面板与边框
         * @details 工具窗透明，圆角外不落系统底色；面板底与描边由本控件自绘
         * @param event 绘制事件
         */
        void paintEvent(QPaintEvent *event) override;

    private:
        /**
         * @brief 按当前主题刷新面板、文字与状态圆点的颜色
         * @details 主题管理器不在构造时落样式表，控制条必须自己取一次色；
         *          切档后同一个入口再取一次，工具窗不会停在旧档色上。
         */
        void refreshColors();

        /// 按暂停态切换红点颜色
        void refreshDot();

        ThemeManager &m_theme;            ///< 主题来源，不拥有
        QLabel       *m_dot    = nullptr; ///< 录制状态圆点
        QLabel       *m_count  = nullptr; ///< 步数读数
        QPushButton  *m_pause  = nullptr; ///< 暂停/继续按钮
        QPushButton  *m_stop   = nullptr; ///< 停止按钮
        bool          m_paused = false;   ///< 当前是否暂停
        QColor        m_surface;          ///< 面板底色
        QColor        m_text;             ///< 正文色
        QColor        m_border;           ///< 面板描边色
        QColor        m_danger;           ///< 录制中的圆点色
        QColor        m_pausedDot;        ///< 暂停时的圆点色
    };

} // namespace GyanisScribe::Gui
