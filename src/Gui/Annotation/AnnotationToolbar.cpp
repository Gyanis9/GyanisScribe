#include "Gui/Annotation/AnnotationToolbar.h"

#include "Core/Annotation/AnnotationPalette.h"
#include "Core/Annotation/AnnotationTool.h"
#include "Core/Pick/RecentColors.h"
#include "Base/Logger.h"
#include "Base/Text.h"
#include "Core/Settings/ISettingsStore.h"
#include "Core/Settings/SettingsKeys.h"
#include "Gui/Controls/AppShortcuts.h"
#include "Gui/Theme/DesignTokens.h"

#include <QApplication>
#include <QAction>
#include <QButtonGroup>
#include <QColorDialog>
#include <QComboBox>
#include <QFontComboBox>
#include <QFrame>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QIcon>
#include <QMenu>
#include <QPainter>
#include <QPen>
#include <QPixmap>
#include <QSlider>
#include <QString>
#include <QToolButton>
#include <QWheelEvent>
#include <QWidgetAction>

#include <algorithm>
#include <array>
#include <cmath>
#include <format>
#include <string_view>
#include <tuple>

namespace
{
    using GyanisScribe::Core::ToolKind;
    using GyanisScribe::Gui::IconSize;
    using GyanisScribe::Gui::kAnnotationWidths;
    using GyanisScribe::Gui::kDefaultAnnotationFontSizeIndex;
    using GyanisScribe::Gui::kDefaultAnnotationWidthStep;
    using GyanisScribe::Gui::kFontSizes;
    using GyanisScribe::Gui::stepIndexOf;

    /// 滚轮一格改多少不透明度（百分点）：二十下从满到最淡，一下改太多会让人滚过头找不到原来那一档
    constexpr int kColorOpacityStepPercent = 5;

    /// 不透明度下限（百分点）：再淡就等于没画，标注是要给人看见的东西
    constexpr int kMinimumColorOpacityPercent = 10;

    /// 不透明度上限（百分点）
    constexpr int kMaximumColorOpacityPercent = 100;

    /// 配置里没存过、或存的值不在阶梯里时用的起点档：值在 Ui 的阶梯常量里，设置页与这里同解
    constexpr int kDefaultWidthStep     = static_cast<int>(kDefaultAnnotationWidthStep);
    constexpr int kDefaultFontSizeIndex = static_cast<int>(kDefaultAnnotationFontSizeIndex);

    /**
     * @brief 画一枚色块
     * @param color 色块本身的颜色
     * @param outline 边框色，取主题的图标色
     * @details 边框不是装饰：色板里有白色，白底界面上一块无框的白什么都看不见，
     *          用户会以为那一格是禁用状态。
     */
    QPixmap colorSwatchPixmap(const QColor &color, const QColor &outline)
    {
        const auto side = static_cast<int>(IconSize::Medium);
        // 按屏缩放比光栅化并写回比例：色块跟着按钮的 20 逻辑格走，125%/150% 屏上才不会被 Qt 整数倍
        // 拉伸成一片糊边。封顶与 IconProvider 同一档，超过 200% 的屏交给 Qt 就近缩放
        const auto ratio = qApp != nullptr ? std::clamp(qApp->devicePixelRatio(), 1.0, 3.0) : 1.0;
        const auto edge  = static_cast<int>(std::ceil(static_cast<qreal>(side) * ratio));
        QPixmap    bitmap(edge, edge);
        bitmap.fill(Qt::transparent);
        bitmap.setDevicePixelRatio(ratio);

        QPainter painter(&bitmap);
        painter.setRenderHint(QPainter::Antialiasing);
        painter.setPen(QPen{outline, 1.0});
        painter.setBrush(color);
        painter.drawRoundedRect(QRectF{1.0, 1.0, static_cast<qreal>(side) - 2.0, static_cast<qreal>(side) - 2.0}, 3.0, 3.0);
        return bitmap;
    }

    /// 分组之间的竖线：需求里「工具组 → 颜色 → 线宽/字号 → 撤销/重做 → 动作」的分界靠它
    QFrame *makeSeparator()
    {
        auto *separator = new QFrame;
        separator->setFrameShape(QFrame::VLine);
        separator->setFrameShadow(QFrame::Plain);
        // 颜色由样式表按这个名字给：工具组、色板、线宽、字号四段之间要有同一条缝
        separator->setObjectName(QStringLiteral("toolbarSeparator"));
        return separator;
    }
} // namespace

