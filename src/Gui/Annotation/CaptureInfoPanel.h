/**
 * @file CaptureInfoPanel.h
 * @brief 截图信息面板：浮在标注窗旁边，写明这一帧来自哪个应用、窗口、屏幕与时刻
 * @author Gyanis
 * @date 2026-11-09
 * @version 0.0.1
 * @copyright Copyright (c) . All rights reserved.
 */

#pragma once

#include "Core/Capture/SourceInfo.h"

#include <QWidget>

class QKeyEvent;
class QLabel;
class QToolButton;

namespace GyanisScribe::Gui
{

    /**
     * @brief 截图来源的浮层
     * @details 独立顶层工具窗：不抢焦点、不进布局，也不参与画布导出。四行值文本按来源逐项写入，
     *          取不到的字段给「未知」那一档，不拿占位串冒充。关闭只藏这一扇窗，面板对象由
     *          标注窗跨轮次复用，下一轮是否再露出来仍由开关与来源决定。
     */
    class CaptureInfoPanel : public QWidget
    {
        Q_OBJECT

    public:
        /**
         * @brief 构造信息面板
         * @param parent Qt 父对象，默认无：面板是顶层窗，带父控件会被标注窗裁切
         */
        explicit CaptureInfoPanel(QWidget *parent = nullptr);

        /**
         * @brief 析构面板
         */
        ~CaptureInfoPanel() override;

        CaptureInfoPanel(const CaptureInfoPanel &)            = delete;
        CaptureInfoPanel &operator=(const CaptureInfoPanel &) = delete;

        /**
         * @brief 换一份来源并刷新四行文本
         * @param source 这一轮的截图来源；空字段按「未知」显示
         * @details 应用取进程名去掉扩展名后的主名，窗口与屏幕取原串，时刻按年月日时分秒写；
         *          值文本超出面板宽度时右侧收成省略号，完整内容留在悬停提示里。
         */
        void setSource(const Core::CaptureSourceInfo &source);

        /**
         * @brief 面板按内容算出来的尺寸
         * @return sizeHint；控件还没上屏、布局尚未算定时给一档按内容估的下限
         * @note 摆放要在 show 之前定下来，这时 sizeHint 可能还是空的
         */
        [[nodiscard]] QSize preferredSize() const;

    signals:
        /**
         * @brief 用户收起了面板（右上角关闭或 Esc）
         */
        void closed();

    protected:
        /**
         * @brief 自绘连续曲率面板与描边
         * @param event 绘制事件
         */
        void paintEvent(QPaintEvent *event) override;

        /**
         * @brief Esc 收起面板
         * @param event 按键事件
         */
        void keyPressEvent(QKeyEvent *event) override;

    private:
        /// 收起面板并报一声：关闭按钮与 Esc 共用这一处
        void closePanel();

        QLabel      *m_appValue     = nullptr; ///< 应用那一行，objectName 为 captureInfoApp
        QLabel      *m_titleValue   = nullptr; ///< 窗口标题那一行，objectName 为 captureInfoTitle
        QLabel      *m_monitorValue = nullptr; ///< 屏幕那一行，objectName 为 captureInfoMonitor
        QLabel      *m_timeValue    = nullptr; ///< 捕获时刻那一行，objectName 为 captureInfoTime
        QToolButton *m_closeButton  = nullptr; ///< 右上角关闭，objectName 为 captureInfoClose
    };

} // namespace GyanisScribe::Gui
