/**
 * @file PinWindow.h
 * @brief 一张贴图的窗口：无边框、置顶、可拖动缩放旋转调透明度与穿透
 * @author Gyanis
 * @date 2026-10-09
 * @version 0.0.1
 * @copyright Copyright (c) . All rights reserved.
 */

#pragma once

#include "Pin/IPinWindow.h"
#include "Pin/PinTransform.h"

#include <QImage>
#include <QPoint>
#include <QRectF>
#include <QVector>
#include <QWheelEvent>
#include <QWidget>

#include <optional>
#include <vector>

class QMimeData;

namespace GyanisScribe::Annotation
{
    class AnnotationEngine;
    class AnnotationToolbar;
    class AnnotationToolbarBridge;
    class AnnotationView;
} // namespace GyanisScribe::Annotation

namespace GyanisScribe::Platform
{
    class IPlatformIntegration;
} // namespace GyanisScribe::Platform

namespace GyanisScribe::Settings
{
    class ISettingsStore;
} // namespace GyanisScribe::Settings

namespace GyanisScribe::Ui
{
    class IconProvider;
} // namespace GyanisScribe::Ui

namespace GyanisScribe::Pin
{

    /**
     * @brief 贴图窗口
     * @details 窗口的矩形始终是「旋转后的外接框」，绘制时按中心旋转回去：这样拖动、命中与手柄
     *          都在一个轴对齐的框里算，不必到处处理斜框。
     * @note 一次操作要改的只有中心位置、缩放、旋转与透明度四项，其余（窗口尺寸、手柄位置）都由这四项派生，
     *       所以任何一处改动都走 applyGeometry() 这一个出口，避免出现第二套尺寸算法。
     */
    class PinWindow : public QWidget, public IPinWindow
    {
        Q_OBJECT

    public:
        /**
         * @brief 用一张图构造贴图
         * @param image 图片（物理像素），显示尺寸按其 devicePixelRatio 折算成逻辑尺寸
         * @param settings 配置来源，保存贴图时从这里取目录
         * @param integration 平台集成，用于查鼠标穿透能力
         * @param parent 父窗口，默认无（顶层）
         */
        PinWindow(const QImage &image, Settings::ISettingsStore &settings, const Platform::IPlatformIntegration &integration, QWidget *parent = nullptr);

        /**
         * @brief 析构窗口
         */
        ~PinWindow() override;

        /**
         * @brief 切换缩略图模式
         * @param enabled true 表示缩成屏上一枚小方块，false 表示恢复原来的大小
         * @details 只改窗口几何与绘制方式，不动缩放、旋转与透明度：退出时回到原样，位置保持中心不动。
         *          缩略图态下不旋转（一枚小方块里转起来看不清是什么），退出后旋转照旧。
         */
        void setThumbnailMode(bool enabled);

        /**
         * @brief 当前是不是缩略图模式
         * @return true 表示现在是那枚小方块
         */
        [[nodiscard]] bool isThumbnailMode() const noexcept;

        /**
         * @brief 在贴图上直接标注：进入标注模式
         * @details 把这张贴图当作画布交给标注引擎，标注视图铺满整窗，工具栏浮在旁边——
         *          与标注窗是同一套引擎、视图与快捷键表。
         *          两处先决动作：缩略图态先摆回整张（小方块上画不了东西）；旋转过的先转正、
         *          退出时转回原角度（视图是轴对齐的，斜画布铺不平，而成品记的是图片自己的像素）。
         * @note 动图不进标注：标注会被下一帧盖掉，这种情况记一条日志而不是悄悄吞掉。
         */
        void startAnnotation();

        /**
         * @brief 结束标注：把画上的图形烘进这张贴图并退出标注模式
         * @details 成品走引擎的 exportImage（与标注窗保存的是同一条路）；旋转角与贴边收起态
         *          恢复到进入之前。没在标注时调用是幂等的。
         */
        void finishAnnotation();

        /**
         * @brief 此刻是不是在标注模式
         * @return true 表示鼠标键盘归标注那一套
         */
        [[nodiscard]] bool isAnnotating() const noexcept;

        /**
         * @brief 设所属贴图分组
         * @param group 分组号，从 1 起；小于 1 的取值会被夹到 1
         */
        void setGroup(int group);

        /**
         * @brief 所属贴图分组
         * @return 分组号，最小是 1
         */
        [[nodiscard]] int group() const noexcept;

