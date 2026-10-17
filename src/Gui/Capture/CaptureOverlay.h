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

#include "Core/Capture/ScreenUtils.h"
#include "Core/Pick/ColorFormat.h"
#include "Core/Pick/PickGeometry.h"

#include <array>
#include <optional>
#include <vector>

class QLineEdit;

namespace GyanisScribe::Core
{
    class ISettingsStore;
} // namespace GyanisScribe::Core

namespace GyanisScribe::Gui
{

    /**
     * @brief 遮罩上一条可换键的裸键对应哪个动作
     * @details 用例按这张表逐条钉：清册读出的那颗键要落到对应的动作上，落错一档就是「按了锁比例却开了尺寸框」。
     */
    enum class RebindableKeyAction
    {
        None,               ///< 这颗键不归可换那一批管
        ToggleAspectRatio,  ///< 锁 / 解长宽比
        OpenSizeEditor,     ///< 开尺寸输入框
        CycleRegionHistory, ///< 在最近几次选区之间循环挑（多按一个 Shift 是反向）
        CopySelectedSize,   ///< 把选区的成品像素尺寸复制进剪贴板
        CycleColorFormat,   ///< 放大镜读数里颜色那一格换一种写法
        ToggleCursor,       ///< 翻这一轮的「成品含鼠标指针」
    };

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
         * @param settings 配置存储：裸键那几档从 Gui/Controls/AppShortcuts.h 那份清册现读；生命周期必须长于本遮罩
         * @param parent 父窗口，默认无
         */
        CaptureOverlay(const QImage &background, const QRect &screenGeometry, qreal devicePixelRatio, Core::ISettingsStore &settings, QWidget *parent = nullptr);

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
         * @brief 交付光标处的一摞界面元素
         * @param globalRects 全局逻辑矩形，从最深一级排到窗口根；空一份表示光标下没有可切分的元素
         * @details 悬停预览优先落在这一摞的最深一级：比整扇窗口更贴目标。相邻同矩形的包装层
         *          （惰性树里常见）在这里并成一档——`,`、`.` 一步步选的是「块」，隔几档尺寸不动是死档。
         *          本方法按这块屏的原点换成局部坐标；空一份就是退回「只认窗口候选」。
         */
        void setElementChain(const std::vector<QRect> &globalRects);

        /**
         * @brief 交付这块屏上的历史选区
         * @param localRects 历史选区，屏内局部逻辑坐标，最近的在前；只放与这块屏同屏的那几条
         * @details `R` / `Shift+R` 在这份清单里循环挑：按一次换一块，转一圈回到起点。
         *          跨屏的那几条不进来——遮罩铺的是特定一块屏，把别屏的矩形放上来只会露出一块越界的框。
         */
        void setRegionHistory(const std::vector<QRect> &localRects);

        /**
         * @brief 光标正压着的那一个像素的颜色
         * @return 冻帧里的实际像素色；还没收到鼠标移动、或冻帧为空时返回无效颜色
         * @details 按这块屏的原点与缩放因子换到帧像素：副屏不从 (0,0) 开始，高 DPI 下帧又比逻辑尺寸大，
         *          两笔换算少任何一笔，报出来的都是光标别处的颜色。
         */
        [[nodiscard]] QColor colorUnderCursor() const;

        /**
         * @brief 放大镜占的位置
         * @return 该屏内的逻辑矩形；还没收到鼠标移动、或视野档是「不显示」时返回空矩形
         * @details 默认摆在光标右下，摆不下就翻到左上：贴到屏角时不翻就会有一半跑出这块屏，
         *          用户看到的格子里根本没有他正要框的那个像素。
         */
        [[nodiscard]] QRect magnifierRect() const;

        /**
         * @brief 此刻会不会画出放大镜那枚像素插图
         * @return 光标在这块屏上出现过、冻帧在、且视野档不是「不显示」时为真
         * @details 与 magnifierRect() 同一判据：悬停挑窗口、拖手柄、平移选区这三处都要像素级读数，
         *          「只在拖框时显示」不够用——挑起始角那一下正是最需要它的时候。
         */
        [[nodiscard]] bool isMagnifierVisible() const noexcept;

