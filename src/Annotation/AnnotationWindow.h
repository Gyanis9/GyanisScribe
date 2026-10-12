/**
 * @file AnnotationWindow.h
 * @brief 标注窗口：显示选区那一块画面，带一把浮在旁边的工具栏
 * @author Gyanis
 * @date 2026-10-09
 * @version 0.0.1
 * @copyright Copyright (c) . All rights reserved.
 */

#pragma once

#include "Annotation/AnnotationToolbar.h"
#include "Annotation/AnnotationView.h"

#include <QWidget>

#include <memory>
#include <optional>

namespace GyanisScribe::Core
{
    struct CapturedImage;
} // namespace GyanisScribe::Core

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
        Copy,            ///< 复制到剪贴板
        Save,            ///< 存成文件
        Pin,             ///< 贴到屏幕上
        Ocr,             ///< 识别文字
        Undo,            ///< 撤销一步
        Redo,            ///< 重做一步
        DeleteSelection, ///< 删掉选中的那条图形
        NudgeSelection,  ///< 把选中的那条图形挪一格（按住 Shift 是大步）
    };

    /**
     * @brief 把按键翻成动作
     * @param key 事件里的键值
     * @param modifiers 当时的修饰键
     * @return 对应动作；不在表内的返回 None
     * @note Ctrl+Shift+Z 与 Ctrl+Y 都算重做：两个习惯都要照顾到，少认一个用户就以为重做坏了
     * @note 方向键（带不带 Shift）都算微调选中项，带 Ctrl 的不算：那几组合留给行内文字框按词移光标
     */
    [[nodiscard]] ShortcutAction shortcutActionFor(int key, Qt::KeyboardModifiers modifiers);

    /**
     * @brief 把字母键翻成要切过去的标注工具
     * @param key 事件里的键值
     * @param modifiers 当时的修饰键
     * @return 那一档工具；这个字母没绑工具、或按下了任何修饰键时返回空
     * @details 键位取自 kToolTable 的第四列，界面提示与分派共用同一张表。带修饰键一律不认：
     *          Ctrl+Z、Ctrl+Shift+Z 这些组合里也含字母，切工具只走裸字母才分得清。
     */
    [[nodiscard]] std::optional<ToolKind> toolShortcutFor(int key, Qt::KeyboardModifiers modifiers);

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
         * @param toolbar 工具栏，所有权交给本窗口（它是本窗口的浮层子窗）
         * @param parent Qt 父对象
         */
        AnnotationWindow(IAnnotationEngine &engine, std::unique_ptr<AnnotationToolbar> toolbar, QWidget *parent = nullptr);

        AnnotationWindow(const AnnotationWindow &)            = delete;
        AnnotationWindow &operator=(const AnnotationWindow &) = delete;

        /**
         * @brief 用刚截到的画面开启一轮标注
         * @param captured 截图产物，窗口尺寸取它的逻辑尺寸
         * @param screenAvailable 目标屏幕的可用区，工具栏摆放用它
         * @param selection 选区，逻辑坐标；工具栏贴着它摆
         * @details 会载入画布、清掉上一轮的图形，并按 placeToolbar 重新摆工具栏。
         */
        void startAnnotation(const Core::CapturedImage &captured, const QRect &screenAvailable, const QRect &selection);

        /// 浮着的工具栏，测试与调试用它核对摆放
        [[nodiscard]] AnnotationToolbar &toolbar() const noexcept;

    signals:
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

    protected:
        /**
         * @brief 键盘事件：查 shortcutActionFor 并执行
         * @param event 按键事件
         */
        void keyPressEvent(QKeyEvent *event) override;

    private:
        /// 把标注结果交出去：先收窗口与工具栏，再发对应信号，避免成品图里带上界面
        void deliver(ShortcutAction action);

        /// 把当前颜色/线宽/字号三件套下发给引擎
        void applyPenParameters();

        IAnnotationEngine                 &m_engine;         ///< 标注引擎，不拥有
        std::unique_ptr<AnnotationToolbar> m_toolbar;        ///< 浮层工具栏，本窗口拥有
        AnnotationView                    *m_view = nullptr; ///< 画布视图，父对象是本窗口
        QColor                             m_color;          ///< 画笔参数三件套：颜色
        int                                m_lineWidth{};    ///< 画笔参数三件套：线宽
        int                                m_fontSize{};     ///< 画笔参数三件套：字号
        int                                m_fillStyle{};    ///< 填充档：0 无、1 半透明、2 实心（矩形与椭圆用）
    };

} // namespace GyanisScribe::Annotation
