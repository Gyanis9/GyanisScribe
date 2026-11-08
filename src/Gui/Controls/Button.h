#pragma once

#include <QPushButton>
#include <QToolButton>

namespace GyanisScribe::Gui
{
    /**
     * @brief 自绘连续曲率底与状态的按钮
     * @details 填充与描边由 ControlPalette 按状态映射；QSS 只留透明占位，
     *          未迁移的 QPushButton 仍走原生样式表规则。
     */
    class Button : public QPushButton
    {
    public:
        /**
         * @brief 构造无文字的按钮
         * @param parent Qt 父对象
         */
        explicit Button(QWidget *parent = nullptr);

        /**
         * @brief 构造带文字的按钮
         * @param text 按钮文字
         * @param parent Qt 父对象
         */
        explicit Button(const QString &text, QWidget *parent = nullptr);

    protected:
        /**
         * @brief 先画连续曲率底，再只画标签
         * @param event 绘制事件
         * @note 不调 QPushButton::paintEvent：没有样式表时原生风格会整块补一层不透明底，
         *       把自绘填充盖掉；标签仍由样式引擎画，QSS 的文字色与图标不受影响
         */
        void paintEvent(QPaintEvent *event) override;
    };

    /**
     * @brief 自绘连续曲率底与状态的工具按钮
     * @details 未选中且无状态时填充透明，给标注工具栏这类密集排布保留可视留白。
     */
    class ToolButton : public QToolButton
    {
    public:
        /**
         * @brief 构造工具按钮
         * @param parent Qt 父对象
         */
        explicit ToolButton(QWidget *parent = nullptr);

    protected:
        /**
         * @brief 先画连续曲率底，再只画标签
         * @param event 绘制事件
         */
        void paintEvent(QPaintEvent *event) override;
    };
} // namespace GyanisScribe::Gui