        /**
         * @brief 此刻会不会画出读数条（坐标 + 颜色那一横条）
         * @return 光标在这块屏上出现过且冻帧在时为真
         * @details 关掉放大镜插图不该把读数条一起没收：那两条信息本来就是这枚工具的另一半用途。
         */
        [[nodiscard]] bool isReadoutVisible() const noexcept;

        /**
         * @brief 读数条占的位置
         * @return 该屏内的逻辑矩形；读数条不画时返回空矩形
         * @details 摆在放大镜下沿，「不显示」那一档也按窄档的位置摆——同一处东西不该因为关一档就跳位。
         *          绘制与判据共读这一处，各自算一遍就会出现「点在条上却读不到」。
         */
        [[nodiscard]] QRect readoutStripRect() const;

        /**
         * @brief 这一轮生效的放大镜视野档
         * @return 构造时从配置读定的那一档
         * @details 遮罩一次会话只活几秒，所以这一档在构造时读一次：读坏了只记一条日志，
         *          而不是每帧绘制都刷一句同样的话。
         */
        [[nodiscard]] Core::MagnifierView magnifierView() const noexcept;

        /**
         * @brief 放大镜读数里颜色那一格用的写法
         * @return 当前档位；按 C 在 Hex、Rgb、Hsl 之间轮换
         */
        [[nodiscard]] Core::ColorFormat readoutFormat() const noexcept;

        /**
         * @brief 放大镜读数条里颜色那一格的文本
         * @return 按当前写法与配置里的字母档排好的那一串
         * @details 画读数条与判据共读这一处：排版埋在 paintEvent 里的话，
         *          「设置里的字母档没接上」就只能靠肉眼看画布，而画布上的像素判不住一串字。
         */
        [[nodiscard]] QString readoutColorText() const;

        /**
         * @brief 这一轮那几处数字按哪种像素量
         * @return 构造时从配置读定的那一档
         */
        [[nodiscard]] Core::SizeUnit sizeUnit() const noexcept;

        /**
         * @brief 把遮罩上的一块逻辑矩形换成这一档单位的那一块
         * @param logicalRegion 遮罩那一层的逻辑矩形（选区或悬停预览）
         * @return 逻辑档原样交回；物理档就是成品图的真实宽高与起点
         * @details 读数、`V` 复制与 `S` 尺寸框三处都从这一条取数：各处自己乘一遍倍率，
         *          就会有一处忘了乘，而这种错只在高分屏上露出来，1 倍屏上一切照旧。
         */
        [[nodiscard]] QRect measuredRect(const QRect &logicalRegion) const;

        /**
         * @brief 当前选区的宽高文本，按这一轮那一档量
         * @return 形如「1500×1050」；没有选区时宽高都是 0
         * @details `V` 复制的就是这一串，判据也读这一处：把排版留在按键分派里，
         *          「看到的与复制的不是一组数」就只能靠肉眼对画布。
         */
        [[nodiscard]] QString selectionSizeText() const;

        /**
         * @brief 光标正压在上的那一个候选
         * @return 已经在框选、或没有候选含住光标时返回空
         */
        [[nodiscard]] std::optional<QRect> hoveredRegion() const;

        /**
         * @brief 装上一档「记住的」长宽比并锁上（开遮罩时服务从配置里读到的就是它）
         * @param ratio 已约分的宽高比；任一维非正时什么都不做
         */
        void setLockedAspectRatio(const QSize &ratio);

        /**
         * @brief 当前锁住的长宽比
         * @return 已约分的比例；没锁时返回空尺寸
         */
        [[nodiscard]] QSize lockedAspectRatio() const;

        /**
         * @brief 装上这一轮「成品要不要含鼠标指针」的初值
         * @param included nullopt 表示这一轮没有这一档可翻：滚动长截图按帧接图，
         *          带指针会把一枚指针复制成一串，所以那一趟根本不碰这一档
         * @details 遮罩里裸按 `P` 可以临时翻这一档，翻完只影响这一轮、不写回配置：
         *          截菜单时想带指针、下一张又想要干净的，改设置来回两次不是出路。
         */
        void setCursorIncluded(std::optional<bool> included);

