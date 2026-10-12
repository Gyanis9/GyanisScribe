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
#include <tuple>
#include <vector>

class QButtonGroup;
class QComboBox;
class QFontComboBox;
class QHBoxLayout;
class QSlider;
class QToolButton;

namespace GyanisScribe::Settings
{
    class ISettingsStore;
} // namespace GyanisScribe::Settings

namespace GyanisScribe::Annotation
{

    /**
     * @brief 工具按钮的登记表：顺序、图标名、中文名与字母快捷键一处对齐
     * @details 顺序必须与 ToolKind 的声明顺序一致，用例按序号逐个核图标名与标签。
     *          放在头文件是因为按钮数组的大小由它算：写死一个数量时，加一档工具就会写出数组外
     *          （Debug 档当场 fast-fail，Release 档静默越界）。
     *          第四列是标注窗的字母快捷键，取英文工具名首字母且十一档互不撞：橡皮擦的英文首字母
     *          E 已被椭圆占着，取的是中文首字母 X；按钮提示与按键分派都读这一列，
     *          另记一张键位表就会与按钮各说各话。
     */
    inline constexpr std::array<std::tuple<ToolKind, std::string_view, std::string_view, std::string_view>, 12> kToolTable = {
            std::tuple{ToolKind::Rectangle, "Rectangle", "矩形", "R"}, std::tuple{ToolKind::Ellipse, "Ellipse", "椭圆", "E"},
            std::tuple{ToolKind::Arrow, "Arrow", "箭头", "A"},         std::tuple{ToolKind::Pen, "Pen", "画笔", "P"},
            std::tuple{ToolKind::Highlight, "Highlight", "高亮", "H"}, std::tuple{ToolKind::Mosaic, "Mosaic", "马赛克", "M"},
            std::tuple{ToolKind::Text, "Text", "文字", "T"},           std::tuple{ToolKind::Number, "Number", "序号", "N"},
            std::tuple{ToolKind::Line, "Line", "直线", "L"},           std::tuple{ToolKind::Blur, "Blur", "高斯模糊", "B"},
            std::tuple{ToolKind::Eraser, "Eraser", "橡皮擦", "X"},     std::tuple{ToolKind::Polyline, "Polyline", "折线", "G"},
    };

    /// 工具栏上的工具按钮数量，由登记表算出：加一档工具只改表，数组跟着长
    inline constexpr std::size_t kToolButtonCount = kToolTable.size();

    /// 自定义取色按钮在互斥组里的编号：不能紧跟在预设色板后面，最近使用颜色要往那段里插
    inline constexpr int kCustomColorButtonId = 100;

    /**
     * @brief 标注工具栏控件
     * @details 按钮顺序按需求第六节：工具组 → 颜色 → 线宽/字号 → 撤销/重做 → 复制/保存/贴图/OCR。
     *          本控件不直接改画布，只把用户意图发成信号，由宿主窗口转给引擎——
     *          这样工具栏不必拖着引擎一起走，将来别处要复用同一排按钮也不必改这里。
     * @note 每个图标按钮都带 tooltip 与无障碍名称，这是需求的可访问性硬要求，不是装饰。
     */
    class AnnotationToolbar : public QWidget
    {
        Q_OBJECT

    public:
        /**
         * @brief 构造工具栏
         * @param settings 配置存储，最近使用颜色读写它；生命周期由应用保证
         * @param icons 图标供给器，生命周期由应用保证，必须活过本控件
         * @param parent Qt 父对象
         */
        explicit AnnotationToolbar(Settings::ISettingsStore &settings, const Ui::IconProvider &icons, QWidget *parent = nullptr);

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

        /// 工具按钮数量，由登记表算出：布局与用例据此核对「每一档工具都有一枚按钮」
        [[nodiscard]] static int toolButtonCount();

        /// 色块按钮数量：八个预设色加一个自定义取色
        [[nodiscard]] int colorButtonCount() const;

        /// 当前选中的颜色，含自定义取回来的那一个
        [[nodiscard]] QColor currentColor() const;

        /// 当前线宽（逻辑像素），取值只会落在 Ui::kAnnotationWidths 的阶梯上
        [[nodiscard]] int currentWidth() const;

        /// 当前字号，取值只会落在 Ui::kFontSizes 的阶梯上
        [[nodiscard]] int currentFontSize() const;


        /// 当前填充档：0 无、1 半透明、2 实心（矩形与椭圆用；新建的图形按它上笔刷）
        [[nodiscard]] int currentFillStyle() const;

        /// 当前线型档：0 实线、1 虚线、2 点线（描边类工具用；新建的图形按它落笔）
        [[nodiscard]] int currentLineStyle() const;

        /// 按 实线 → 虚线 → 点线 走一档；按钮与用例共用这一个入口
        void cycleLineStyle();

        /// 按 无 → 半透明 → 实心 → 无 走一档；按钮与用例共用这一个入口
        void cycleFillStyle();

