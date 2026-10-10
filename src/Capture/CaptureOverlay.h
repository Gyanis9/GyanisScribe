/**
 * @file CaptureOverlay.h
 * @brief 截图遮罩：压暗全屏、框选区域、显示尺寸与坐标读数
 * @author Gyanis
 * @date 2026-10-09
 * @version 0.0.1
 * @copyright Copyright (c) . All rights reserved.
 */

#pragma once

#include <QImage>
#include <QPoint>
#include <QRect>
#include <QWidget>

#include <optional>
#include <vector>

namespace GyanisScribe::Capture
{

    /**
     * @brief 一块屏幕上的框选遮罩
     * @details 一个实例只管一块屏：几何与缩放因子在构造时交进来，之后所有换算都按那一块的读边走，
     *          跨屏拖动不会被拼成一张图（多显示器各自一块遮罩，各框各的）。
     * @note 遮罩上刻意不放任何复杂控件，只画读数：放上去的按钮会把用户的框选目标遮住。
     */
    class CaptureOverlay : public QWidget
    {
        Q_OBJECT

    public:
        /**
         * @brief 用该屏按下瞬间的整屏图构造遮罩
         * @param background 该屏的物理像素图，选区内显示它的原图
         * @param screenGeometry 该屏的全局逻辑几何，遮罩就铺在这一块上
         * @param devicePixelRatio 该屏缩放因子，必须大于 0
         * @param parent 父窗口，默认无
         */
        CaptureOverlay(const QImage &background, const QRect &screenGeometry, qreal devicePixelRatio, QWidget *parent = nullptr);

        /**
         * @brief 析构遮罩
         */
        ~CaptureOverlay() override;

        /**
         * @brief 当前的选区
         * @return 该屏内的逻辑矩形，宽高已被归一为非负；还没框时为 QRect()
         */
        [[nodiscard]] QRect selectedRegion() const;

        /**
         * @brief 确认当前选区并发出 regionSelected
         * @note 空选区时不发声：一次没有落成的框不该产出一张 0×0 的图
         */
        void confirmSelection();

        /**
         * @brief 交给遮罩的窗口候选
         * @param globalRects 全局逻辑矩形，按压盖顺序从最上面排到最下面
         * @details 空一份就是不启用「光标压在哪个窗口上」：平台给不出候选时遮罩照旧只做框选。
         *          传全局坐标即可，本方法按这块屏的原点换成局部坐标。
         */
        void setWindowCandidates(const std::vector<QRect> &globalRects);

        /**
         * @brief 光标正压在上的那一个候选
         * @return 已经在框选、或没有候选含住光标时返回空
         */
        [[nodiscard]] std::optional<QRect> hoveredRegion() const;

    signals:
        /**
         * @brief 选区已确认
         * @param logicalRegion 该屏内的逻辑矩形
         */
        void regionSelected(const QRect &logicalRegion);

        /**
         * @brief 用户取消了这次截图
         */
        void dismissed();

    protected:
        /**
         * @brief 绘制背景、压暗层、选区边框与读数
         * @param event 绘制事件，本实现不使用其载荷
         */
        void paintEvent(QPaintEvent *event) override;

        /**
         * @brief 按下开始框选
         * @param event 鼠标事件
         */
        void mousePressEvent(QMouseEvent *event) override;

        /**
         * @brief 拖动更新选区
         * @param event 鼠标事件
         */
        void mouseMoveEvent(QMouseEvent *event) override;

        /**
         * @brief 松开完成框选
         * @param event 鼠标事件
         */
        void mouseReleaseEvent(QMouseEvent *event) override;

        /**
         * @brief 双击直接确认
         * @param event 鼠标事件
         */
        void mouseDoubleClickEvent(QMouseEvent *event) override;

        /**
         * @brief 键盘：Enter 确认，Esc 取消
         * @param event 键盘事件
         */
        void keyPressEvent(QKeyEvent *event) override;

    private:
        /**
         * @brief 由按下点与当前点组成归一化矩形
         * @param anchor 按下点
         * @param current 当前点
         * @return 宽高非负的逻辑矩形
         */
        [[nodiscard]] static QRect normalizeRect(const QPoint &anchor, const QPoint &current);

        /**
         * @brief 读数应当画在哪
         * @param region 选区
         * @return 选区上方的位置；贴到屏顶时改画在选区下方
         */
        [[nodiscard]] QPoint hintPositionFor(const QRect &region) const;

        /**
         * @brief 按当前点重算「光标压在哪个候选上」
         * @details 只在还没有选区时算：用户已经框出一块之后再亮一个别的矩形，会把「这就是要截的那块」
         *          这句已经确定的话改成疑问句。
         */
        void refreshHoveredRegion();

        QImage               m_background;             ///< 该屏的物理像素图，按缩放因子画成逻辑大小
        QRect                m_screenGeometry;         ///< 该屏的全局逻辑几何
        qreal                m_devicePixelRatio = 1.0; ///< 该屏缩放因子
        QPoint               m_anchorPoint;            ///< 本次框选的按下点，局部逻辑坐标
        QPoint               m_currentPoint;           ///< 框选过程中的当前点
        QPoint               m_cursorPoint;            ///< 光标最近所在点，只喂辅助线与悬停，不参与选区计算
        bool                 m_isSelecting = false;    ///< 是否正在拖动框选
        std::vector<QRect>   m_windowCandidates;       ///< 窗口候选，已换成局部逻辑坐标，按 z 序排
        std::optional<QRect> m_hoveredRegion;          ///< 光标压在上的那一个候选，没有则空
    };

} // namespace GyanisScribe::Capture