        /// 这一轮要不要把指针画进成品；这一档不存在时返回空
        [[nodiscard]] std::optional<bool> cursorIncluded() const noexcept;

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

        /**
         * @brief 光标在这块屏上动了，且此刻没在框选、也还没有选区
         * @param globalPoint 光标的全局逻辑坐标
         * @details 服务按这条去问平台「点下有哪些界面元素」，再把结果走 setElementChain 送回来。
         *          没在框选、没有选区时才报：这两种状态下悬停预览本来就不显示，白问一次是跨进程开销。
         */
        void hoverPointChanged(const QPoint &globalPoint);

        /**
         * @brief 长宽比锁开/关或换档了
         * @param ratio 锁住的比例（已约分）；解开时为空尺寸
         * @details 服务接这一声落盘（见 Settings 里那把 Capture/AspectRatio 键）：
         *          锁着的档跨轮次记住这件事就是它在办
         */
        void aspectRatioLockChanged(const QSize &ratio);

        /**
         * @brief 这一轮「要不要把指针画进成品」被 `P` 翻了
         * @param included 翻过之后的那一档
         * @details 只报意图不写配置：这一档是临时的，配置里那一格照旧管着没有遮罩的那几路。
         */
        void cursorIncludeChanged(bool included);

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
         *          裸按 `A` 锁/解长宽比（`Ctrl+A` 仍是选整块屏）、裸按 `S` 开尺寸输入框。
         */
        void keyPressEvent(QKeyEvent *event) override;

        /**
         * @brief 拦尺寸输入框的 Esc 与回车
         * @param watched 被观察对象
         * @param event 事件
         * @return 处理掉了返回 true
         * @details 两者都不是 QLineEdit 的信号：Esc 不给它拦住会穿到遮罩把整轮截图关掉；
         *          回车在滤镜里处理，`returnPressed` 与它两个都接会应用两次。
         */
        bool eventFilter(QObject *watched, QEvent *event) override;

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
         *          这句已经确定的话改成疑问句。有元素链时优先在链里选（按层数从最深往上），
         *          链取完或没有链时退回窗口候选。
         */
        void refreshHoveredRegion();

        /**
         * @brief 画八个抓点的小方块
         * @param painter 正在画这块遮罩的画笔
         * @param region 已经落定的选区
         */
        void paintGrips(QPainter &painter, const QRect &region);

        /**
         * @brief 八个抓点的位置
         * @param region 选区
         * @return 四角与四边中点，顺序与 Core::SelectionGrip 的边点同解
         */
        [[nodiscard]] static std::array<QPoint, 8> gripCentersFor(const QRect &region);

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
         * @brief 锁上/解开长宽比
         * @details 锁上时优先取当前选区的比例（先框一个相近的再按 A），没有选区就用这一轮里
         *          上次锁过的那一档；都没有时什么都不做——凭空锁一个比例只会让下一拖出意外。
         *          解开时清掉持久层那一档（信号里带上空尺寸），但"上一档"留在本轮里，
         *          再按 A 还能用同一个比例重新锁上。
         */
        void toggleAspectRatioLock();

        /// 在读数框的位置开尺寸输入框（预填当前选区尺寸）
        void openSizeEditor();

        /// 应用输入框里的尺寸：选区左上角不动，越界夹回屏内；敲不成尺寸就留在框里继续改
        void commitSizeEditor();

        /// 收起输入框且不应用：Esc 与「点别处」走这一条
        void cancelSizeEditor();

        /// 销毁输入框：两条收场路共用，免得各漏一半
        void closeSizeEditor();

        /**
         * @brief 这一次按键落在清册里哪一档可换键上
         * @param event 按键事件
         * @return 对应动作；一颗都没认到时返回 None
         * @details 键位读 Gui/Controls/AppShortcuts.h 那份清册，整颗组合相等才算命中；「挑最近选区」那一档
         *          还认多按一个 Shift 的反向档，方向由调用处按 Shift 位判。
         *          这一层排在 switch 之前，所以 Ctrl+A 这类既有组合与换上去的键撞上时，换上去的那一档赢。
         */
        [[nodiscard]] RebindableKeyAction rebindableActionFor(const QKeyEvent *event) const;

