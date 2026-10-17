/**
 * @file AnnotationToolbar.h
 * @brief 标注工具栏：工具、颜色、线宽字号与撤销重做等动作
 * @author Gyanis
 * @date 2026-10-09
 * @version 0.0.1
 * @copyright Copyright (c) . All rights reserved.
 */

#pragma once

#include "Core/Annotation/IAnnotationEngine.h"
#include "Gui/Theme/DesignTokens.h"
#include "Gui/Resources/IconProvider.h"

#include <QColor>
#include <QHash>
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

namespace GyanisScribe::Core
{
    class ISettingsStore;
} // namespace GyanisScribe::Core

namespace GyanisScribe::Gui
{

    /**
     * @brief 工具按钮的登记表：顺序、图标名、中文名与界面按键标识一处对齐
     * @details 顺序必须与 Core::ToolKind 的声明顺序一致，用例按序号逐个核图标名与标签。
     *          放在头文件是因为按钮数组的大小由它算：写死一个数量时，加一档工具就会写出数组外
     *          （Debug 档当场 fast-fail，Release 档静默越界）。
     *          第四列是 Gui/Controls/AppShortcuts.h 那份清册里的标识：认哪颗键（默认那颗与用户改后的那颗）
     *          只住在清册一处，这里再存一份字母就会出现「按键改了、按钮提示还是旧的」。
     */
    inline constexpr std::array<std::tuple<Core::ToolKind, std::string_view, std::string_view, std::string_view>, 14> kToolTable = {
            std::tuple{Core::ToolKind::Select, "CursorHighlight", "选择", "Annotation/Select"},
            std::tuple{Core::ToolKind::Rectangle, "Rectangle", "矩形", "Annotation/Rectangle"},
            std::tuple{Core::ToolKind::Ellipse, "Ellipse", "椭圆", "Annotation/Ellipse"},
            std::tuple{Core::ToolKind::Arrow, "Arrow", "箭头", "Annotation/Arrow"},
            std::tuple{Core::ToolKind::Pen, "Pen", "画笔", "Annotation/Pen"},
            std::tuple{Core::ToolKind::Highlight, "Highlight", "高亮", "Annotation/Highlight"},
            std::tuple{Core::ToolKind::Mosaic, "Mosaic", "马赛克", "Annotation/Mosaic"},
            std::tuple{Core::ToolKind::Text, "Text", "文字", "Annotation/Text"},
            std::tuple{Core::ToolKind::Number, "Number", "序号", "Annotation/Number"},
            std::tuple{Core::ToolKind::Line, "Line", "直线", "Annotation/Line"},
            std::tuple{Core::ToolKind::Blur, "Blur", "高斯模糊", "Annotation/Blur"},
            std::tuple{Core::ToolKind::Eraser, "Eraser", "橡皮擦", "Annotation/Eraser"},
            std::tuple{Core::ToolKind::Polyline, "Polyline", "折线", "Annotation/Polyline"},
            std::tuple{Core::ToolKind::Magnifier, "Magnifier", "放大镜", "Annotation/Magnifier"},
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
        explicit AnnotationToolbar(Core::ISettingsStore &settings, const Gui::IconProvider &icons, QWidget *parent = nullptr);

        AnnotationToolbar(const AnnotationToolbar &)            = delete;
        AnnotationToolbar &operator=(const AnnotationToolbar &) = delete;

        /**
         * @brief 外部（键盘快捷键）切了工具时同步按钮态
         * @param toolKind 当前生效的工具
         * @note 引擎才是真源：快捷键改了工具必须回灌到这里，否则按钮亮着一个、画的却是另一个
         * @note 顺带按这一档重收可见段：可见集合真的变了才会发 contextLayoutChanged
         */
        void setActiveTool(Core::ToolKind toolKind);

        /**
         * @brief 同步撤销/重做按钮的可用态
         * @param canUndo 引擎是否还有可撤销的一步
         * @param canRedo 引擎还有可重做的一步
         */
        void setUndoRedoAvailable(bool canUndo, bool canRedo);

        /// 工具按钮数量，由登记表算出：布局与用例据此核对「每一档工具都有一枚按钮」
        [[nodiscard]] static int toolButtonCount();

        /// 色块按钮数量：色板那几格加一个自定义取色（色板是几格由配置里那一格定）
        [[nodiscard]] int colorButtonCount() const;

        /**
         * @brief 把色板里的一格挪到另一格那儿，并把新的顺序写回配置
         * @param from 拖走的那一格序号
         * @param to 落点那一格序号（都以挪走之前的表为准）
         * @details 越界、原地不动与「名单被别处改坏」都不写配置：前者是手抖，最后一写下去会把工具栏
         *          正在摆的那一排顶成另一份。公开这一格是给判据用的——拖动要三个鼠标事件与真实几何，
         *          而那一步是那一串的最后一下。
         */
        void requestPaletteMove(int from, int to);

        /// 当前选中的颜色，含自定义取回来的那一个
        [[nodiscard]] QColor currentColor() const;

        /// 当前线宽（逻辑像素），取值只会落在 Gui::kAnnotationWidths 的阶梯上
        [[nodiscard]] int currentWidth() const;

        /// 当前字号，取值只会落在 Gui::kFontSizes 的阶梯上
        [[nodiscard]] int currentFontSize() const;


        /// 当前填充档：0 无、1 半透明、2 实心（矩形与椭圆用；新建的图形按它上笔刷）
        [[nodiscard]] int currentFillStyle() const;

        /// 当前是不是圆角矩形（矩形工具用；新建的矩形按它切角）
        [[nodiscard]] bool currentRoundedRect() const;

        /**
         * @brief 开/关圆角矩形，按钮态、提示与信号一次对齐
         * @param enabled true 表示四角切圆
         */
        void setRoundedRect(bool enabled);

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

        /// 当前是不是反向箭头（箭头画在起点那一头）
        [[nodiscard]] bool currentArrowReversed() const;

        /// 开/关反向箭头：把箭头换到起点那一头
        void setArrowReversed(bool enabled);

        /// 当前是不是椭圆档（马赛克/高斯模糊/高亮/橡皮擦用；新建的图形按它选形状）
        [[nodiscard]] bool currentElliptical() const;

        /**
         * @brief 开/关椭圆档，按钮态、提示与信号一次对齐
         * @param enabled true 表示区域类收进内切椭圆、笔迹类换圆头圆角
         */
        void setElliptical(bool enabled);

        /// 当前马赛克粒度（方块边长，逻辑像素），取值只会落在 Core::kMosaicBlockSizes 的阶梯上
        [[nodiscard]] int currentMosaicBlockSize() const;

        /// 按 中 → 粗 → 细 → 中 走一档；按钮与用例共用这一个入口
        void cycleMosaicBlockSize();

        /// 当前高亮透明度（0-255），取值只会落在 Core::kHighlightAlphas 的阶梯上
        [[nodiscard]] int currentHighlightAlpha() const;

        /// 当前高亮加宽倍数，取值只会落在 Core::kHighlightWidthFactors 的阶梯上
        [[nodiscard]] int currentHighlightWidthFactor() const;

        /// 按 中 → 重 → 轻 → 中 走一档；按钮与用例共用这一个入口
        void cycleHighlightAlpha();

        /// 高亮加宽走一档（窄 → 中 → 宽），并发出 highlightWidthFactorChanged
        void cycleHighlightWidthFactor();

        /// 当前箭头头部款式：0 实心、1 空心、2 开口两翼（箭头工具用；新建的箭头按它画头）
        [[nodiscard]] int currentArrowHeadStyle() const;

        /// 箭头款式走一档（实心 → 空心 → 开口），并发出 arrowHeadStyleChanged
        void cycleArrowHeadStyle();

        /// 当前箭头头部大小：头长相对线宽的倍数，取值只会落在 Core::kArrowHeadScales 的阶梯上
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

        /// 当前描边色档：0 自动（与字色反差）、1 白、2 黑（文字工具用；描边开着时按它取色）
        [[nodiscard]] int currentTextStrokeColorStep() const;

        /// 描边色走一档（自动 → 白 → 黑 → 自动），并发出 textStrokeColorStepChanged
        void cycleTextStrokeColor();

        /// 当前描边粗细档：0 细、1 中、2 粗（取值表 Core::kTextOutlineWidths，中档与这一格出现之前的写死值同解）
        [[nodiscard]] int currentTextOutlineWidthStep() const;

        /// 描边粗细走一档（细 → 中 → 粗 → 细），并发出 textOutlineWidthStepChanged
        void cycleTextOutlineWidth();

        /// 当前底板色档：0 自动（与字色反差）、1 白、2 黑、3 用这一形状自己的色
        [[nodiscard]] int currentTextPlateColorStep() const;

        /// 底板色走一档（自动 → 白 → 黑 → 形状色 → 自动），并发出 textPlateColorStepChanged
        void cycleTextPlateColor();

        /// 当前计数组（1..Core::kNumberGroupCount；序号工具用，新落的牌记在它上面）
        [[nodiscard]] int currentNumberGroup() const;

        /// 计数组走一档（第 1 组 → 第 2 组 → 第 3 组 → 第 1 组），并发出 numberGroupChanged
        void cycleNumberGroup();

        /// 当前文字是不是反色底板
        [[nodiscard]] bool currentTextBackground() const;

        /**
         * @brief 开/关文字反色底板，按钮态、提示与信号一次对齐
         * @param enabled true 表示字形后面垫一块与字色反差的半透明圆角板
         */
        void setTextBackground(bool enabled);

        /**
         * @brief 当前颜色的不透明度档位（百分比，四舍五入到整数）
         * @return 10 到 100 之间的整数；颜色本身带着 alpha，这里只是把读数说给界面与用例听
         */
        [[nodiscard]] int currentColorOpacityPercent() const;

        /**
         * @brief 指定序号的工具按钮，用例与调试用它核对状态
         * @param index 按钮序号，越界返回空指针
         */
        [[nodiscard]] QToolButton *toolButtonAt(std::size_t index) const;

    signals:
        /// 用户切了工具
        void toolChanged(Core::ToolKind toolKind);
        /// 当前工具换过之后可见段与尺寸跟着变了：宿主据此把工具栏重摆一次（保持贴着选区）
        void contextLayoutChanged();
        /// 用户切了颜色
        void colorChanged(const QColor &color);
        /// 用户改了线宽
        void widthChanged(int width);
        /// 用户改了字号
        void fontSizeChanged(int fontSize);
        /// 用户改了填充档
        void fillStyleChanged(int fillStyle);
        /// 用户开了/关了圆角矩形
        void roundedRectChanged(bool enabled);
        /// 用户改了线型
        void lineStyleChanged(int style);
        /// 用户开了/关了双向箭头
        void doubleHeadedChanged(bool enabled);
        /// 用户开了/关了反向箭头
        void arrowReversedChanged(bool enabled);
        /// 用户开了/关了椭圆档
        void ellipticalChanged(bool enabled);
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
        /// 用户改了文字描边色档
        void textStrokeColorStepChanged(int step);
        /// 用户切了文字描边的粗细档
        void textOutlineWidthStepChanged(int step);
        /// 用户切了文字底板的颜色档
        void textPlateColorStepChanged(int step);
        /// 用户切了序号的计数组
        void numberGroupChanged(int group);
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

        /// 识别二维码/条形码
        void scanClicked();

    protected:
        /**
         * @brief 在色板那一排上滚滚轮：调当前形状那一格颜色的不透明度
         * @param event 滚轮事件
         * @details 重写 QWidget::wheelEvent：只在色块那一段矩形里生效，滚到工具按钮或滑块上不改颜色——
         *          「在这排上滚滚轮」是这一档唯一的手势出口，滚哪儿都改会让调线宽的手势顺手把颜色调淡。
         */
        void wheelEvent(QWheelEvent *event) override;

        /**
         * @brief 色板那几格上的按住拖动：把这一格拖到另一格那儿就重排
         * @param watched 被观察的控件（这里只有色板那几枚色块）
         * @param event 来的事件
         * @return 交回基类的结果：这一路只看不吞，按下与点击照旧走完
         * @details 重写 QWidget::eventFilter：只看不动——按下、移动、抬起三下都在基类流程里照常走完，
         *          这一路只在走过阈值且落到另一格上时挪一次顺序。松手落在别格上时 Qt 本就不发 clicked()，
         *          所以重排不会顺手把在用的颜色换掉，这里不需要额外挡一道。
         */
        bool eventFilter(QObject *watched, QEvent *event) override;

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
         * @details 先摘旧按钮再插新的，编号从色板那一排的格数起排（自定义之后格数不是八）；
         *          自定义按钮固定用 kCustomColorButtonId，才不会和插进来的色块撞编号。
         */
        void rebuildRecentColors();

        /**
         * @brief 用户挑了一格颜色：把它记到当前形状那一格上，并发出 colorChanged
         * @param color 挑中的颜色
         * @details 色表按形状存：同一张画布上「箭头一律红、序号一律蓝」是这套工具最常见的用法，
         *          切一样东西就得重新挑色，比一色到底更贴近实际。
         */
        void applyColorToActiveTool(const QColor &color);

        /**
         * @brief 切到另一个形状时，把那一格记着的颜色换到当前颜色上
         * @param kind 切过去的形状
         * @details 没记过颜色的形状不动：突然把用户正在用的颜色换成色板第一格，比留着更让人莫名。
         *          换了色要把按下的色块跟着挪过去，否则按下的那一格说的不是当前色。
         */
        void adoptColorForTool(Core::ToolKind kind);

        /// 按当前颜色把色块行里的按下态挪到匹配的那一格；没有匹配格就落到自定义那一格
        void syncColorButtonChecked();

        /// 这个窗口内落点是否在色板那一段上（预设 + 最近使用 + 自定义那一枚的合矩形）
        [[nodiscard]] bool overColorRow(const QPoint &localPoint) const;

        /// 全局落点落在色板第几格上；不在那一排上返回 -1（最近使用与自定义那两枚不参与重排）
        [[nodiscard]] int paletteIndexAtGlobalPoint(const QPoint &globalPoint) const;

        /// 按 m_palette 重画色块图标：重排只换图标，不重建按钮（拖的那一下正跑在按钮的事件里，删它会当场踩空）
        void repaintPaletteSwatches();

        /// 把当前不透明度写进自定义按钮的提示：读数要有出口，不然「滚了一下到底改没改」只能靠画一笔去看
        void updateOpacityHint();

        /// 按清册现读的键位重排工具按钮的提示；设置页换过键要能重算，所以不并进建按钮那一段
        void refreshToolKeyHints();

        /**
         * @brief 按当前工具与描边/底板两个开关收放中段的参数控件
         * @details 全部常显会把工具栏撑到 2374px 宽（实测，见审查台账 UiAuditPlan，仓外 GyanisScribe-Reports/），窄屏上右端的
         *          动作按钮整排落在屏外；这里按「画出来的东西用得上」收敛——可适用性以 Core::AnnotationShape.h
         *          各字段注释（「仅 X 用」）为准，描边色/描边粗细是描边开关的子档、底板色是底板开关的子档。
         *          可见集合真的变了才发 contextLayoutChanged：宿主拿它重摆，每回都发会让工具栏一直挪。
         */
        void updateContextVisibility();

        Core::ISettingsStore      &m_settings;                  ///< 最近使用颜色的读写去处
        std::vector<QColor>        m_recentColors;              ///< 最近用过的自定义颜色，首位最新
        std::vector<QToolButton *> m_recentButtons;             ///< 与 m_recentColors 同序的色块按钮
        std::vector<QToolButton *> m_paletteButtons;            ///< 色板那几格的按钮，与 m_palette 同序（自定义色板时格数不是写死的八格）
        std::vector<QColor>        m_palette;                   ///< 这一排摆的色板：配置里那份，没填就是内置那八格
        int                        m_recentInsertIndex    = 0;  ///< 色块插进布局的位置（预设色板之后、自定义按钮之前）
        int                        m_draggingPaletteIndex = -1; ///< 正在被拖的色板格序号，-1 表示没在拖
        QPoint                     m_dragStartGlobal;           ///< 按下那一下的全局位置：走过 Qt 的拖动阈值才算拖
        class QHBoxLayout         *m_layout     = nullptr;      ///< 工具栏主布局，重建色块时要往这里插
        QButtonGroup              *m_colorGroup = nullptr;      ///< 色块的互斥组，重建最近使用时要同步它的成员

        const Gui::IconProvider                     &m_icons;                                                                   ///< 图标出口，不拥有
        std::array<QToolButton *, kToolButtonCount>  m_toolButtons{};                                                           ///< 工具按钮，按 Core::ToolKind 顺序
        std::array<Core::ToolKind, kToolButtonCount> m_toolKinds{};                                                             ///< 按钮序号到工具的映射，反向查也用它
        QToolButton                                 *m_customColorButton    = nullptr;                                          ///< 自定义取色按钮
        QToolButton                                 *m_undoButton           = nullptr;                                          ///< 撤销按钮
        QToolButton                                 *m_redoButton           = nullptr;                                          ///< 重做按钮
        QSlider                                     *m_widthSlider          = nullptr;                                          ///< 线宽滑块，档位对齐 kAnnotationWidths
        QComboBox                                   *m_fontSizeCombo        = nullptr;                                          ///< 字号下拉，取值对齐 kFontSizes
        QToolButton                                 *m_fillButton           = nullptr;                                          ///< 填充档按钮，点一下走一档
        QToolButton                                 *m_roundedRectButton    = nullptr;                                          ///< 圆角矩形开关，按下表示切圆角
        bool                                         m_roundedRect          = false;                                            ///< 当前是不是圆角矩形
        int                                          m_fillStyle            = 0;                                                ///< 当前填充档：0 无、1 半透明、2 实心
        QToolButton                                 *m_lineStyleButton      = nullptr;                                          ///< 线型按钮，点一下走一档
        int                                          m_lineStyle            = static_cast<int>(Core::kDefaultLineStyleStep);    ///< 当前线型档，值表是 Core::kAnnotationLineStyles
        QToolButton                                 *m_doubleArrowButton    = nullptr;                                          ///< 双向箭头开关按钮，按下表示两端有头
        bool                                         m_doubleHeaded         = false;                                            ///< 当前是不是双向箭头
        QToolButton                                 *m_reversedArrowButton  = nullptr;                                          ///< 反向箭头开关按钮，按下表示箭头在起点那头
        bool                                         m_arrowReversed        = false;                                            ///< 当前是不是反向箭头
        QToolButton                                 *m_ellipticalButton     = nullptr;                                          ///< 椭圆形状开关按钮，按下表示收进内切椭圆/换圆头
        bool                                         m_elliptical           = false;                                            ///< 当前是不是椭圆档
        QToolButton                                 *m_mosaicGrainButton    = nullptr;                                          ///< 马赛克粒度按钮，点一下走一档
        int                                          m_mosaicBlockStep      = static_cast<int>(Core::kDefaultMosaicBlockStep);  ///< 当前粒度档下标，值表是 Core::kMosaicBlockSizes
        QToolButton                                 *m_highlightAlphaButton = nullptr;                                          ///< 高亮透明度按钮，点一下走一档
        int                                          m_highlightAlphaStep = static_cast<int>(Core::kDefaultHighlightAlphaStep); ///< 当前透明度档下标，值表是 Core::kHighlightAlphas
        QToolButton                                 *m_highlightWidthButton = nullptr;                                          ///< 高亮加宽按钮，点一下走一档
        int                m_highlightWidthStep   = static_cast<int>(Core::kDefaultHighlightWidthStep);   ///< 当前加宽档下标，值表是 Core::kHighlightWidthFactors
        QToolButton       *m_arrowHeadStyleButton = nullptr;                                              ///< 箭头款式按钮，点一下走一档
        int                m_arrowHeadStyle       = Core::kArrowHeadFilled;                               ///< 当前头部款式：0 实心、1 空心、2 开口两翼
        QToolButton       *m_arrowHeadScaleButton = nullptr;                                              ///< 箭头大小按钮，点一下走一档
        int                m_arrowHeadScaleStep   = static_cast<int>(Core::kDefaultArrowHeadScaleStep);   ///< 当前大小档下标，值表是 Core::kArrowHeadScales
        QFontComboBox     *m_fontCombo            = nullptr;                                              ///< 文字字体族下拉，列系统字体
        QToolButton       *m_boldButton           = nullptr;                                              ///< 文字粗体开关，按下表示粗
        QToolButton       *m_italicButton         = nullptr;                                              ///< 文字斜体开关，按下表示斜
        QToolButton       *m_outlineButton        = nullptr;                                              ///< 文字描边开关，按下表示围一圈反色轮廓
        QToolButton       *m_strokeColorButton    = nullptr;                                              ///< 文字描边色按钮，点一下走一档
        int                m_textStrokeColorStep  = static_cast<int>(Core::kDefaultTextStrokeColorStep);  ///< 当前描边色档，值表是 kTextStrokeColor* 三档
        QToolButton       *m_outlineWidthButton   = nullptr;                                              ///< 文字描边粗细按钮，点一下走一档
        int                m_textOutlineWidthStep = static_cast<int>(Core::kDefaultTextOutlineWidthStep); ///< 当前描边粗细档，值表是 Core::kTextOutlineWidths 三档
        QToolButton       *m_numberGroupButton    = nullptr;                                              ///< 计数组按钮，点一下走一档（序号工具用）
        int                m_numberGroup          = 1;                                                    ///< 当前计数组（1..Core::kNumberGroupCount）
        QToolButton       *m_backgroundButton     = nullptr;                                              ///< 文字底板开关，按下表示垫一块反色圆角板
        QToolButton       *m_plateColorButton     = nullptr;                                              ///< 文字底板色按钮，点一下走一档
        int                m_textPlateColorStep   = static_cast<int>(Core::kDefaultTextPlateColorStep);   ///< 当前底板色档，值表是 kTextPlateColor* 四档
        QColor             m_currentColor;                                                                ///< 当前颜色，初值取色板第一格
        QHash<int, QColor> m_colorsByTool;          ///< 每种形状各记一格颜色（键是 Core::ToolKind 的底层值）；没挑过的形状不在表里
        bool               m_stateIsInitial = true; ///< 还在构造期：起点值不回写配置，坏值要留在文件里让人看得见
        Core::ToolKind     m_activeTool;            ///< 当前工具，用来挡住「重复点同一个工具」的二次信号
    };

} // namespace GyanisScribe::Gui