        /// 当前是不是双向箭头（箭头工具用；新建的箭头按它两端画头）
        [[nodiscard]] bool currentDoubleHeaded() const;

        /**
         * @brief 开/关双向箭头，按钮态、提示与信号一次对齐
         * @param enabled true 表示两端各画一个箭头
         */
        void setDoubleHeaded(bool enabled);

        /// 当前马赛克粒度（方块边长，逻辑像素），取值只会落在 kMosaicBlockSizes 的阶梯上
        [[nodiscard]] int currentMosaicBlockSize() const;

        /// 按 中 → 粗 → 细 → 中 走一档；按钮与用例共用这一个入口
        void cycleMosaicBlockSize();

        /// 当前高亮透明度（0-255），取值只会落在 kHighlightAlphas 的阶梯上
        [[nodiscard]] int currentHighlightAlpha() const;

        /// 当前高亮加宽倍数，取值只会落在 kHighlightWidthFactors 的阶梯上
        [[nodiscard]] int currentHighlightWidthFactor() const;

        /// 按 中 → 重 → 轻 → 中 走一档；按钮与用例共用这一个入口
        void cycleHighlightAlpha();

        /// 高亮加宽走一档（窄 → 中 → 宽），并发出 highlightWidthFactorChanged
        void cycleHighlightWidthFactor();

        /// 当前箭头头部款式：0 实心、1 空心、2 开口两翼（箭头工具用；新建的箭头按它画头）
        [[nodiscard]] int currentArrowHeadStyle() const;

        /// 箭头款式走一档（实心 → 空心 → 开口），并发出 arrowHeadStyleChanged
        void cycleArrowHeadStyle();

        /// 当前箭头头部大小：头长相对线宽的倍数，取值只会落在 kArrowHeadScales 的阶梯上
        [[nodiscard]] qreal currentArrowHeadScale() const;

        /// 箭头大小走一档（中 → 大 → 小 → 中），并发出 arrowHeadScaleChanged
        void cycleArrowHeadScale();

        /// 当前文字字体族，取字体下拉的当前项（非空；跟随应用默认时就是默认家族名）
        [[nodiscard]] QString currentFontFamily() const;

        /// 当前文字是不是粗体
        [[nodiscard]] bool currentTextBold() const;

        /**
         * @brief 开/关文字粗体，按钮态、提示与信号一次对齐
         * @param enabled true 表示粗体
         */
        void setTextBold(bool enabled);

        /// 当前文字是不是斜体
        [[nodiscard]] bool currentTextItalic() const;

        /**
         * @brief 开/关文字斜体，按钮态、提示与信号一次对齐
         * @param enabled true 表示斜体
         */
        void setTextItalic(bool enabled);

        /// 当前文字是不是反色描边
        [[nodiscard]] bool currentTextOutline() const;

        /**
         * @brief 开/关文字反色描边，按钮态、提示与信号一次对齐
         * @param enabled true 表示字形围一圈与字色反差的轮廓
         */
        void setTextOutline(bool enabled);

        /// 当前文字是不是反色底板
        [[nodiscard]] bool currentTextBackground() const;

        /**
         * @brief 开/关文字反色底板，按钮态、提示与信号一次对齐
         * @param enabled true 表示字形后面垫一块与字色反差的半透明圆角板
         */
        void setTextBackground(bool enabled);

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
        /// 用户改了填充档
        void fillStyleChanged(int fillStyle);
        /// 用户改了线型
        void lineStyleChanged(int style);
        /// 用户开了/关了双向箭头
        void doubleHeadedChanged(bool enabled);
        /// 用户改了马赛克粒度
        void mosaicBlockSizeChanged(int blockSize);
        /// 用户改了高亮透明度
        void highlightAlphaChanged(int alpha);

        /// 用户改了高亮加宽倍数
        void highlightWidthFactorChanged(int factor);
        /// 用户改了箭头头部款式
        void arrowHeadStyleChanged(int style);
        /// 用户改了箭头头部大小
        void arrowHeadScaleChanged(qreal scale);
        /// 用户换了文字字体族
        void fontFamilyChanged(const QString &family);
        /// 用户开了/关了文字粗体
        void textBoldChanged(bool enabled);
        /// 用户开了/关了文字斜体
        void textItalicChanged(bool enabled);
        /// 用户开了/关了文字反色描边
        void textOutlineChanged(bool enabled);
        /// 用户开了/关了文字反色底板
        void textBackgroundChanged(bool enabled);
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

        /**
         * @brief 把配置里存的默认线宽与字号落到滑块和下拉框上
         * @details 存的值不在阶梯里时记一条警告再回默认档：就近取一档会让用户以为存的那档生效了。
         */
        void applyStoredDefaults();

        /// 把当前的线宽与字号写回配置，供下次打开标注窗作起点
        void rememberDefaults();

        /**
         * @brief 按 m_recentColors 重建最近使用的色块
         * @details 先摘旧按钮再插新的，编号从 kAnnotationPalette.size() 起排；
         *          自定义按钮固定用 kCustomColorButtonId，才不会和插进来的色块撞编号。
         */
        void rebuildRecentColors();