        /**
         * @brief 放大镜这一轮的取样与摆放
         * @return 布局；还没收到鼠标移动时三个量都不可用
         * @details 视野档是「不显示」时也按窄档算一遍位置：读数条要留在它一直在的那一处，
         *          关掉插图不该让那一条跳到别的位置。
         */
        [[nodiscard]] Core::MagnifierLayout magnifierLayout() const;

        /**
         * @brief 放大镜读数条上的坐标部分
         * @return 形如「(1920, 300)」的全局逻辑坐标
         */
        [[nodiscard]] QString formatCursorReadout() const;

        QImage                m_background;                              ///< 该屏的物理像素图，按缩放因子画成逻辑大小
        Core::ISettingsStore &m_settings;                                ///< 裸键那几档的读端（Gui/Controls/AppShortcuts.h 那份清册），不拥有
        QRect                 m_screenGeometry;                          ///< 该屏的全局逻辑几何
        qreal                 m_devicePixelRatio = 1.0;                  ///< 该屏缩放因子
        QPoint                m_anchorPoint;                             ///< 本次框选的按下点，局部逻辑坐标
        QPoint                m_currentPoint;                            ///< 框选过程中的当前点
        QPoint                m_cursorPoint;                             ///< 光标最近所在点，只喂辅助线、悬停与放大镜，不参与选区计算
        bool                  m_hasCursorPoint = false;                  ///< 是否收到过鼠标移动：没有就不摆放大镜，别把 (0,0) 当成光标
        Core::ColorFormat     m_readoutFormat  = Core::ColorFormat::Hex; ///< 读数里颜色那格的写法，按 C 轮换
        Core::MagnifierView   m_magnifierView;                           ///< 这一轮的放大镜视野档，构造时读一次
        Core::SizeUnit        m_sizeUnit = Core::SizeUnit::Logical;      ///< 这一轮那几处数字用的单位，构造时读一次

        std::vector<QRect>                 m_regionHistory;         ///< 这块屏上的历史选区，最近的在前；由 Gui::CaptureService 推过来
        int                                m_historyIndex = -1;     ///< 当前选区取自历史里的哪一条；-1 表示不是取自历史（用户自己框/改的）
        QString                            m_copiedSizeHint;        ///< 「已复制尺寸 …」那行提示，指针一动就清
        bool                               m_isAdjusting = false;   ///< 是否正拖着某个抓点改选区
        std::optional<Core::SelectionGrip> m_draggedGrip;           ///< 拖着的那个抓点，没拖时为空
        QPoint                             m_adjustPoint;           ///< 上一帧的鼠标位置，拖抓点时按增量走
        bool                               m_isSelecting = false;   ///< 是否正在拖动框选
        std::vector<QRect>                 m_windowCandidates;      ///< 窗口候选，已换成局部逻辑坐标，按 z 序排
        std::vector<QRect>                 m_elementChain;          ///< 光标处的元素链，已换成局部逻辑坐标，由深到浅；相邻同矩形已并档
        std::size_t                        m_elementAscent = 0;     ///< 从这里往上数了几级（0 = 最深一级）；等于链长时看整扇窗口
        std::optional<QRect>               m_hoveredRegion;         ///< 光标压在上的那一个候选，没有则空
        QSize                              m_aspectRatio;           ///< 上一次锁过的长宽比（已约分）；空 = 还没有过
        bool                               m_aspectLocked  = false; ///< 长宽比锁此刻是不是开着
        bool                               m_cursorTouched = false; ///< 指针档被 `P` 翻过没有：没翻过就不必多占读数那一格
        std::optional<bool>                m_cursorIncluded;        ///< 这一轮成品要不要含指针；空 = 这一轮没有这一档
        QLineEdit                         *m_sizeEditor = nullptr;  ///< 尺寸输入框，开着时才有
    };
} // namespace GyanisScribe::Gui