namespace GyanisScribe::Gui
{
    AnnotationToolbar::AnnotationToolbar(Core::ISettingsStore &settings, const Gui::IconProvider &icons, QWidget *parent) :
        QWidget(parent), m_settings(settings), m_icons(icons), m_activeTool(Core::ToolKind::Select)
    {
        setObjectName(QStringLiteral("AnnotationToolbar"));
        // 样式表给这个对象名画了一圈卡片底；QWidget 子类不打开这个开关就不会吃样式表的背景
        setAttribute(Qt::WA_StyledBackground, true);

        auto *layout = new QHBoxLayout{this};
        layout->setContentsMargins(Gui::kSpacingTiny, Gui::kSpacingTiny, Gui::kSpacingTiny, Gui::kSpacingTiny);
        // 这一排最多五十几件控件，节距每多 1px 整条就多五十几像素：收到发丝档，
        // 按钮自己带 2px 内边距做视觉留白就够了（按钮最小尺寸也按同一理由在样式表里收了一档）
        layout->setSpacing(Gui::kSpacingHairline);

        // 工具组：互斥交给 QButtonGroup，这样鼠标点击与 setActiveTool 走的是同一套按钮状态
        auto *toolGroup = new QButtonGroup{this};
        toolGroup->setExclusive(true);
        for (std::size_t index = 0; index < kToolTable.size(); ++index)
        {
            const auto kind      = std::get<0>(kToolTable[index]);
            const auto iconName  = std::get<1>(kToolTable[index]);
            const auto label     = std::get<2>(kToolTable[index]);
            auto      *button    = makeButton(iconName, GyanisScribe::Base::uiText(label), true);
            m_toolButtons[index] = button;
            m_toolKinds[index]   = kind;
            // id 取工具序号：与 Core::ToolKind 的底层值同解，反向查按钮时不必再存一张表
            toolGroup->addButton(button, static_cast<int>(kind));
            QObject::connect(button, &QToolButton::clicked, this,
                             [this, kind]
                             {
                                 // 重复点已经在用的工具不再发第二次：宿主每次收到 toolChanged 都要问一遍引擎并刷按钮态
                                 if (m_activeTool == kind)
                                 {
                                     return;
                                 }
                                 m_activeTool = kind;
                                 // 先把这一格形状记着的颜色换过来再发信号：宿主在 toolChanged 里读 currentColor()，
                                 // 顺序反过来它会拿到上一个形状的颜色
                                 adoptColorForTool(kind);
                                 // 可见段先在本地收好再发信号：宿主收到 toolChanged 与 contextLayoutChanged 时
                                 // 读到的 sizeHint 必须已经是新的一档
                                 updateContextVisibility();
                                 emit toolChanged(kind);
                             });
            layout->addWidget(button);
        }
        // 键位提示排在按钮建好之后：那一串要从清册现读，与按键分派吃到的是同一颗键
        refreshToolKeyHints();
        // 设置页换过某档的键就重排提示：按钮只在建窗时排一次，不订阅就停在启动那一刻的旧键上，
        // 于是「按屏幕上写的那颗键没反应」
        QObject::connect(&m_settings, &GyanisScribe::Core::ISettingsStore::valueChanged, this,
                         [this](const QString &key)
                         {
                             if (key.startsWith(QString::fromUtf8(GyanisScribe::Core::kAppShortcutPrefix.data(), static_cast<int>(GyanisScribe::Core::kAppShortcutPrefix.size()))))
                             {
                                 refreshToolKeyHints();
                             }
                         });
        layout->addWidget(makeSeparator());

        // 颜色入口：色板、最近使用与自定义取色都收进这一枚按钮弹出的菜单里，工具栏只占一格。
        // 色板读配置里那一份：没填就是内置那八格。读坏了记一条警告再回落（与「最近使用」那一路同一口径），
        // 不静默把坏值当成「一格都没存」——用户手改配置文件时那句警告是唯一能看见的线索
        if (const auto resolved = Core::annotationPalette(m_settings); resolved)
        {
            m_palette = *resolved;
        } else
        {
            Base::Logger::warning(std::format("自定义色板读不出：{}，本次按内置那八格摆", resolved.error().message));
            m_palette = Core::defaultAnnotationPalette();
        }
        if (const auto parsed = Core::parseRecentColors(m_settings.stringListValue(Core::kAnnotationRecentColors, {})); parsed)
        {
            m_recentColors = *parsed;
            // 手改过的配置文件可能超出上限：按上限截断，菜单不会被撑到放不下
            if (m_recentColors.size() > Core::kRecentColorCapacity)
            {
                m_recentColors.resize(Core::kRecentColorCapacity);
            }
        } else
        {
            Base::Logger::warning(std::format("最近使用颜色读不出：{}，本次从空清单开始", parsed.error().message));
        }
        m_colorButton = makeButton("ColorPicker", GyanisScribe::Base::uiText("颜色"), false);
        m_colorButton->setObjectName(QStringLiteral("annotationColorButton"));
        m_colorButton->setPopupMode(QToolButton::InstantPopup);
        m_colorMenu = new QMenu{this};
        m_colorMenu->setObjectName(QStringLiteral("annotationColorMenu"));
        m_colorButton->setMenu(m_colorMenu);
        // 每次弹出前重建：色板与最近使用只在面板里出现，选过一格之后不必另外通知界面
        QObject::connect(m_colorMenu, &QMenu::aboutToShow, this, &AnnotationToolbar::rebuildColorMenu);
        layout->addWidget(m_colorButton);
        layout->addWidget(makeSeparator());

        // 线宽：滑块只走阶梯，取值表与 Gui::kAnnotationWidths 同解，不会出现 3.5 这种没定义过的宽度
        m_widthSlider = new QSlider{Qt::Horizontal, this};
        m_widthSlider->setRange(0, static_cast<int>(kAnnotationWidths.size()) - 1);
        m_widthSlider->setPageStep(1);
        m_widthSlider->setFixedWidth(Gui::kToolbarSliderWidth);
        m_widthSlider->setAccessibleName(GyanisScribe::Base::uiText("线宽"));
        m_widthSlider->setToolTip(GyanisScribe::Base::uiText("线宽"));
        QObject::connect(m_widthSlider, &QSlider::valueChanged, this,
                         [this](int step)
                         {
                             if (step >= 0 && static_cast<std::size_t>(step) < kAnnotationWidths.size())
                             {
                                 emit widthChanged(kAnnotationWidths[static_cast<std::size_t>(step)]);
                                 // 记成默认档：下次开标注窗不用再从 4 重新调，这是「工具记住了我」的那半
                                 rememberDefaults();
                             }
                         });
        layout->addWidget(m_widthSlider);

        // 字号：需求给的是离散阶梯（12/13/14/16/20/24），用下拉而不是滑块，避免出现 15 这种没定义的值
        m_fontSizeCombo = new QComboBox{this};
        for (const auto size: kFontSizes)
        {
            m_fontSizeCombo->addItem(QString::number(size), size);
        }
        m_fontSizeCombo->setAccessibleName(GyanisScribe::Base::uiText("字号"));
        m_fontSizeCombo->setToolTip(GyanisScribe::Base::uiText("字号"));
        QObject::connect(m_fontSizeCombo, &QComboBox::currentIndexChanged, this,
                         [this](int index)
                         {
                             if (index >= 0 && index < m_fontSizeCombo->count())
                             {
                                 emit fontSizeChanged(m_fontSizeCombo->itemData(index).toInt());
                                 rememberDefaults();
                             }
                         });
        // 填充档：矩形与椭圆用得上，点一下走一档（无 → 半透明 → 实心）。按钮不进工具栏的互斥组——
        // 它是「这条图形怎么填」而不是「用哪档工具」
        m_fillButton = makeButton("Fill", GyanisScribe::Base::uiText("填充"), false);
        QObject::connect(m_fillButton, &QToolButton::clicked, this, &AnnotationToolbar::cycleFillStyle);
        layout->addWidget(m_fillButton);
        // 圆角矩形：矩形工具用得上，是一个保持按下态的开关（与双向箭头同一套口径）
        m_roundedRectButton = makeButton("RoundedRect", GyanisScribe::Base::uiText("圆角"), true);
        QObject::connect(m_roundedRectButton, &QToolButton::clicked, this, [this] { setRoundedRect(m_roundedRectButton->isChecked()); });
        layout->addWidget(m_roundedRectButton);
        // 线型：描边类工具用得上，点一下走一档（实线 → 虚线 → 点线）
        m_lineStyleButton = makeButton("LineStyle", GyanisScribe::Base::uiText("线型"), false);
        QObject::connect(m_lineStyleButton, &QToolButton::clicked, this, &AnnotationToolbar::cycleLineStyle);
        layout->addWidget(m_lineStyleButton);
        // 双向箭头：箭头工具用得上，是一个保持按下态的开关。同样不进互斥组，
        // 点击时 Qt 先翻 checked 再发 clicked，所以这里读按钮态而不是自己翻一遍
        m_doubleArrowButton = makeButton("ArrowDouble", GyanisScribe::Base::uiText("双向箭头"), true);
        QObject::connect(m_doubleArrowButton, &QToolButton::clicked, this, [this] { setDoubleHeaded(m_doubleArrowButton->isChecked()); });
        layout->addWidget(m_doubleArrowButton);
        // 反向箭头：同一枚箭头工具的第二档开关，把箭头换到起点那一头（拖的方向与指的方向相反）。
        // 与双向不互斥——两个都开就是两端都有头，谁也没被偷偷关掉
        m_reversedArrowButton = makeButton("ArrowReversed", GyanisScribe::Base::uiText("反向箭头"), true);
        QObject::connect(m_reversedArrowButton, &QToolButton::clicked, this, [this] { setArrowReversed(m_reversedArrowButton->isChecked()); });
        layout->addWidget(m_reversedArrowButton);
        // 椭圆形状：马赛克/高斯模糊/高亮/橡皮擦四件用得上，同为保持按下态的开关；
        // 名字带「形状」是为了与工具组里那枚「椭圆」区分——那枚是切工具，这枚是切当前工具的落笔形状
        m_ellipticalButton = makeButton("EllipseShape", GyanisScribe::Base::uiText("椭圆形状"), true);
        QObject::connect(m_ellipticalButton, &QToolButton::clicked, this, [this] { setElliptical(m_ellipticalButton->isChecked()); });
        layout->addWidget(m_ellipticalButton);
        // 马赛克粒度：马赛克工具用得上，点一下走一档（细 → 中 → 粗），档位表是 Core::kMosaicBlockSizes
        m_mosaicGrainButton = makeButton("MosaicGrain", GyanisScribe::Base::uiText("马赛克粒度"), false);
        QObject::connect(m_mosaicGrainButton, &QToolButton::clicked, this, &AnnotationToolbar::cycleMosaicBlockSize);
        layout->addWidget(m_mosaicGrainButton);
        // 高亮透明度：高亮工具用得上，点一下走一档（轻 → 中 → 重），档位表是 Core::kHighlightAlphas
        m_highlightAlphaButton = makeButton("HighlightOpacity", GyanisScribe::Base::uiText("高亮透明度"), false);
        QObject::connect(m_highlightAlphaButton, &QToolButton::clicked, this, &AnnotationToolbar::cycleHighlightAlpha);
        layout->addWidget(m_highlightAlphaButton);
        // 高亮加宽：高亮工具用得上，点一下走一档（窄 → 中 → 宽），档位表是 Core::kHighlightWidthFactors
        m_highlightWidthButton = makeButton("HighlightWidth", GyanisScribe::Base::uiText("高亮加宽"), false);
        QObject::connect(m_highlightWidthButton, &QToolButton::clicked, this, &AnnotationToolbar::cycleHighlightWidthFactor);
        layout->addWidget(m_highlightWidthButton);
        // 箭头款式：箭头工具用得上，点一下走一档（实心 → 空心 → 开口）
        m_arrowHeadStyleButton = makeButton("ArrowHeadStyle", GyanisScribe::Base::uiText("箭头样式"), false);
        QObject::connect(m_arrowHeadStyleButton, &QToolButton::clicked, this, &AnnotationToolbar::cycleArrowHeadStyle);
        layout->addWidget(m_arrowHeadStyleButton);
        // 箭头大小：箭头工具用得上，点一下走一档（小 → 中 → 大），档位表是 Core::kArrowHeadScales
        m_arrowHeadScaleButton = makeButton("ArrowHeadSize", GyanisScribe::Base::uiText("箭头大小"), false);
        QObject::connect(m_arrowHeadScaleButton, &QToolButton::clicked, this, &AnnotationToolbar::cycleArrowHeadScale);
        layout->addWidget(m_arrowHeadScaleButton);
        // 文字字体族：文字工具用得上，列系统字体。宽度封顶——字体名长的很，放任 sizeHint 会撑出去
        m_fontCombo = new QFontComboBox{this};
        m_fontCombo->setMaximumWidth(Gui::kToolbarFontComboWidth);
        m_fontCombo->setAccessibleName(GyanisScribe::Base::uiText("字体"));
        m_fontCombo->setToolTip(GyanisScribe::Base::uiText("字体（文字）"));
        QObject::connect(m_fontCombo, &QFontComboBox::currentFontChanged, this, [this](const QFont &font) { emit fontFamilyChanged(font.family()); });
        layout->addWidget(m_fontCombo);
        layout->addWidget(m_fontSizeCombo);
        // 粗体/斜体：文字工具用得上，保持按下态的开关，与双向箭头同一套口径
        m_boldButton = makeButton("Bold", GyanisScribe::Base::uiText("粗体"), true);
        QObject::connect(m_boldButton, &QToolButton::clicked, this, [this] { setTextBold(m_boldButton->isChecked()); });
        layout->addWidget(m_boldButton);
        m_italicButton = makeButton("Italic", GyanisScribe::Base::uiText("斜体"), true);
        QObject::connect(m_italicButton, &QToolButton::clicked, this, [this] { setTextItalic(m_italicButton->isChecked()); });
        layout->addWidget(m_italicButton);
        // 描边/底板：文字工具用得上，同为保持按下态的开关
        m_outlineButton = makeButton("TextOutline", GyanisScribe::Base::uiText("描边"), true);
        QObject::connect(m_outlineButton, &QToolButton::clicked, this, [this] { setTextOutline(m_outlineButton->isChecked()); });
        layout->addWidget(m_outlineButton);
        // 描边色：描边开着时用哪一色，点一下走一档（自动 → 白 → 黑）
        m_strokeColorButton = makeButton("StrokeColor", GyanisScribe::Base::uiText("描边色"), false);
        QObject::connect(m_strokeColorButton, &QToolButton::clicked, this, &AnnotationToolbar::cycleTextStrokeColor);
        layout->addWidget(m_strokeColorButton);
        // 描边粗细：描边开着时那一圈有多厚，点一下走一档（细 → 中 → 粗）
        m_outlineWidthButton = makeButton("TextOutlineWidth", GyanisScribe::Base::uiText("描边粗细"), false);
        QObject::connect(m_outlineWidthButton, &QToolButton::clicked, this, &AnnotationToolbar::cycleTextOutlineWidth);
        layout->addWidget(m_outlineWidthButton);
        // 计数组：序号工具用得上，点一下走一档（第 1 组 → 第 2 组 → 第 3 组 → 第 1 组）
        m_numberGroupButton = makeButton("NumberGroup", GyanisScribe::Base::uiText("计数组"), false);
        QObject::connect(m_numberGroupButton, &QToolButton::clicked, this, &AnnotationToolbar::cycleNumberGroup);
        layout->addWidget(m_numberGroupButton);
        m_backgroundButton = makeButton("TextBackground", GyanisScribe::Base::uiText("背景"), true);
        QObject::connect(m_backgroundButton, &QToolButton::clicked, this, [this] { setTextBackground(m_backgroundButton->isChecked()); });
        layout->addWidget(m_backgroundButton);
        // 底板色：背景开着时垫哪一色，点一下走一档（自动 → 白 → 黑 → 这一形状自己的色）
        m_plateColorButton = makeButton("PlateColor", GyanisScribe::Base::uiText("底板色"), false);
        QObject::connect(m_plateColorButton, &QToolButton::clicked, this, &AnnotationToolbar::cycleTextPlateColor);
        layout->addWidget(m_plateColorButton);
        layout->addWidget(makeSeparator());

        m_undoButton = makeButton("Undo", GyanisScribe::Base::uiText("撤销（Ctrl+Z）"), false);
        m_redoButton = makeButton("Redo", GyanisScribe::Base::uiText("重做（Ctrl+Y）"), false);
        // 初值不可用：引擎刚建好时没有历史，亮着的撤销按钮点了没反应，比灰着更误导
        m_undoButton->setEnabled(false);
        m_redoButton->setEnabled(false);
        QObject::connect(m_undoButton, &QToolButton::clicked, this, &AnnotationToolbar::undoClicked);
        QObject::connect(m_redoButton, &QToolButton::clicked, this, &AnnotationToolbar::redoClicked);
        layout->addWidget(m_undoButton);
        layout->addWidget(m_redoButton);
        layout->addWidget(makeSeparator());

        struct ActionSpec
        {
            std::string_view iconName;
            QString          label;
            void (AnnotationToolbar::*signal)();
        };

        const std::array actions = {
                ActionSpec{"Copy", GyanisScribe::Base::uiText("复制到剪贴板"), &AnnotationToolbar::copyClicked},
                ActionSpec{"Save", GyanisScribe::Base::uiText("保存为文件"), &AnnotationToolbar::saveClicked},
                ActionSpec{"Pin", GyanisScribe::Base::uiText("贴到屏幕上"), &AnnotationToolbar::pinClicked},
                ActionSpec{"Ocr", GyanisScribe::Base::uiText("识别文字"), &AnnotationToolbar::ocrClicked},
                ActionSpec{"QrCode", GyanisScribe::Base::uiText("识别二维码"), &AnnotationToolbar::scanClicked},
        };
        for (const auto &action: actions)
        {
            auto *button = makeButton(action.iconName, action.label, false);
            QObject::connect(button, &QToolButton::clicked, this, [this, signal = action.signal] { (this->*signal)(); });
            layout->addWidget(button);
        }

        // 最右那枚 X 与 Esc 同一条收场路：免得用户以为只能去托盘退出或再截一张
        layout->addWidget(makeSeparator());
        m_closeButton = makeButton("Close", GyanisScribe::Base::uiText("关闭标注"), false);
        m_closeButton->setToolTip(GyanisScribe::Base::uiText("关闭标注（Esc）"));
        QObject::connect(m_closeButton, &QToolButton::clicked, this, &AnnotationToolbar::closeClicked);
        layout->addWidget(m_closeButton);

        // 起点状态：选择工具按下，线宽与字号取「上次用的那一档」（配置里没有就回到 4 / 14）。
        // 此刻还没有任何外部接收方（宿主是在构造之后才接线的），所以不需要挡信号，
        // 只需要把状态摆对——尤其是 m_activeTool 要与按下的那个工具同解，否则第一次点矩形会被守卫吞掉
        toolGroup->button(static_cast<int>(Core::ToolKind::Select))->setChecked(true);
        applyStoredDefaults();
        m_stateIsInitial = false;
        m_currentColor   = m_palette.front();
        updateOpacityHint();
        updateContextVisibility();
    }