        /**
         * @brief 告诉这张贴图「现在有哪些分组」
         * @param groups 现有分组号，升序；右键菜单的「移动到分组」按它长出条目
         * @details 窗口不自己去问管理器：分组是管理器那份模型的账，
         *          窗口只是在菜单里替它列出可选项，清单由管理器推过来。
         */
        void setGroupChoices(const QVector<int> &groups);

        /**
         * @brief 进出取色模式
         * @param enabled true 表示鼠标变成取色笔：悬停出读数，点一下就把那一像素的颜色交出去
         * @details 取色模式不改图、不改缩放：它只是把鼠标事件从「拖动/缩放」改成「读像素」。
         *          进去时把在拖的手柄清掉，免得从手柄上按下时两种语义打架。
         */
        void setColorPickMode(bool enabled);

        /**
         * @brief 当前是不是取色模式
         * @return true 表示下一次左键点击会取走一个像素的颜色
         */
        [[nodiscard]] bool isColorPickMode() const noexcept;

        /**
         * @brief 取色模式下光标处那一像素的颜色读数
         * @return 已按设置里的写法排好的文本（例如 #3C78C8）；光标不在图上时返回空
         */
        [[nodiscard]] std::optional<QString> hoveredColorText() const;

        /**
         * @brief 现在贴在屏的哪条边上
         * @return 贴的边；没贴边返回 ScreenEdge::None
         */
        [[nodiscard]] ScreenEdge edgeDock() const noexcept;

        /**
         * @brief 接上一个文件作为动图来源
         * @param file 盘上的图片文件；不是动图（或读不出）时什么都不做，窗口仍显示构造时那张静图
         * @details 动图只认「文件」这一条来路：剪贴板与标注贴出来的图在内存里，没有帧序列可播。
         *          播放只在窗口露在屏上时进行（收起或隐藏时停下），否则一张藏起来的贴图会把 CPU 一直吃住。
         */
        void setAnimatedSource(const QString &file);

        /**
         * @brief 这张贴图是不是动图
         * @return true 表示有帧序列可播（哪怕此刻暂停着）
         */
        [[nodiscard]] bool isAnimated() const noexcept;

        /**
         * @brief 把这张贴图贴到最近的屏边并收成一条（或取消贴边）
         * @param docked true 表示贴边收起，false 表示取消贴边、整张摆回屏上
         * @details 贴到离当前矩形最近的边；菜单那一项与拖到边的释放走同一条路。
         *          贴边收起后留在屏上最窄的一条，悬停会自动露出来（见 enterEvent）。
         */
        void setEdgeDocked(bool docked);

        /**
         * @brief 设缩放，见 IPinWindow::setScale()
         * @param scale 缩放因子，越界按边界取
         */
        void setScale(double scale) override;

        /// 当前缩放，见 IPinWindow::scale()
        [[nodiscard]] double scale() const noexcept override;

        /**
         * @brief 设旋转角度，见 IPinWindow::setRotationDegrees()
         * @param degrees 顺时针角度，折进 [0,360)
         */
        void setRotationDegrees(double degrees) override;

        /// 当前旋转角度，见 IPinWindow::rotationDegrees()
        [[nodiscard]] double rotationDegrees() const noexcept override;

        /**
         * @brief 设不透明度，见 IPinWindow::setOpacityLevel()
         * @param opacity 取值 (0,1]，越界按边界取
         */
        void setOpacityLevel(double opacity) override;

        /// 当前不透明度，见 IPinWindow::opacityLevel()
        [[nodiscard]] double opacityLevel() const noexcept override;

        /**
         * @brief 开关鼠标穿透，见 IPinWindow::setMouseThrough()
         * @param through 是否穿透
         * @return 成功返回空载荷；平台没有该能力时返回 PlatformUnsupported 并写明替代做法
         * @details 重写接口：走 Qt 的 WindowTransparentForInput 标志（平台原生能力），
         *          穿透时同时去掉焦点接受，避免贴图吃不到鼠标却又抢走焦点。
         */
        Core::Expected<void> setMouseThrough(bool through) override;

        /// 是否穿透，见 IPinWindow::isMouseThrough()
        [[nodiscard]] bool isMouseThrough() const noexcept override;

        /**
         * @brief 开关置顶，见 IPinWindow::setAlwaysOnTop()
         * @param pinned 是否保持置顶
         */
        void setAlwaysOnTop(bool pinned) override;

