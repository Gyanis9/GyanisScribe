/**
 * @file AnnotationWindow.h
 * @brief 标注窗口：显示选区那一块画面，带一把浮在旁边的工具栏
 * @author Gyanis
 * @date 2026-10-09
 * @version 0.0.1
 * @copyright Copyright (c) . All rights reserved.
 */

#pragma once

#include "Annotation/AnnotationToolbarBridge.h"
#include "Annotation/AnnotationToolbar.h"
#include "Annotation/AnnotationView.h"

#include <QWidget>

#include <memory>
#include <optional>

namespace GyanisScribe::Base
{
    struct CapturedImage;
} // namespace GyanisScribe::Base

namespace GyanisScribe::Settings
{
    class ISettingsStore;
} // namespace GyanisScribe::Settings

namespace GyanisScribe::Annotation
{
    class IAnnotationEngine;

    /**
     * @brief 键盘快捷键映射到的动作
     * @details 用例按这张表逐个钉：界面上的按键含义一旦改了，用户肌肉记忆就改了。
     */
    enum class ShortcutAction
    {
        None,            ///< 不归标注窗口管，交回给 Qt 继续分发
        Close,           ///< 关闭并丢弃标注
        Copy,            ///< 复制整张成品到剪贴板
        Save,            ///< 存成文件
        Pin,             ///< 贴到屏幕上
        Ocr,             ///< 识别文字
        Print,           ///< 打印（不关窗：打印取消或打完，编辑现场都还在）
        Undo,            ///< 撤销一步
        Redo,            ///< 重做一步
        DeleteSelection, ///< 删掉选中的图形（多选时整组）
        NudgeSelection,  ///< 把选中的图形挪一格（按住 Shift 是大步）
        CopyShape,       ///< 把选中的图形复制进引擎内的剪贴板
        PasteShape,      ///< 把剪贴板里的图形粘到画布上
        RaiseSelection,  ///< 选中的图形上移一层
        LowerSelection,  ///< 选中的图形下移一层
        BringToFront,    ///< 选中的图形置顶
        SendToBack,      ///< 选中的图形置底
        Refresh,         ///< 刷新截图：重抓这块区域换底图，标注留着
    };

    /**
     * @brief 把按键翻成动作
     * @param key 事件里的键值
     * @param modifiers 当时的修饰键
     * @return 对应动作；不在表内的返回 None
     * @note Ctrl+Shift+Z 与 Ctrl+Y 都算重做：两个习惯都要照顾到，少认一个用户就以为重做坏了
     * @note 方向键（带不带 Shift）是微调；Ctrl+Shift+↑/↓ 改层序、Ctrl+Shift+Home/End 置底/置顶，
     *       带 Ctrl 的其它方向键交回 Qt：行内文字框在它那儿是按词移光标。
     * @note Ctrl+C 的「没选中就复制整图」由窗口分派时判：快捷键表只报意图，看状态的话表就不好逐条钉
     */
    [[nodiscard]] ShortcutAction shortcutActionFor(int key, Qt::KeyboardModifiers modifiers);

    /**
     * @brief 把按键翻成要切过去的标注工具
     * @param key 事件里的键值
     * @param modifiers 当时的修饰键
     * @param settings 配置存储：键位从 Ui/AppShortcuts.h 那份清册现读
     * @return 那一档工具；没有一档认这颗键时返回空
     * @details 整颗组合相等才算命中，所以默认那十三颗裸字母照旧，而 Ctrl+Z 这类组合不会被当成换工具。
     *          键位只有清册一处：按钮提示读的是同一处，用户在设置页换过一颗，两边一起跟上。
     *          宿主先按 shortcutActionFor() 分派，撞进既有组合键的那一次归组合键那一档。
     */
    [[nodiscard]] std::optional<ToolKind> toolShortcutFor(int key, Qt::KeyboardModifiers modifiers, const Settings::ISettingsStore &settings);

    /**
     * @brief 标注窗口
     * @details 窗口本身只放视图；工具栏是独立的浮窗，用 placeToolbar 摆在选区旁边并避开屏幕边缘。
     *          工具栏的信号与快捷键都汇到这里的私有槽，再由本窗口发对外信号——
     *          动作只有「一处入口」，AppController 不需要知道用户是点按钮还是按键盘。
     */
    class AnnotationWindow : public QWidget
    {
        Q_OBJECT

    public:
        /**
         * @brief 构造窗口
         * @param engine 标注引擎，所有权在宿主
         * @param settings 配置存储，字母键位从 Ui/AppShortcuts.h 那份清册现读；生命周期必须长于本窗口
         * @param toolbar 工具栏，所有权交给本窗口（它是本窗口的浮层子窗）
         * @param parent Qt 父对象
         */
        AnnotationWindow(IAnnotationEngine &engine, Settings::ISettingsStore &settings, std::unique_ptr<AnnotationToolbar> toolbar, QWidget *parent = nullptr);