    int AnnotationToolbar::currentFillStyle() const
    {
        return m_fillStyle;
    }

    void AnnotationToolbar::cycleFillStyle()
    {
        m_fillStyle = (m_fillStyle + 1) % 3;
        if (m_fillButton != nullptr)
        {
            // 档名落在提示里：一枚图标说不清现在是哪一档，鼠标一停就该看得见
            const auto label = m_fillStyle == 0   ? GyanisScribe::Base::uiText("填充：无")
                               : m_fillStyle == 1 ? GyanisScribe::Base::uiText("填充：半透明")
                                                  : GyanisScribe::Base::uiText("填充：实心");
            m_fillButton->setToolTip(label);
            m_fillButton->setAccessibleName(label);
        }
        emit fillStyleChanged(m_fillStyle);
    }

    bool AnnotationToolbar::currentDoubleHeaded() const
    {
        return m_doubleHeaded;
    }

    bool AnnotationToolbar::currentRoundedRect() const
    {
        return m_roundedRectButton != nullptr && m_roundedRectButton->isChecked();
    }
    void AnnotationToolbar::setRoundedRect(const bool enabled)
    {
        m_roundedRect = enabled;
        if (m_roundedRectButton != nullptr)
        {
            m_roundedRectButton->setChecked(enabled);
            const auto label = enabled ? GyanisScribe::Base::uiText("圆角：开") : GyanisScribe::Base::uiText("圆角：关");
            m_roundedRectButton->setToolTip(label);
            m_roundedRectButton->setAccessibleName(label);
        }
        emit roundedRectChanged(enabled);
    }

