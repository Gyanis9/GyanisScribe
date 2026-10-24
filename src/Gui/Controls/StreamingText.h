/**
 * @file StreamingText.h
 * @brief Toast 的流式文字：逐字时间轴与盖在原生标签上的暂时覆盖层
 * @author Gyanis
 * @date 2026-10-24
 * @version 0.0.1
 * @copyright Copyright (c) . All rights reserved.
 */

#pragma once

#include <QColor>
#include <QElapsedTimer>
#include <QFont>
#include <QImage>
#include <QRectF>
#include <QResizeEvent>
#include <QString>
#include <QTimer>
#include <QWidget>

#include <vector>

namespace GyanisScribe::Gui
{
    /**
     * @brief 逐字显现的时间轴
     * @details 第 i 个字在 i×30ms 起步，单字用 90ms 从模糊走到清晰。
     */
    struct StreamingTimeline
    {
        int count     = 0;  ///< 字数，小于等于 0 时总时长为 0
        int staggerMs = 30; ///< 相邻两字的起步间隔
        int fadeMs    = 90; ///< 单字从模糊到清晰的时间

        /**
         * @brief 整段时间轴的长度
         * @return 90 + 30×(字数−1)，字数为 0 时返回 0
         */
        [[nodiscard]] qint64 totalDurationMs() const noexcept
        {
            return count <= 0 ? 0 : fadeMs + staggerMs * (count - 1);
        }

        /**
         * @brief 取某个字在某一时刻的显现进度
         * @param index 字序号，越界时返回 0
         * @param elapsedMs 从动画起点算起的毫秒数
         * @return 0 到 1 的进度，缓动为 cubic-bezier(.215,.61,.355,1)
         */
        [[nodiscard]] qreal progressFor(int index, qint64 elapsedMs) const;
    };

    /**
     * @brief 盖在 Toast 文本标签上的流式显现层
     * @details 原生 QLabel 仍是文字与选区的真身，本层只在动画期间铺满它的矩形；
     *          收工或换新提示时销毁，露出原生文字。整段文字先按 DPR 渲染成清晰与模糊两张图，
     *          每帧只做逐字裁剪与透明度叠加，不做逐帧模糊。
     */
    class StreamingTextOverlay final : public QWidget
    {
        Q_OBJECT

    public:
        /**
         * @brief 构造覆盖层
         * @param parent 父控件，通常是 Toast 的文本标签
         * @param text 要显现的整段文字
         * @param font 与原生标签一致的字体
         * @param textColor 文字色，取该条提示的语义色
         * @param backgroundColor 气泡底色，先铺满再画字
         */
        StreamingTextOverlay(QWidget *parent, QString text, QFont font, QColor textColor, QColor backgroundColor);

        /**
         * @brief 启动逐字显现
         * @details 没有可画的字或动画被关闭时直接收工，由 finished 交给调用方销毁。
         */
        void start();

        /**
         * @brief 把时间轴向前推指定毫秒
         * @param milliseconds 本次推进的毫秒数，负值按 0 处理
         * @details 只给用例用：不依赖真实时钟。时间轴到头的那一帧先画出来，再推进一次才收工。
         */
        void advanceForTesting(qint64 milliseconds);

        /// 整段时间轴的长度
        [[nodiscard]] qint64 totalDurationMs() const noexcept;

        /// 是否已收工
        [[nodiscard]] bool isFinished() const noexcept;

        /// 已完成的真实绘制次数，用例据此判每帧有没有重绘
        [[nodiscard]] int paintCount() const noexcept;

    signals:
        /// 动画结束，覆盖层已隐藏，可以销毁
        void finished();

    protected:
        /**
         * @brief 铺底色并按进度画每个字
         * @details 重写 QWidget::paintEvent()：不调用基类，底色与文字都由本层画。
         * @param event 绘制事件
         */
        void paintEvent(QPaintEvent *event) override;

        /**
         * @brief 尺寸变化时按新矩形重建两层图与逐字矩形
         * @details 重写 QWidget::resizeEvent()：先让基类处理几何，再按落定的宽高重建。
         * @param event 尺寸变化事件
         */
        void resizeEvent(QResizeEvent *event) override;

    private:
        /// 一个字的裁剪矩形：文本布局坐标，单位逻辑像素
        struct GlyphRun
        {
            int    index = 0; ///< 时间轴序号
            QRectF rect;      ///< 该字在覆盖层里的矩形
        };

        /// 用当前字体重排整段文字，并按 DPR 渲染清晰与模糊两层
        void buildLayers();

        /// 定时器每帧推进一次，按真实时钟取经过时间
        void tick();

        /// 停表、隐藏并发出 finished
        void finish();

        QString               m_text;              ///< 整段文字
        QFont                 m_font;              ///< 与原生标签一致的字体
        QColor                m_textColor;         ///< 语义文字色
        QColor                m_background;        ///< 气泡底色
        QImage                m_sharp;             ///< 清晰版整段文字，按 DPR 渲染
        QImage                m_blurred;           ///< 模糊版整段文字，启动前算好
        std::vector<GlyphRun> m_glyphs;            ///< 逐字裁剪矩形，空格不入表
        StreamingTimeline     m_timeline;          ///< 逐字时间轴
        QTimer                m_timer;             ///< 帧定时器
        QElapsedTimer         m_clock;             ///< 真实经过时间
        qint64                m_elapsedMs = 0;     ///< 已推进的毫秒数
        int                   m_paintCount = 0;    ///< 真实绘制次数，只增不减
        bool                  m_finished  = false; ///< 是否已收工
    };
} // namespace GyanisScribe::Gui
