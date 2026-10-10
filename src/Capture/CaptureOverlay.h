/**
 * @file CaptureOverlay.h
 * @brief 截图遮罩：压暗全屏、框选区域、显示尺寸与坐标读数、拖动途中给一枚像素放大镜
 * @author Gyanis
 * @date 2026-10-09
 * @version 0.0.1
 * @copyright Copyright (c) . All rights reserved.
 */

#pragma once

#include <QColor>
#include <QImage>
#include <QPoint>
#include <QRect>
#include <QWidget>

#include <optional>
#include <vector>

class QKeyEvent;

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
         * @brief 光标正压着的那一个像素的颜色
         * @return 冻帧里的实际像素色；还没收到鼠标移动、或冻帧为空时返回无效颜色
         * @details 按这块屏的原点与缩放因子换到帧像素：副屏不从 (0,0) 开始，高 DPI 下帧又比逻辑尺寸大，
         *          两笔换算少任何一笔，报出来的都是光标别处的颜色。
         */
        [[nodiscard]] QColor colorUnderCursor() const;

        /**
         * @brief 放大镜占的位置
         * @return 该屏内的逻辑矩形；还没收到鼠标移动时返回空矩形
         * @details 默认摆在光标右下，摆不下就翻到左上：贴到屏角时不翻就会有一半跑出这块屏，
         *          用户看到的格子里根本没有他正要框的那个像素。
         */
        [[nodiscard]] QRect magnifierRect() const;

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
         * @brief 键盘：Enter 确认，Esc 取消，方向键微调选区
         * @param event 键盘事件
         * @details 方向键单独按是整块平移，按住 Shift 改成挪右下角；还没框出选区时什么都不做。
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
         * @brief 方向键对应的那一格位移
         * @param event 键盘事件
         * @return 左/右/上/下分别给 (-1,0)、(1,0)、(0,-1)、(0,1)；别的键给空
         */
        [[nodiscard]] static std::optional<QPoint> arrowStep(const QKeyEvent &event);

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

        /**
         * @brief 画放大镜：像素格与光标那一点的颜色读数
         * @param painter 正在画这块遮罩的画笔
         * @details 只在拖动途中画：选区一定下来放大镜就挡住刚框好的那块，读数此时已经没用了。
         */
        void paintMagnifier(QPainter &painter);

        /**
         * @brief 光标所在点的全局逻辑坐标
         * @return 局部点加上这块屏的原点
         */
        [[nodiscard]] QPoint globalCursorPoint() const;

        /**
         * @brief 放大镜读数条上的坐标部分
         * @return 形如「(1920, 300)」的全局逻辑坐标
         */
        [[nodiscard]] QString formatCursorReadout() const;

        QImage               m_background;             ///< 该屏的物理像素图，按缩放因子画成逻辑大小
        QRect                m_screenGeometry;         ///< 该屏的全局逻辑几何
        qreal                m_devicePixelRatio = 1.0; ///< 该屏缩放因子
        QPoint               m_anchorPoint;            ///< 本次框选的按下点，局部逻辑坐标
        QPoint               m_currentPoint;           ///< 框选过程中的当前点
        QPoint               m_cursorPoint;            ///< 光标最近所在点，只喂辅助线、悬停与放大镜，不参与选区计算
        bool                 m_hasCursorPoint = false; ///< 是否收到过鼠标移动：没有就不摆放大镜，别把 (0,0) 当成光标
        bool                 m_isSelecting    = false; ///< 是否正在拖动框选
        std::vector<QRect>   m_windowCandidates;       ///< 窗口候选，已换成局部逻辑坐标，按 z 序排
        std::optional<QRect> m_hoveredRegion;          ///< 光标压在上的那一个候选，没有则空
    };

} // namespace GyanisScribe::Capture