    int AnnotationToolbar::currentLineStyle() const
    {
        return m_lineStyle;
    }

    void AnnotationToolbar::cycleLineStyle()
    {
        m_lineStyle = (m_lineStyle + 1) % static_cast<int>(Core::kAnnotationLineStyles.size());
        if (m_lineStyleButton != nullptr)
        {
            // 与另几档同一套口径：提示就是这一档的读数
            const auto label = m_lineStyle == 0   ? GyanisScribe::Base::uiText("线型：实线")
                               : m_lineStyle == 1 ? GyanisScribe::Base::uiText("线型：虚线")
                                                  : GyanisScribe::Base::uiText("线型：点线");
            m_lineStyleButton->setToolTip(label);
            m_lineStyleButton->setAccessibleName(label);
        }
        emit lineStyleChanged(currentLineStyle());
    }

    void AnnotationToolbar::setDoubleHeaded(const bool enabled)
    {
        m_doubleHeaded = enabled;
        if (m_doubleArrowButton != nullptr)
        {
            // 按下态本身看得见，但提示与无障碍名还是要跟上：读屏报「双向箭头」时得知道箭头现在是开是关
            m_doubleArrowButton->setChecked(enabled);
            const auto label = enabled ? GyanisScribe::Base::uiText("双向箭头：开") : GyanisScribe::Base::uiText("双向箭头：关");
            m_doubleArrowButton->setToolTip(label);
            m_doubleArrowButton->setAccessibleName(label);
        }
        emit doubleHeadedChanged(enabled);
    }

    bool AnnotationToolbar::currentArrowReversed() const
    {
        return m_arrowReversed;
    }

    void AnnotationToolbar::setArrowReversed(const bool enabled)
    {
        m_arrowReversed = enabled;
        if (m_reversedArrowButton != nullptr)
        {
            m_reversedArrowButton->setChecked(enabled);
            const auto label = enabled ? GyanisScribe::Base::uiText("反向箭头：开") : GyanisScribe::Base::uiText("反向箭头：关");
            m_reversedArrowButton->setToolTip(label);
            m_reversedArrowButton->setAccessibleName(label);
        }
        emit arrowReversedChanged(enabled);
    }