        Settings::ISettingsStore  &m_settings;                    ///< 最近使用颜色的读写去处
        std::vector<QColor>        m_recentColors;                ///< 最近用过的自定义颜色，首位最新
        std::vector<QToolButton *> m_recentButtons;               ///< 与 m_recentColors 同序的色块按钮
        int                        m_recentInsertIndex = 0;       ///< 色块插进布局的位置（预设色板之后、自定义按钮之前）
        class QHBoxLayout         *m_layout            = nullptr; ///< 工具栏主布局，重建色块时要往这里插
        QButtonGroup              *m_colorGroup        = nullptr; ///< 色块的互斥组，重建最近使用时要同步它的成员

        const Ui::IconProvider                                  &m_icons;                                                             ///< 图标出口，不拥有
        std::array<QToolButton *, kToolButtonCount>              m_toolButtons{};                                                     ///< 工具按钮，按 ToolKind 顺序
        std::array<QToolButton *, Ui::kAnnotationPalette.size()> m_colorButtons{};                                                    ///< 八个预设色块
        std::array<ToolKind, kToolButtonCount>                   m_toolKinds{};                                                       ///< 按钮序号到工具的映射，反向查也用它
        QToolButton                                             *m_customColorButton    = nullptr;                                    ///< 自定义取色按钮
        QToolButton                                             *m_undoButton           = nullptr;                                    ///< 撤销按钮
        QToolButton                                             *m_redoButton           = nullptr;                                    ///< 重做按钮
        QSlider                                                 *m_widthSlider          = nullptr;                                    ///< 线宽滑块，档位对齐 kAnnotationWidths
        QComboBox                                               *m_fontSizeCombo        = nullptr;                                    ///< 字号下拉，取值对齐 kFontSizes
        QToolButton                                             *m_fillButton           = nullptr;                                    ///< 填充档按钮，点一下走一档
        int                                                      m_fillStyle            = 0;                                          ///< 当前填充档：0 无、1 半透明、2 实心
        QToolButton                                             *m_lineStyleButton      = nullptr;                                    ///< 线型按钮，点一下走一档
        int                                                      m_lineStyle            = static_cast<int>(kDefaultLineStyleStep);    ///< 当前线型档，值表是 kAnnotationLineStyles
        QToolButton                                             *m_doubleArrowButton    = nullptr;                                    ///< 双向箭头开关按钮，按下表示两端有头
        bool                                                     m_doubleHeaded         = false;                                      ///< 当前是不是双向箭头
        QToolButton                                             *m_mosaicGrainButton    = nullptr;                                    ///< 马赛克粒度按钮，点一下走一档
        int                                                      m_mosaicBlockStep      = static_cast<int>(kDefaultMosaicBlockStep);  ///< 当前粒度档下标，值表是 kMosaicBlockSizes
        QToolButton                                             *m_highlightAlphaButton = nullptr;                                    ///< 高亮透明度按钮，点一下走一档
        int                                                      m_highlightAlphaStep = static_cast<int>(kDefaultHighlightAlphaStep); ///< 当前透明度档下标，值表是 kHighlightAlphas
        QToolButton                                             *m_highlightWidthButton = nullptr;                                    ///< 高亮加宽按钮，点一下走一档
        int            m_highlightWidthStep   = static_cast<int>(kDefaultHighlightWidthStep); ///< 当前加宽档下标，值表是 kHighlightWidthFactors
        QToolButton   *m_arrowHeadStyleButton = nullptr;                                      ///< 箭头款式按钮，点一下走一档
        int            m_arrowHeadStyle       = kArrowHeadFilled;                             ///< 当前头部款式：0 实心、1 空心、2 开口两翼
        QToolButton   *m_arrowHeadScaleButton = nullptr;                                      ///< 箭头大小按钮，点一下走一档
        int            m_arrowHeadScaleStep   = static_cast<int>(kDefaultArrowHeadScaleStep); ///< 当前大小档下标，值表是 kArrowHeadScales
        QFontComboBox *m_fontCombo            = nullptr;                                      ///< 文字字体族下拉，列系统字体
        QToolButton   *m_boldButton           = nullptr;                                      ///< 文字粗体开关，按下表示粗
        QToolButton   *m_italicButton         = nullptr;                                      ///< 文字斜体开关，按下表示斜
        QToolButton   *m_outlineButton        = nullptr;                                      ///< 文字描边开关，按下表示围一圈反色轮廓
        QToolButton   *m_backgroundButton     = nullptr;                                      ///< 文字底板开关，按下表示垫一块反色圆角板
        QColor         m_currentColor;                                                        ///< 当前颜色，初值取色板第一格
        bool           m_stateIsInitial = true;                                               ///< 还在构造期：起点值不回写配置，坏值要留在文件里让人看得见
        ToolKind       m_activeTool;                                                          ///< 当前工具，用来挡住「重复点同一个工具」的二次信号
    };

} // namespace GyanisScribe::Annotation
