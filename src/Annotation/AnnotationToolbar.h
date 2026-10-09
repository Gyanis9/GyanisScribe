/**
 * @file AnnotationToolbar.h
 * @brief 标注工具栏：工具、颜色、线宽字号与撤销重做等动作
 * @author Gyanis
 * @date 2026-10-09
 * @version 0.0.1
 * @copyright Copyright (c) . All rights reserved.
 */

#pragma once

#include "Annotation/IAnnotationEngine.h"
#include "Ui/DesignTokens.h"
#include "Ui/IconProvider.h"

#include <QColor>
#include <QWidget>

#include <array>
#include <string_view>

class QComboBox;
class QSlider;
class QToolButton;

namespace GyanisScribe::Annotation
{

    /// 工具栏上的工具按钮数量，与 ToolKind 的八个工具同解
    inline constexpr std::size_t kToolButtonCount = 8;

    /**
     * @brief 标注工具栏控件
     * @details 按钮顺序按需求第六节：工具组 → 颜色 → 线宽/字号 → 撤销/重做 → 复制/保存/贴图/OCR。
     *          本控件不直接改画布，只把用户意图发成信号，由宿主窗口转给引擎——
     *          这样工具栏不必拖着引擎一起走，贴图编辑要复用它也不需要改这里。
     * @note 每个图标按钮都带 tooltip 与无障碍名称，这是需求的可访问性硬要求，不是装饰。
     */
    class AnnotationToolbar : public QWidget
    {
        Q_OBJECT

    public:
        /**
         * @brief 构造工具栏
         * @param icons 图标供给器，生命周期由应用保证，必须活过本控件
         * @param parent Qt 父对象
         */
        explicit AnnotationToolbar(const Ui::IconProvider &icons, QWidget *parent = nullptr);

        AnnotationToolbar(const AnnotationToolbar &)            = delete;
        AnnotationToolbar &operator=(const AnnotationToolbar &) = delete;

        /**
         * @brief 外部（键盘快捷键）切了工具时同步按钮态
         * @param toolKind 当前生效的工具
         * @note 引擎才是真源：快捷键改了工具必须回灌到这里，否则按钮亮着一个、画的却是另一个
         */
        void setActiveTool(ToolKind toolKind);

        /**
         * @brief 同步撤销/重做按钮的可用态
         * @param canUndo 引擎是否还有可撤销的一步
         * @param canRedo 引擎还有可重做的一步
         */
        void setUndoRedoAvailable(bool canUndo, bool canRedo);

        /// 工具按钮数量，布局与用例据此核对「八个工具齐全」
        [[nodiscard]] static int toolButtonCount();

        /// 色块按钮数量：八个预设色加一个自定义取色
        [[nodiscard]] static int colorButtonCount();

        /// 当前选中的颜色，含自定义取回来的那一个
        [[nodiscard]] QColor currentColor() const;

        /// 当前线宽（逻辑像素），取值只会落在 Ui::kAnnotationWidths 的阶梯上
        [[nodiscard]] int currentWidth() const;

        /// 当前字号，取值只会落在 Ui::kFontSizes 的阶梯上
        [[nodiscard]] int currentFontSize() const;

        /**
         * @brief 指定序号的工具按钮，用例与调试用它核对状态
         * @param index 按钮序号，越界返回空指针
         */
        [[nodiscard]] QToolButton *toolButtonAt(std::size_t index) const;

    signals:
        /// 用户切了工具
        void toolChanged(ToolKind toolKind);
        /// 用户切了颜色
        void colorChanged(const QColor &color);
        /// 用户改了线宽
        void widthChanged(int width);
        /// 用户改了字号
        void fontSizeChanged(int fontSize);
        /// 撤销
        void undoClicked();
        /// 重做
        void redoClicked();
        /// 复制到剪贴板
        void copyClicked();
        /// 存成文件
        void saveClicked();
        /// 贴到屏幕上
        void pinClicked();
        /// 送去做文字识别
        void ocrClicked();

    private:
        /**
         * @brief 建一个带图标、tooltip 与无障碍名称的按钮
         * @param iconName SVG 文件名去掉扩展名，例如 "Rectangle"
         * @param label 中文名，同时用作 tooltip 与无障碍名称
         * @param checkable 是否可保持按下态（工具与色块要用，动作按钮不用）
         * @return 归本控件所有的按钮
         */
        [[nodiscard]] QToolButton *makeButton(std::string_view iconName, const QString &label, bool checkable);

        /// 打开系统取色对话框并把选中的色并进色块行；用户取消时什么都不发
        void chooseCustomColor();

        const Ui::IconProvider                                  &m_icons;                       ///< 图标出口，不拥有
        std::array<QToolButton *, kToolButtonCount>              m_toolButtons{};               ///< 八个工具按钮，按 ToolKind 顺序
        std::array<QToolButton *, Ui::kAnnotationPalette.size()> m_colorButtons{};              ///< 八个预设色块
        std::array<ToolKind, kToolButtonCount>                   m_toolKinds{};                 ///< 按钮序号到工具的映射，反向查也用它
        QToolButton                                             *m_customColorButton = nullptr; ///< 自定义取色按钮
        QToolButton                                             *m_undoButton        = nullptr; ///< 撤销按钮
        QToolButton                                             *m_redoButton        = nullptr; ///< 重做按钮
        QSlider                                                 *m_widthSlider       = nullptr; ///< 线宽滑块，档位对齐 kAnnotationWidths
        QComboBox                                               *m_fontSizeCombo     = nullptr; ///< 字号下拉，取值对齐 kFontSizes
        QColor                                                   m_currentColor;                ///< 当前颜色，初值取色板第一格
        ToolKind                                                 m_activeTool;                  ///< 当前工具，用来挡住「重复点同一个工具」的二次信号
    };

} // namespace GyanisScribe::Annotation