    bool AnnotationToolbar::currentElliptical() const
    {
        return m_elliptical;
    }

    void AnnotationToolbar::setElliptical(const bool enabled)
    {
        m_elliptical = enabled;
        if (m_ellipticalButton != nullptr)
        {
            // 与双向箭头同一套口径：按钮态、提示、无障碍名三者同解
            m_ellipticalButton->setChecked(enabled);
            const auto label = enabled ? GyanisScribe::Base::uiText("椭圆形状：椭圆") : GyanisScribe::Base::uiText("椭圆形状：矩形");
            m_ellipticalButton->setToolTip(label);
            m_ellipticalButton->setAccessibleName(label);
        }
        emit ellipticalChanged(enabled);
    }

    int AnnotationToolbar::currentMosaicBlockSize() const
    {
        return Core::kMosaicBlockSizes[static_cast<std::size_t>(m_mosaicBlockStep)];
    }

    void AnnotationToolbar::cycleMosaicBlockSize()
    {
        m_mosaicBlockStep = (m_mosaicBlockStep + 1) % static_cast<int>(Core::kMosaicBlockSizes.size());
        if (m_mosaicGrainButton != nullptr)
        {
            // 档名落在提示里：一枚图标说不清现在是哪一档，鼠标一停就该看得见
            const auto label = m_mosaicBlockStep == 0   ? GyanisScribe::Base::uiText("马赛克粒度：细")
                               : m_mosaicBlockStep == 1 ? GyanisScribe::Base::uiText("马赛克粒度：中")
                                                        : GyanisScribe::Base::uiText("马赛克粒度：粗");
            m_mosaicGrainButton->setToolTip(label);
            m_mosaicGrainButton->setAccessibleName(label);
        }
        emit mosaicBlockSizeChanged(currentMosaicBlockSize());
    }

    int AnnotationToolbar::currentHighlightAlpha() const
    {
        return Core::kHighlightAlphas[static_cast<std::size_t>(m_highlightAlphaStep)];
    }

    void AnnotationToolbar::cycleHighlightAlpha()
    {
        m_highlightAlphaStep = (m_highlightAlphaStep + 1) % static_cast<int>(Core::kHighlightAlphas.size());
        if (m_highlightAlphaButton != nullptr)
        {
            // 与粒度同一套口径：提示就是这一档的读数
            const auto label = m_highlightAlphaStep == 0   ? GyanisScribe::Base::uiText("高亮透明度：轻")
                               : m_highlightAlphaStep == 1 ? GyanisScribe::Base::uiText("高亮透明度：中")
                                                           : GyanisScribe::Base::uiText("高亮透明度：重");
            m_highlightAlphaButton->setToolTip(label);
            m_highlightAlphaButton->setAccessibleName(label);
        }
        emit highlightAlphaChanged(currentHighlightAlpha());
    }

    int AnnotationToolbar::currentHighlightWidthFactor() const
    {
        return Core::kHighlightWidthFactors[static_cast<std::size_t>(m_highlightWidthStep)];
    }

    void AnnotationToolbar::cycleHighlightWidthFactor()
    {
        m_highlightWidthStep = (m_highlightWidthStep + 1) % static_cast<int>(Core::kHighlightWidthFactors.size());
        if (m_highlightWidthButton != nullptr)
        {
            // 与另两档同一套口径：提示就是这一档的读数
            const auto label = m_highlightWidthStep == 0   ? GyanisScribe::Base::uiText("高亮加宽：窄")
                               : m_highlightWidthStep == 1 ? GyanisScribe::Base::uiText("高亮加宽：中")
                                                           : GyanisScribe::Base::uiText("高亮加宽：宽");
            m_highlightWidthButton->setToolTip(label);
            m_highlightWidthButton->setAccessibleName(label);
        }
        emit highlightWidthFactorChanged(currentHighlightWidthFactor());
    }

    int AnnotationToolbar::currentArrowHeadStyle() const
    {
        return m_arrowHeadStyle;
    }

    void AnnotationToolbar::cycleArrowHeadStyle()
    {
        m_arrowHeadStyle = (m_arrowHeadStyle + 1) % Core::kArrowHeadStyleCount;
        if (m_arrowHeadStyleButton != nullptr)
        {
            // 与另几档同一套口径：提示就是这一档的读数
            const auto label = m_arrowHeadStyle == Core::kArrowHeadFilled   ? GyanisScribe::Base::uiText("箭头样式：实心")
                               : m_arrowHeadStyle == Core::kArrowHeadHollow ? GyanisScribe::Base::uiText("箭头样式：空心")
                                                                            : GyanisScribe::Base::uiText("箭头样式：开口");
            m_arrowHeadStyleButton->setToolTip(label);
            m_arrowHeadStyleButton->setAccessibleName(label);
        }
        emit arrowHeadStyleChanged(currentArrowHeadStyle());
    }

    qreal AnnotationToolbar::currentArrowHeadScale() const
    {
        return Core::kArrowHeadScales[static_cast<std::size_t>(m_arrowHeadScaleStep)];
    }

    void AnnotationToolbar::cycleArrowHeadScale()
    {
        m_arrowHeadScaleStep = (m_arrowHeadScaleStep + 1) % static_cast<int>(Core::kArrowHeadScales.size());
        if (m_arrowHeadScaleButton != nullptr)
        {
            // 与另几档同一套口径：提示就是这一档的读数
            const auto label = m_arrowHeadScaleStep == 0   ? GyanisScribe::Base::uiText("箭头大小：小")
                               : m_arrowHeadScaleStep == 1 ? GyanisScribe::Base::uiText("箭头大小：中")
                                                           : GyanisScribe::Base::uiText("箭头大小：大");
            m_arrowHeadScaleButton->setToolTip(label);
            m_arrowHeadScaleButton->setAccessibleName(label);
        }
        emit arrowHeadScaleChanged(currentArrowHeadScale());
    }

    QString AnnotationToolbar::currentFontFamily() const
    {
        return m_fontCombo->currentFont().family();
    }

    bool AnnotationToolbar::currentTextBold() const
    {
        return m_boldButton != nullptr && m_boldButton->isChecked();
    }

    void AnnotationToolbar::setTextBold(const bool enabled)
    {
        if (m_boldButton != nullptr)
        {
            m_boldButton->setChecked(enabled);
            const auto label = enabled ? GyanisScribe::Base::uiText("粗体：开") : GyanisScribe::Base::uiText("粗体：关");
            m_boldButton->setToolTip(label);
            m_boldButton->setAccessibleName(label);
        }
        emit textBoldChanged(enabled);
    }

    bool AnnotationToolbar::currentTextItalic() const
    {
        return m_italicButton != nullptr && m_italicButton->isChecked();
    }