        /// 是否置顶，见 IPinWindow::isAlwaysOnTop()
        [[nodiscard]] bool isAlwaysOnTop() const noexcept override;

        /**
         * @brief 收起或摆回这张贴图，见 IPinWindow::setConcealed()
         * @param concealed 收起还是摆回
         * @details 重写基类接口：收 = hide()，摆回 = show() 再 raise()——不 raise 的话
         *          它回到桌面时落在别的窗口后面，看起来就像没收回来。图片与摆位都不动。
         */
        void setConcealed(bool concealed) override;

        /**
         * @brief 此刻是否被收着，见 IPinWindow::isConcealed()
         * @return 窗口不在屏上时为真
         */
        [[nodiscard]] bool isConcealed() const noexcept override;

        /**
         * @brief 交进剪贴板，见 IPinWindow::copyToClipboard()
         */
        void copyToClipboard() override;

        /**
         * @brief 存成文件，见 IPinWindow::saveToFile()
         * @param directory 目标目录，不存在时先创建
         * @param fileName 目录里的文件名，原样使用，不加时间戳也不改名；后缀决定写成什么格式
         * @param quality 有损档的质量，-1 表示交给 Qt 的默认档
         * @return 成功返回写下的路径；建目录或写文件失败时返回带路径的原因
         * @details 与基类一致，差别在命名：文件名归调用方，管理器一次写整批时靠序号区分，
         *          否则同一毫秒里的两张会写到同一个文件上，只剩最后一张。
         */
        [[nodiscard]] Core::Expected<QString> saveToFile(const std::filesystem::path &directory, const QString &fileName, int quality) override;

        /// 全屏坐标里的位置，见 IPinWindow::globalRect()
        [[nodiscard]] QRect globalRect() const override;

        /// 布局记录，见 IPinWindow::state()
        [[nodiscard]] PinState state() const override;


        /**
         * @brief 手柄占位框，供绘制与用例核对
         * @return 八个框，顺序与 HandleKind 一致
         */
        [[nodiscard]] std::vector<QRectF> handleRects() const;

        /**
         * @brief 建右键菜单
         * @return 菜单对象，所有权交调用方（Qt 的 exec 路径）
         * @note 公开出来是给用例核对菜单结构与接线：菜单项的动作都握在 lambda 里，
         *       不把菜单本身借出来，那些接线就只能靠人读代码
         */
        [[nodiscard]] class QMenu *buildContextMenu();

    signals:
        /**
         * @brief 用户要求识别这张图上的文字
         * @param image 当前图片
         * @note 由 PinManager 逐张转发出去，界面层在 AppController 里接；菜单项恒是可点的，
         *       能不能识别由后端回答——拿不到后端时是一条中文原因，而不是把项灰掉让人猜
         */
        void ocrRequested(const QImage &image);

        /**
         * @brief 用户从右键菜单要求把这张贴图移到某个分组
         * @param group 目标分组号；比现有最大组号大 1 表示「新建分组」
         * @details 只发意图、自己不改分组：分组是管理器那份模型里的账，
         *          改动与随之而来的显示/隐藏都由它一处说了算
         */
        void moveToGroupRequested(int group);

        /**
         * @brief 取色模式下点中了一个像素
         * @param color 那一像素的颜色
         * @details 只报颜色：排成哪种写法、复制到剪贴板还是存起来，都是界面层的事；
         *          贴图窗自己只管「点到了哪一格」
         */
        void colorPicked(const QColor &color);

        /**
         * @brief 标注模式下点了「贴到屏幕上」：把成品另贴一张新贴图
         * @param image 烘焙好的成品
         * @details 不动这一张：要的是「改这张」之外的另一种走向（留底再贴一份）才走这条路；
         *          摆位由管理器按本张附近算，界面层只管建一张新贴图
         */
        void pinAnotherRequested(const QImage &image);

        /**
         * @brief 有东西被拖到这张贴图上（盘上的文件、链接或文本）
         * @param data 拖放载荷，只在本次事件处理期间有效
         * @param globalTopLeft 落点的全局坐标，新贴图从这里开始摆
         * @details 窗口只报「谁被拖到了哪」：渲染成什么、贴几张、失败怎么说，都是界面层的事。
         */
        void dropRequested(const QMimeData *data, const QPoint &globalTopLeft);

        /**
         * @brief 贴图被关闭，管理器据此把自己记的那一份摘掉
         */
        void closedByUser();