        AnnotationWindow(const AnnotationWindow &)            = delete;
        AnnotationWindow &operator=(const AnnotationWindow &) = delete;

        /**
         * @brief 用刚截到的画面开启一轮标注
         * @param captured 截图产物，窗口尺寸取它的逻辑尺寸
         * @param screenAvailable 目标屏幕的可用区，工具栏摆放用它
         * @param selection 选区，逻辑坐标；工具栏贴着它摆
         * @details 会载入画布、清掉上一轮的图形，并按 placeToolbar 重新摆工具栏。
         */
        void startAnnotation(const Base::CapturedImage &captured, const QRect &screenAvailable, const QRect &selection);

        /// 浮着的工具栏，测试与调试用它核对摆放
        [[nodiscard]] AnnotationToolbar &toolbar() const noexcept;

        /**
         * @brief 把重新抓到的一帧换上：底图换了，标注、选中与撤销历史都留着
         * @param captured 重抓的产物，应与开窗时那一帧同形（同一块区域再抓一帧）
         * @details 引擎的 refreshCanvas 就是这个语义；与 loadCanvas 分开正是为了不把标注清掉。
         */
        void refreshCapture(const Base::CapturedImage &captured);

    signals:
        /**
         * @brief 请求重抓一块区域（F5 刷新截图）
         * @param screenName 屏幕名
         * @param region 该屏内的逻辑区域
         * @note 窗口自己不碰抓屏来源：把「哪块屏、哪块区域」报出去，由宿主接上采集服务再送回来
         */
        void refreshRequested(const QString &screenName, const QRect &region);
        /**
         * @brief 请求把标注结果送进剪贴板
         * @param image 烘焙好的图，物理像素
         */
        void copyRequested(const QImage &image);
        /**
         * @brief 请求把标注结果存成文件
         * @param image 烘焙好的图
         */
        void saveRequested(const QImage &image);
        /**
         * @brief 请求把标注结果贴成浮窗
         * @param image 烘焙好的图
         */
        void pinRequested(const QImage &image);
        /**
         * @brief 请求对标注结果做文字识别
         * @param image 烘焙好的图
         */
        void ocrRequested(const QImage &image);
        /**
         * @brief 请求识别画面里的二维码/条形码
         * @param image 当前成品的烘焙图，物理像素
         * @note 与别的出口不同：识别不关窗——多半只是想知道「这里头是什么」，
         *       认出与否都该留着编辑现场（与打印同一条口径）
         */
        void scanRequested(const QImage &image);
        /**
         * @brief 请求打印标注结果
         * @param image 烘焙好的图
         * @note 与其它出口不同：打印不关窗，用户取消打印或在打印之后接着改，现场都不丢
         */
        void printRequested(const QImage &image);

    protected:
        /**
         * @brief 键盘事件：查 shortcutActionFor 并执行
         * @param event 按键事件
         */
        void keyPressEvent(QKeyEvent *event) override;

    private:
        /// 把标注结果交出去：先收窗口与工具栏，再发对应信号，避免成品图里带上界面
        void deliver(ShortcutAction action);

        /// 按记下的选区与屏幕可用区把工具栏摆一次；可见段收放（contextLayoutChanged）后也走这一处
        void placeToolbarForSelection();

        IAnnotationEngine                       &m_engine;                ///< 标注引擎，不拥有
        Settings::ISettingsStore                &m_settings;              ///< 字母键位的读端（Ui/AppShortcuts.h 那份清册），不拥有
        std::unique_ptr<AnnotationToolbar>       m_toolbar;               ///< 浮层工具栏，本窗口拥有
        std::unique_ptr<AnnotationToolbarBridge> m_toolbarBridge;         ///< 工具栏与引擎的接线：样式、工具、撤销重做都在桥里
        AnnotationView                          *m_view = nullptr;        ///< 画布视图，父对象是本窗口
        QString                                  m_refreshScreenName;     ///< 本轮截图来自哪块屏（F5 重抓用它）
        QRect                                    m_refreshRegion;         ///< 本轮截的是屏内哪块区域（F5 重抓用它）
        QRect                                    m_placedScreenAvailable; ///< 本轮摆放用的屏幕可用区：可见段变化后重摆要同一份
        QRect                                    m_placedSelection;       ///< 本轮摆放参照的选区：可见段变化后重摆要同一份
    };

} // namespace GyanisScribe::Annotation