    void AnnotationToolbar::setTextItalic(const bool enabled)
    {
        if (m_italicButton != nullptr)
        {
            m_italicButton->setChecked(enabled);
            const auto label = enabled ? GyanisScribe::Base::uiText("斜体：开") : GyanisScribe::Base::uiText("斜体：关");
            m_italicButton->setToolTip(label);
            m_italicButton->setAccessibleName(label);
        }
        emit textItalicChanged(enabled);
    }

    bool AnnotationToolbar::currentTextOutline() const
    {
        return m_outlineButton != nullptr && m_outlineButton->isChecked();
    }

    void AnnotationToolbar::setTextOutline(const bool enabled)
    {
        if (m_outlineButton != nullptr)
        {
            m_outlineButton->setChecked(enabled);
            const auto label = enabled ? GyanisScribe::Base::uiText("描边：开") : GyanisScribe::Base::uiText("描边：关");
            m_outlineButton->setToolTip(label);
            m_outlineButton->setAccessibleName(label);
        }
        // 描边色与描边粗细跟着主开关收放：开着才有得改
        updateContextVisibility();
        emit textOutlineChanged(enabled);
    }

    int AnnotationToolbar::currentTextStrokeColorStep() const
    {
        return m_textStrokeColorStep;
    }

    void AnnotationToolbar::cycleTextStrokeColor()
    {
        m_textStrokeColorStep = (m_textStrokeColorStep + 1) % Core::kTextStrokeColorCount;
        if (m_strokeColorButton != nullptr)
        {
            // 与另几档同一套口径：提示就是这一档的读数
            const auto label = m_textStrokeColorStep == Core::kTextStrokeColorWhite   ? GyanisScribe::Base::uiText("描边色：白")
                               : m_textStrokeColorStep == Core::kTextStrokeColorBlack ? GyanisScribe::Base::uiText("描边色：黑")
                                                                                      : GyanisScribe::Base::uiText("描边色：自动");
            m_strokeColorButton->setToolTip(label);
            m_strokeColorButton->setAccessibleName(label);
        }
        emit textStrokeColorStepChanged(m_textStrokeColorStep);
    }

    int AnnotationToolbar::currentTextOutlineWidthStep() const
    {
        return m_textOutlineWidthStep;
    }

    void AnnotationToolbar::cycleTextOutlineWidth()
    {
        m_textOutlineWidthStep = (m_textOutlineWidthStep + 1) % static_cast<int>(Core::kTextOutlineWidths.size());
        if (m_outlineWidthButton != nullptr)
        {
            const auto label = m_textOutlineWidthStep == 0   ? GyanisScribe::Base::uiText("描边粗细：细")
                               : m_textOutlineWidthStep == 1 ? GyanisScribe::Base::uiText("描边粗细：中")
                                                             : GyanisScribe::Base::uiText("描边粗细：粗");
            m_outlineWidthButton->setToolTip(label);
            m_outlineWidthButton->setAccessibleName(label);
        }
        emit textOutlineWidthStepChanged(m_textOutlineWidthStep);
    }

    int AnnotationToolbar::currentTextPlateColorStep() const
    {
        return m_textPlateColorStep;
    }

    void AnnotationToolbar::cycleTextPlateColor()
    {
        m_textPlateColorStep = (m_textPlateColorStep + 1) % Core::kTextPlateColorCount;
        if (m_plateColorButton != nullptr)
        {
            const auto label = m_textPlateColorStep == Core::kTextPlateColorWhite       ? GyanisScribe::Base::uiText("底板色：白")
                               : m_textPlateColorStep == Core::kTextPlateColorBlack     ? GyanisScribe::Base::uiText("底板色：黑")
                               : m_textPlateColorStep == Core::kTextPlateColorFromShape ? GyanisScribe::Base::uiText("底板色：用标注色")
                                                                                        : GyanisScribe::Base::uiText("底板色：自动");
            m_plateColorButton->setToolTip(label);
            m_plateColorButton->setAccessibleName(label);
        }
        emit textPlateColorStepChanged(m_textPlateColorStep);
    }

    int AnnotationToolbar::currentNumberGroup() const
    {
        return m_numberGroup;
    }

    void AnnotationToolbar::cycleNumberGroup()
    {
        m_numberGroup = m_numberGroup % Core::kNumberGroupCount + 1;
        if (m_numberGroupButton != nullptr)
        {
            // 与另几档同一套口径：提示就是这一档的读数
            const auto label = m_numberGroup == 1   ? GyanisScribe::Base::uiText("计数组：第 1 组")
                               : m_numberGroup == 2 ? GyanisScribe::Base::uiText("计数组：第 2 组")
                                                    : GyanisScribe::Base::uiText("计数组：第 3 组");
            m_numberGroupButton->setToolTip(label);
            m_numberGroupButton->setAccessibleName(label);
        }
        emit numberGroupChanged(m_numberGroup);
    }

    bool AnnotationToolbar::currentTextBackground() const
    {
        return m_backgroundButton != nullptr && m_backgroundButton->isChecked();
    }

    void AnnotationToolbar::setTextBackground(const bool enabled)
    {
        if (m_backgroundButton != nullptr)
        {
            m_backgroundButton->setChecked(enabled);
            const auto label = enabled ? GyanisScribe::Base::uiText("背景：开") : GyanisScribe::Base::uiText("背景：关");
            m_backgroundButton->setToolTip(label);
            m_backgroundButton->setAccessibleName(label);
        }
        // 底板色跟着主开关收放：垫板开着才有得改
        updateContextVisibility();
        emit textBackgroundChanged(enabled);
    }

    void AnnotationToolbar::applyStoredDefaults()
    {
        const auto storedWidth = m_settings.intValue(Core::kAnnotationDefaultWidth, kAnnotationWidths[kDefaultWidthStep]);
        if (const auto step = stepIndexOf(kAnnotationWidths, storedWidth); step)
        {
            m_widthSlider->setValue(*step);
        } else
        {
            // 就近取一档会把「配置里存的是 5」这件事藏起来：界面摆了 4，用户以为自己要的那档生效了
            Base::Logger::warning(std::format("默认线宽 {} 不在阶梯（2/4/6/8）里，本次按默认档显示", storedWidth));
            m_widthSlider->setValue(kDefaultWidthStep);
        }

        const auto storedFontSize = m_settings.intValue(Core::kAnnotationDefaultFontSize, kFontSizes[kDefaultFontSizeIndex]);
        if (const auto index = stepIndexOf(kFontSizes, storedFontSize); index)
        {
            m_fontSizeCombo->setCurrentIndex(*index);
        } else
        {
            Base::Logger::warning(std::format("默认字号 {} 不在阶梯（12/13/14/16/20/24）里，本次按默认档显示", storedFontSize));
            m_fontSizeCombo->setCurrentIndex(kDefaultFontSizeIndex);
        }
    }