    protected:
        /**
         * @brief 绘制图片、选中边框与手柄
         * @param event 绘制事件，本实现不使用其载荷
         */
        void paintEvent(QPaintEvent *event) override;

        /**
         * @brief 指针进到窗口上：贴边收起的贴图在这里露出来
         * @param event 进入事件
         * @details 收成一条之后它大部分在屏外，能接到的交互只有这条边——悬停展开是它唯一的自救口
         */
        void enterEvent(QEnterEvent *event) override;

        /**
         * @brief 指针离开窗口：贴边的贴图在这里收回去
         * @param event 离开事件
         */
        void leaveEvent(QEvent *event) override;

        /**
         * @brief 窗口露出来了：动图在这时开播
         * @param event 显示事件
         */
        void showEvent(QShowEvent *event) override;

        /**
         * @brief 窗口藏起来了：动图在这时停下
         * @param event 隐藏事件
         * @details 收起（setConcealed）走的也是 hide，所以两条路一起管住，不用两处各停一次
         */
        void hideEvent(QHideEvent *event) override;

        /**
         * @brief 按下：在手柄上起拖缩放，否则开始移动
         * @param event 鼠标事件
         */
        void mousePressEvent(QMouseEvent *event) override;

        /**
         * @brief 拖动：改缩放或移动窗口
         * @param event 鼠标事件
         */
        void mouseMoveEvent(QMouseEvent *event) override;

        /**
         * @brief 松开：结束本次拖拽
         * @param event 鼠标事件
         */
        void mouseReleaseEvent(QMouseEvent *event) override;

        /**
         * @brief 左键双击：回到原始比例与角度
         * @param event 鼠标事件，非左键时交回 Qt 继续分发
         * @details 重写 QWidget 的空实现：这一击与按 `0` 走同一个出口，已经在 1:1 且没转角时是幂等。
         */
        void mouseDoubleClickEvent(QMouseEvent *event) override;

        /**
         * @brief 滚轮：默认缩放，Ctrl 旋转，Shift 改透明度
         * @param event 滚轮事件
         */
        void wheelEvent(QWheelEvent *event) override;

        /**
         * @brief 键盘：Esc 关闭（取色模式里先退出取色），`R`/`Shift+R` 转正一挡，`0` 回到原始比例与角度
         * @param event 键盘事件；带 Ctrl 的字母不归这里管，交回 Qt
         */
        void keyPressEvent(QKeyEvent *event) override;

        /**
         * @brief 右键弹出菜单
         * @param event 上下文菜单事件，取它的全局位置
         */
        void contextMenuEvent(QContextMenuEvent *event) override;

        /**
         * @brief 有东西拖到窗口上：内容可贴且没在标注时收下
         * @param event 拖动进入事件
         * @details 只做粗判（带文件、图像或文本就收）：细判要在落下那一刻读文件，拖动途中读一遍纯属白费。
         *          具体的可用性与拒绝原因由内容渲染那一层回答。
         */
        void dragEnterEvent(QDragEnterEvent *event) override;

        /// 拖动在窗口上移动：与进入同一判据，不放行的话这一拖根本落不下来
        void dragMoveEvent(QDragMoveEvent *event) override;

        /**
         * @brief 落下：把载荷与落点交给界面层贴出新图
         * @param event 放下事件
         */
        void dropEvent(QDropEvent *event) override;

    private:
        /// 按当前的缩放与旋转重算窗口尺寸与位置，保持中心不动
        void applyGeometry();

        /// 画布矩形（未旋转、已缩放）在本窗口内的位置
        [[nodiscard]] QRectF contentRect() const;

        /**
         * @brief 回到原始比例（1:1）与角度（0°），透明度与位置不动
         * @details 键盘 `0` 与左键双击共用这一个出口：两处各写一份复位就会在将来只改一处。
         *          位置不跟着改中心：复位尺寸不该把人摆好的那一站挪走。
         */
        void resetScaleAndRotation();

        /**
         * @brief 画取色模式的读数框：色块加写法文本，跟在光标旁边
         * @param painter 目标画家
         * @details 读数只在光标落在图上时出现；文本与复制出去的那一串同源（同一个 formatColor），
         *          用户看到什么就能复制到什么。
         */
        void paintColorReadout(QPainter &painter);