    void AnnotationToolbar::rememberDefaults()
    {
        // 构造期不回写：那时写的就是刚读出来的值（或坏值的兜底档），写回去会把用户文件里的坏值悄悄改掉
        if (m_stateIsInitial)
        {
            return;
        }
        if (auto written = m_settings.setValue(Core::kAnnotationDefaultWidth, currentWidth()); !written)
        {
            Base::Logger::warning(std::format("默认线宽没能保存：{}", written.error().message));
        }
        if (auto written = m_settings.setValue(Core::kAnnotationDefaultFontSize, currentFontSize()); !written)
        {
            Base::Logger::warning(std::format("默认字号没能保存：{}", written.error().message));
        }
    }

    void AnnotationToolbar::setActiveTool(const Core::ToolKind toolKind)
    {
        for (std::size_t index = 0; index < m_toolKinds.size(); ++index)
        {
            if (m_toolKinds[index] != toolKind)
            {
                continue;
            }
            // 只改按下态、不发信号：这是宿主把引擎状态回灌给界面，再发一次 toolChanged 就成回环。
            // setChecked 不会触发 clicked，因此这里不需要额外的挡信号手段
            m_toolButtons[index]->setChecked(true);
            // 记下当前工具：不记的话用户之后再点这一个会被上面的守卫吞掉，看起来像「按钮失灵」
            m_activeTool = toolKind;
            updateContextVisibility();
            return;
        }
    }

    void AnnotationToolbar::updateContextVisibility()
    {
        // 可适用性以 Core::AnnotationShape.h 各字段注释（「仅 X 用」）为准；加一档工具/参数要在这里对一次表
        const auto toolIs = [this](const std::initializer_list<Core::ToolKind> kinds) { return std::find(kinds.begin(), kinds.end(), m_activeTool) != kinds.end(); };
        // 「变没变」按显隐标记比而不是 isVisible()：构造期根窗口还没显示，isVisible() 恒假，
        // 拿它比会把每一枚都记成「刚变过」，于是每回都发一次没意义的 contextLayoutChanged
        bool       changed = false;
        const auto place   = [&changed](QWidget *widget, const bool visible)
        {
            if (widget == nullptr || visible == !widget->isHidden())
            {
                return;
            }
            widget->setVisible(visible);
            changed = true;
        };

        const bool text         = toolIs({Core::ToolKind::Text});
        const bool outlineOn    = m_outlineButton != nullptr && m_outlineButton->isChecked();
        const bool backgroundOn = m_backgroundButton != nullptr && m_backgroundButton->isChecked();

        // 线宽：描一条笔迹的那些工具用得上（橡皮擦按它算擦除宽度、高亮乘上加宽倍数）；马赛克/高斯模糊/
        // 文字/序号/放大镜不吃它，藏起来
        place(m_widthSlider, toolIs({Core::ToolKind::Rectangle, Core::ToolKind::Ellipse, Core::ToolKind::Arrow, Core::ToolKind::Pen, Core::ToolKind::Highlight,
                                     Core::ToolKind::Line, Core::ToolKind::Polyline, Core::ToolKind::Magnifier, Core::ToolKind::Eraser}));
        place(m_fillButton, toolIs({Core::ToolKind::Rectangle, Core::ToolKind::Ellipse}));
        place(m_roundedRectButton, toolIs({Core::ToolKind::Rectangle}));
        place(m_lineStyleButton, toolIs({Core::ToolKind::Rectangle, Core::ToolKind::Ellipse, Core::ToolKind::Arrow, Core::ToolKind::Pen, Core::ToolKind::Line,
                                         Core::ToolKind::Polyline, Core::ToolKind::Magnifier}));
        place(m_doubleArrowButton, toolIs({Core::ToolKind::Arrow}));
        place(m_reversedArrowButton, toolIs({Core::ToolKind::Arrow}));
        place(m_arrowHeadStyleButton, toolIs({Core::ToolKind::Arrow}));
        place(m_arrowHeadScaleButton, toolIs({Core::ToolKind::Arrow}));
        place(m_ellipticalButton, toolIs({Core::ToolKind::Mosaic, Core::ToolKind::Blur, Core::ToolKind::Highlight, Core::ToolKind::Eraser}));
        place(m_mosaicGrainButton, toolIs({Core::ToolKind::Mosaic}));
        place(m_highlightAlphaButton, toolIs({Core::ToolKind::Highlight}));
        place(m_highlightWidthButton, toolIs({Core::ToolKind::Highlight}));
        place(m_fontCombo, text);
        place(m_fontSizeCombo, text || toolIs({Core::ToolKind::Number}));
        place(m_boldButton, text);
        place(m_italicButton, text);
        place(m_outlineButton, text);
        // 描边色与描边粗细是描边开关的子档、底板色是底板开关的子档：主开关关着时它们改不了任何东西
        place(m_strokeColorButton, text && outlineOn);
        place(m_outlineWidthButton, text && outlineOn);
        place(m_backgroundButton, text);
        place(m_plateColorButton, text && backgroundOn);
        place(m_numberGroupButton, toolIs({Core::ToolKind::Number}));

        if (changed)
        {
            emit contextLayoutChanged();
        }
    }

    void AnnotationToolbar::setUndoRedoAvailable(const bool canUndo, const bool canRedo)
    {
        m_undoButton->setEnabled(canUndo);
        m_redoButton->setEnabled(canRedo);
    }

    int AnnotationToolbar::toolButtonCount()
    {
        return static_cast<int>(kToolButtonCount);
    }

    int AnnotationToolbar::colorButtonCount() const
    {
        // 颜色入口里能挑的颜色数：色板若干格 + 最近使用若干 + 自定义一枚
        return static_cast<int>(m_palette.size() + m_recentColors.size()) + 1;
    }

    QColor AnnotationToolbar::currentColor() const
    {
        return m_currentColor;
    }

    int AnnotationToolbar::currentWidth() const
    {
        return kAnnotationWidths[static_cast<std::size_t>(m_widthSlider->value())];
    }

    int AnnotationToolbar::currentFontSize() const
    {
        return m_fontSizeCombo->currentData().toInt();
    }

    QToolButton *AnnotationToolbar::toolButtonAt(const std::size_t index) const
    {
        return index < m_toolButtons.size() ? m_toolButtons[index] : nullptr;
    }

    QToolButton *AnnotationToolbar::makeButton(const std::string_view iconName, const QString &label, const bool checkable)
    {
        auto *button = new QToolButton{this};
        button->setIcon(m_icons.icon(iconName));
        button->setIconSize(QSize{static_cast<int>(IconSize::Medium), static_cast<int>(IconSize::Medium)});
        // tooltip 与无障碍名称同名：需求要求每个图标按钮两者都有，只有 tooltip 时读屏拿不到名字
        button->setToolTip(label);
        button->setAccessibleName(label);
        button->setCheckable(checkable);
        button->setAutoRaise(true);
        return button;
    }

    void AnnotationToolbar::chooseCustomColor()
    {
        const auto picked = QColorDialog::getColor(m_currentColor, this, GyanisScribe::Base::uiText("选择标注颜色"));
        if (!picked.isValid())
        {
            // 用户按了取消：什么都不发，颜色停在上一格
            return;
        }
        // 自定义色也记在「当前形状」那一格上，与色板那一排同一条路
        applyColorToActiveTool(picked);

        // 记进「最近使用」：菜单下次弹出时按这份清单重建，不必在这里再动按钮；
        // 落盘失败只记日志——颜色本身已经生效了，不该因为存不上就回退
        m_recentColors = Core::pushRecentColor(m_recentColors, picked);
        if (auto written = m_settings.setValue(Core::kAnnotationRecentColors, Core::serializeRecentColors(m_recentColors)); !written)
        {
            Base::Logger::warning(std::format("最近使用颜色没能保存：{}", written.error().message));
        }
    }

    void AnnotationToolbar::rebuildColorMenu()
    {
        m_colorMenu->clear();

        // 一格一枚色块，八列排开：色板在上、最近使用在下，最后是自定义取色的入口
        constexpr int kColorColumns = 8;
        auto         *panel         = new QWidget{m_colorMenu};
        auto         *grid          = new QGridLayout{panel};
        grid->setContentsMargins(8, 8, 8, 4);
        grid->setSpacing(4);

        int  slot      = 0;
        auto addSwatch = [&](const QColor &color, const QString &label)
        {
            auto *button = new QToolButton{panel};
            button->setAutoRaise(true);
            button->setIcon(QIcon{colorSwatchPixmap(color, m_icons.tintColor())});
            button->setIconSize(QSize{static_cast<int>(IconSize::Medium), static_cast<int>(IconSize::Medium)});
            button->setToolTip(label);
            button->setAccessibleName(label);
            QObject::connect(button, &QToolButton::clicked, this,
                             [this, color]
                             {
                                 applyColorToActiveTool(color);
                                 m_colorMenu->close();
                             });
            grid->addWidget(button, slot / kColorColumns, slot % kColorColumns);
            ++slot;
        };
        for (std::size_t index = 0; index < m_palette.size(); ++index)
        {
            addSwatch(m_palette[index], GyanisScribe::Base::uiText("颜色 %1").arg(static_cast<int>(index) + 1));
        }
        for (std::size_t index = 0; index < m_recentColors.size(); ++index)
        {
            addSwatch(m_recentColors[index], GyanisScribe::Base::uiText("最近使用 %1").arg(static_cast<int>(index) + 1));
        }

        auto *widgetAction = new QWidgetAction{m_colorMenu};
        widgetAction->setDefaultWidget(panel);
        m_colorMenu->addAction(widgetAction);

        m_colorMenu->addSeparator();
        QAction *custom = m_colorMenu->addAction(GyanisScribe::Base::uiText("自定义颜色…"));
        QObject::connect(custom, &QAction::triggered, this, &AnnotationToolbar::chooseCustomColor);
    }

    void AnnotationToolbar::applyColorToActiveTool(const QColor &color)
    {
        m_currentColor = color;
        // 按 Core::ToolKind 的底层值作键：往后加一档工具不必改这张表，也不用假设工具数与按钮数相等
        m_colorsByTool.insert(static_cast<int>(m_activeTool), color);
        updateOpacityHint();
        emit colorChanged(color);
    }

    void AnnotationToolbar::adoptColorForTool(const Core::ToolKind kind)
    {
        const auto found = m_colorsByTool.find(static_cast<int>(kind));
        // 这个形状没被挑过色就什么都不动：把用户正在用的颜色凭空换成色板第一格，比沿用更让人莫名
        if (found == m_colorsByTool.end() || found.value() == m_currentColor)
        {
            return;
        }
        m_currentColor = found.value();
        // 颜色按钮上的色块跟着换：它说的必须是现在这支色
        updateOpacityHint();
        emit colorChanged(m_currentColor);
    }

    int AnnotationToolbar::currentColorOpacityPercent() const
    {
        // 读数从颜色自己的 alpha 换算：再存一份「当前不透明度」就会与颜色分家（画的是 alpha，说的是另一格）
        return static_cast<int>(std::lround(static_cast<double>(m_currentColor.alpha()) * 100.0 / 255.0));
    }

    bool AnnotationToolbar::overColorRow(const QPoint &localPoint) const
    {
        // 颜色只剩这一枚按钮：滚轮落在它上面就是调不透明度
        return m_colorButton != nullptr && m_colorButton->geometry().contains(localPoint);
    }

    void AnnotationToolbar::updateOpacityHint()
    {
        refreshColorButton();
    }

    void AnnotationToolbar::refreshColorButton()
    {
        if (m_colorButton == nullptr)
        {
            return;
        }
        // 无障碍名保持「颜色」：那是这一枚按钮的本名，读屏要的是动作而不是状态
        m_colorButton->setIcon(QIcon{colorSwatchPixmap(m_currentColor, m_icons.tintColor())});
        m_colorButton->setToolTip(GyanisScribe::Base::uiText("颜色（当前不透明度 %1%；在这一枚上滚滚轮调节）").arg(currentColorOpacityPercent()));
    }

    void AnnotationToolbar::refreshToolKeyHints()
    {
        for (std::size_t index = 0; index < kToolTable.size(); ++index)
        {
            const auto label = std::get<2>(kToolTable[index]);
            const auto text  = GyanisScribe::Base::uiText(label);
            // 字母键位只进 tooltip：无障碍名要保持与标签同名，读屏报出来的才是工具本名而不是键位
            // 清册里找不到这一档（标识写歪）就只报工具名：括号里空一格，读的人会以为那颗键坏了
            const auto *entry = GyanisScribe::Gui::appShortcutEntry(std::get<3>(kToolTable[index]));
            m_toolButtons[index]->setToolTip(entry == nullptr ? text : text + QStringLiteral(" (") + GyanisScribe::Gui::appShortcutText(m_settings, *entry) + QStringLiteral(")"));
        }
    }

    void AnnotationToolbar::wheelEvent(QWheelEvent *event)
    {
        const auto notches = event->angleDelta().y() / 120; // 一格 120 度：与 Qt 的缺省档同解
        if (notches == 0 || !overColorRow(event->position().toPoint()))
        {
            // 不在色板上（或只是惯性滚了一小丝）就交回默认：滑块与下拉框自己吃轮，到不了这一档的也不该改色
            QWidget::wheelEvent(event);
            return;
        }

        const auto percent = std::clamp(currentColorOpacityPercent() + notches * kColorOpacityStepPercent, kMinimumColorOpacityPercent, kMaximumColorOpacityPercent);
        auto       dimmed  = m_currentColor;
        dimmed.setAlpha(static_cast<int>(std::lround(static_cast<double>(percent) * 255.0 / 100.0)));
        // 走同一条上色路：于是这一档的不透明度也记在当前形状那一格上，切回来还是这支淡色
        applyColorToActiveTool(dimmed);
        updateOpacityHint();
        event->accept();
    }

} // namespace GyanisScribe::Gui