        /**
         * @brief 按设置里的目录与命名模板把这张贴图存一份
         * @return 成功返回写下的目录；失败返回带原因的错误
         * @details 「保存」与「在文件夹中查看」共用这一处起名与写盘：两处各写一份，
         *          将来改命名模板就会漏掉一处。
         */
        [[nodiscard]] Core::Expected<std::filesystem::path> saveWithDefaultName();

        /**
         * @brief 按现在的尺寸与贴边档把窗口摆到贴上那条边上（露出或收成一条）
         */
        void applyDockGeometry();

        /**
         * @brief 标注模式下的按键路由：查标注窗那张快捷键表并执行
         * @param event 按键事件
         * @details 与标注窗同一张表（shortcutActionFor + toolShortcutFor），两处按键含义不会各说各话。
         *          `Esc` 在这里是「收工保留」而不是关掉贴图：贴图是本来就有的东西，
         *          收起的那一步在菜单与快捷键里，不该被标注的退出键撞上。
         */
        void routeAnnotationKey(QKeyEvent *event);

        QImage                                               m_image;                               ///< 贴图内容，物理像素
        QSizeF                                               m_logicalSize;                         ///< 图片逻辑尺寸
        double                                               m_scale           = 1.0;               ///< 缩放因子
        double                                               m_rotationDegrees = 0.0;               ///< 旋转角度
        double                                               m_opacity         = 1.0;               ///< 不透明度
        bool                                                 m_mouseThrough    = false;             ///< 是否鼠标穿透
        bool                                                 m_thumbnail       = false;             ///< 缩略图模式：窗口缩成小方块，缩放等设定原样留着
        bool                                                 m_pinned          = true;              ///< 是否置顶
        int                                                  m_group           = kFirstPinGroup;    ///< 所属贴图分组，菜单与布局记录都取它
        QVector<int>                                         m_groupChoices{kFirstPinGroup};        ///< 现有分组号（升序），由管理器推过来
        bool                                                 m_colorPickMode = false;               ///< 取色模式：鼠标读像素而不是拖动
        QPointF                                              m_pickPoint;                           ///< 取色模式下光标在窗口里的位置，读数框跟它走
        ScreenEdge                                           m_edgeDock = ScreenEdge::None;         ///< 贴的是哪条屏边；None 表示没贴
        QRect                                                m_dockArea;                            ///< 贴边那一刻那块屏的可用区，收起与展开都按它算
        bool                                                 m_dockCollapsed = false;               ///< 贴边后是不是收成了那一条
        bool                                                 m_dragMoved     = false;               ///< 这一轮按下之后真拖动过没有（只点一下不吸边）
        class QMovie                                        *m_movie         = nullptr;             ///< 动图播放器；静图这张贴图时为空
        QString                                              m_sourceFile;                          ///< 动图的来源文件；进布局记录，重启后按它接上动画
        QPoint                                               m_dragOffset;                          ///< 移动时起笔点与窗口左上角的差
        std::optional<HandleKind>                            m_draggedHandle;                       ///< 正在拖的手柄，没拖时为空
        double                                               m_dragInitialScale            = 1.0;   ///< 起拖时的缩放
        double                                               m_dragInitialDistance         = 0.0;   ///< 起拖时指针到中心的距离
        bool                                                 m_annotating                  = false; ///< 正在贴图上直接标注
        double                                               m_rotationBeforeAnnotate      = 0.0;   ///< 进标注前的旋转角，退出时恢复
        bool                                                 m_dockCollapsedBeforeAnnotate = false; ///< 进标注前的贴边收起态，退出时恢复
        std::unique_ptr<Annotation::AnnotationEngine>        m_annotateEngine;                      ///< 标注引擎，首次进入标注时创建
        Annotation::AnnotationView                          *m_annotateView    = nullptr;           ///< 铺在贴图上的标注视图，本窗口的子控件
        Annotation::AnnotationToolbar                       *m_annotateToolbar = nullptr;           ///< 浮着的标注工具栏
        std::unique_ptr<Annotation::AnnotationToolbarBridge> m_annotateBridge;                      ///< 工具栏与引擎的接线
        std::unique_ptr<Ui::IconProvider>                    m_icons;                               ///< 工具栏要的图标供给器，首次进入标注时创建
        Settings::ISettingsStore                            &m_settings;                            ///< 配置来源（保存目录）
        const Platform::IPlatformIntegration                &m_integration;                         ///< 平台能力查询
    };

} // namespace GyanisScribe::Pin
