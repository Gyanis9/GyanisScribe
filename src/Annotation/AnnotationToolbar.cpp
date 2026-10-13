#include "Annotation/AnnotationToolbar.h"

#include "Annotation/AnnotationTool.h"
#include "Pick/RecentColors.h"
#include "Core/Logger.h"
#include "Core/Text.h"
#include "Settings/ISettingsStore.h"
#include "Settings/SettingsKeys.h"
#include "Ui/DesignTokens.h"

#include <QButtonGroup>
#include <QColorDialog>
#include <QComboBox>
#include <QFontComboBox>
#include <QFrame>
#include <QHBoxLayout>
#include <QIcon>
#include <QPainter>
#include <QPen>
#include <QPixmap>
#include <QSlider>
#include <QString>
#include <QToolButton>

#include <array>
#include <format>
#include <string_view>
#include <tuple>

namespace
{
    using GyanisScribe::Annotation::ToolKind;
    using GyanisScribe::Ui::IconSize;
    using GyanisScribe::Ui::kAnnotationPalette;
    using GyanisScribe::Ui::kAnnotationWidths;
    using GyanisScribe::Ui::kDefaultAnnotationFontSizeIndex;
    using GyanisScribe::Ui::kDefaultAnnotationWidthStep;
    using GyanisScribe::Ui::kFontSizes;
    using GyanisScribe::Ui::stepIndexOf;

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
        QPixmap    bitmap(side, side);
        bitmap.fill(Qt::transparent);

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

namespace GyanisScribe::Annotation
{
    AnnotationToolbar::AnnotationToolbar(Settings::ISettingsStore &settings, const Ui::IconProvider &icons, QWidget *parent) :
        QWidget(parent), m_settings(settings), m_icons(icons), m_activeTool(ToolKind::Rectangle)
    {
        setObjectName(QStringLiteral("AnnotationToolbar"));
        // 样式表给这个对象名画了一圈卡片底；QWidget 子类不打开这个开关就不会吃样式表的背景
        setAttribute(Qt::WA_StyledBackground, true);

        auto *layout = new QHBoxLayout{this};
        m_layout     = layout;
        layout->setContentsMargins(Ui::kSpacingSmall, Ui::kSpacingSmall, Ui::kSpacingSmall, Ui::kSpacingSmall);
        layout->setSpacing(Ui::kSpacingSmall);

        // 工具组：互斥交给 QButtonGroup，这样鼠标点击与 setActiveTool 走的是同一套按钮状态
        auto *toolGroup = new QButtonGroup{this};
        toolGroup->setExclusive(true);
        for (std::size_t index = 0; index < kToolTable.size(); ++index)
        {
            const auto [kind, iconName, label, shortcut] = kToolTable[index];
            auto *button                                 = makeButton(iconName, GyanisScribe::Core::uiText(label.data()), true);
            // 字母快捷键只进 tooltip：无障碍名要保持与标签同名，读屏报出来的才是工具本名而不是键位
            button->setToolTip(GyanisScribe::Core::uiText(label.data()) + QStringLiteral(" (") + QString::fromUtf8(shortcut.data()) + QStringLiteral(")"));
            m_toolButtons[index] = button;
            m_toolKinds[index]   = kind;
            // id 取工具序号：与 ToolKind 的底层值同解，反向查按钮时不必再存一张表
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
                                 emit toolChanged(kind);
                             });
            layout->addWidget(button);
        }
        layout->addWidget(makeSeparator());

        // 颜色组：八个预设色块、最近使用的色块与一枚取色按钮，同样互斥，自定义色选中时也只亮一个
        auto *colorGroup = new QButtonGroup{this};
        m_colorGroup     = colorGroup;
        colorGroup->setExclusive(true);
        for (std::size_t index = 0; index < kAnnotationPalette.size(); ++index)
        {
            const auto color  = kAnnotationPalette[index];
            auto      *button = makeButton("Close", GyanisScribe::Core::uiText("颜色 %1").arg(index + 1), true);
            // 图标换成色块：makeButton 那份图标只保证按钮尺寸合适，色块才是这一格要表达的东西
            button->setIcon(QIcon{colorSwatchPixmap(color, m_icons.tintColor())});
            m_colorButtons[index] = button;
            colorGroup->addButton(button, static_cast<int>(index));
            QObject::connect(button, &QToolButton::clicked, this, [this, color] { applyColorToActiveTool(color); });
            layout->addWidget(button);
        }
        // 最近使用颜色：读回上一次留下的清单，插在预设色板之后、自定义按钮之前。
        // 记下这个插入点，之后每次重建成品色块都要落回同一排位置
        m_recentInsertIndex = layout->count();
        if (const auto parsed = Pick::parseRecentColors(m_settings.stringListValue(Settings::kAnnotationRecentColors, {})); parsed)
        {
            m_recentColors = *parsed;
            // 手改过的配置文件可能超出上限：按上限截断，色板不会被撑到放不下
            if (m_recentColors.size() > Pick::kRecentColorCapacity)
            {
                m_recentColors.resize(Pick::kRecentColorCapacity);
            }
        } else
        {
            Core::Logger::warning(std::format("最近使用颜色读不出：{}，本次从空清单开始", parsed.error().message));
        }
        rebuildRecentColors();

        m_customColorButton = makeButton("ColorPicker", GyanisScribe::Core::uiText("自定义颜色"), true);
        // 固定编号：最近使用的色块从 kAnnotationPalette.size() 起排，用相邻编号会撞车
        colorGroup->addButton(m_customColorButton, kCustomColorButtonId);
        QObject::connect(m_customColorButton, &QToolButton::clicked, this, &AnnotationToolbar::chooseCustomColor);
        layout->addWidget(m_customColorButton);
        layout->addWidget(makeSeparator());

        // 线宽：滑块只走阶梯，取值表与 Ui::kAnnotationWidths 同解，不会出现 3.5 这种没定义过的宽度
        m_widthSlider = new QSlider{Qt::Horizontal, this};
        m_widthSlider->setRange(0, static_cast<int>(kAnnotationWidths.size()) - 1);
        m_widthSlider->setPageStep(1);
        m_widthSlider->setFixedWidth(Ui::kToolbarSliderWidth);
        m_widthSlider->setAccessibleName(GyanisScribe::Core::uiText("线宽"));
        m_widthSlider->setToolTip(GyanisScribe::Core::uiText("线宽"));
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
        m_fontSizeCombo->setAccessibleName(GyanisScribe::Core::uiText("字号"));
        m_fontSizeCombo->setToolTip(GyanisScribe::Core::uiText("字号"));
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
        m_fillButton = makeButton("Fill", GyanisScribe::Core::uiText("填充"), false);
        QObject::connect(m_fillButton, &QToolButton::clicked, this, &AnnotationToolbar::cycleFillStyle);
        layout->addWidget(m_fillButton);
        // 圆角矩形：矩形工具用得上，是一个保持按下态的开关（与双向箭头同一套口径）
        m_roundedRectButton = makeButton("RoundedRect", GyanisScribe::Core::uiText("圆角"), true);
        QObject::connect(m_roundedRectButton, &QToolButton::clicked, this, [this] { setRoundedRect(m_roundedRectButton->isChecked()); });
        layout->addWidget(m_roundedRectButton);
        // 线型：描边类工具用得上，点一下走一档（实线 → 虚线 → 点线）
        m_lineStyleButton = makeButton("LineStyle", GyanisScribe::Core::uiText("线型"), false);
        QObject::connect(m_lineStyleButton, &QToolButton::clicked, this, &AnnotationToolbar::cycleLineStyle);
        layout->addWidget(m_lineStyleButton);
        // 双向箭头：箭头工具用得上，是一个保持按下态的开关。同样不进互斥组，
        // 点击时 Qt 先翻 checked 再发 clicked，所以这里读按钮态而不是自己翻一遍
        m_doubleArrowButton = makeButton("ArrowDouble", GyanisScribe::Core::uiText("双向箭头"), true);
        QObject::connect(m_doubleArrowButton, &QToolButton::clicked, this, [this] { setDoubleHeaded(m_doubleArrowButton->isChecked()); });
        layout->addWidget(m_doubleArrowButton);
        // 椭圆形状：马赛克/高斯模糊/高亮/橡皮擦四件用得上，同为保持按下态的开关；
        // 名字带「形状」是为了与工具组里那枚「椭圆」区分——那枚是切工具，这枚是切当前工具的落笔形状
        m_ellipticalButton = makeButton("EllipseShape", GyanisScribe::Core::uiText("椭圆形状"), true);
        QObject::connect(m_ellipticalButton, &QToolButton::clicked, this, [this] { setElliptical(m_ellipticalButton->isChecked()); });
        layout->addWidget(m_ellipticalButton);
        // 马赛克粒度：马赛克工具用得上，点一下走一档（细 → 中 → 粗），档位表是 kMosaicBlockSizes
        m_mosaicGrainButton = makeButton("MosaicGrain", GyanisScribe::Core::uiText("马赛克粒度"), false);
        QObject::connect(m_mosaicGrainButton, &QToolButton::clicked, this, &AnnotationToolbar::cycleMosaicBlockSize);
        layout->addWidget(m_mosaicGrainButton);
        // 高亮透明度：高亮工具用得上，点一下走一档（轻 → 中 → 重），档位表是 kHighlightAlphas
        m_highlightAlphaButton = makeButton("HighlightOpacity", GyanisScribe::Core::uiText("高亮透明度"), false);
        QObject::connect(m_highlightAlphaButton, &QToolButton::clicked, this, &AnnotationToolbar::cycleHighlightAlpha);
        layout->addWidget(m_highlightAlphaButton);
        // 高亮加宽：高亮工具用得上，点一下走一档（窄 → 中 → 宽），档位表是 kHighlightWidthFactors
        m_highlightWidthButton = makeButton("HighlightWidth", GyanisScribe::Core::uiText("高亮加宽"), false);
        QObject::connect(m_highlightWidthButton, &QToolButton::clicked, this, &AnnotationToolbar::cycleHighlightWidthFactor);
        layout->addWidget(m_highlightWidthButton);
        // 箭头款式：箭头工具用得上，点一下走一档（实心 → 空心 → 开口）
        m_arrowHeadStyleButton = makeButton("ArrowHeadStyle", GyanisScribe::Core::uiText("箭头样式"), false);
        QObject::connect(m_arrowHeadStyleButton, &QToolButton::clicked, this, &AnnotationToolbar::cycleArrowHeadStyle);
        layout->addWidget(m_arrowHeadStyleButton);
        // 箭头大小：箭头工具用得上，点一下走一档（小 → 中 → 大），档位表是 kArrowHeadScales
        m_arrowHeadScaleButton = makeButton("ArrowHeadSize", GyanisScribe::Core::uiText("箭头大小"), false);
        QObject::connect(m_arrowHeadScaleButton, &QToolButton::clicked, this, &AnnotationToolbar::cycleArrowHeadScale);
        layout->addWidget(m_arrowHeadScaleButton);
        // 文字字体族：文字工具用得上，列系统字体。宽度封顶——字体名长的很，放任 sizeHint 会撑出去
        m_fontCombo = new QFontComboBox{this};
        m_fontCombo->setMaximumWidth(Ui::kToolbarFontComboWidth);
        m_fontCombo->setAccessibleName(GyanisScribe::Core::uiText("字体"));
        m_fontCombo->setToolTip(GyanisScribe::Core::uiText("字体（文字）"));
        QObject::connect(m_fontCombo, &QFontComboBox::currentFontChanged, this, [this](const QFont &font) { emit fontFamilyChanged(font.family()); });
        layout->addWidget(m_fontCombo);
        layout->addWidget(m_fontSizeCombo);
        // 粗体/斜体：文字工具用得上，保持按下态的开关，与双向箭头同一套口径
        m_boldButton = makeButton("Bold", GyanisScribe::Core::uiText("粗体"), true);
        QObject::connect(m_boldButton, &QToolButton::clicked, this, [this] { setTextBold(m_boldButton->isChecked()); });
        layout->addWidget(m_boldButton);
        m_italicButton = makeButton("Italic", GyanisScribe::Core::uiText("斜体"), true);
        QObject::connect(m_italicButton, &QToolButton::clicked, this, [this] { setTextItalic(m_italicButton->isChecked()); });
        layout->addWidget(m_italicButton);
        // 描边/底板：文字工具用得上，同为保持按下态的开关
        m_outlineButton = makeButton("TextOutline", GyanisScribe::Core::uiText("描边"), true);
        QObject::connect(m_outlineButton, &QToolButton::clicked, this, [this] { setTextOutline(m_outlineButton->isChecked()); });
        layout->addWidget(m_outlineButton);
        // 描边色：描边开着时用哪一色，点一下走一档（自动 → 白 → 黑）
        m_strokeColorButton = makeButton("StrokeColor", GyanisScribe::Core::uiText("描边色"), false);
        QObject::connect(m_strokeColorButton, &QToolButton::clicked, this, &AnnotationToolbar::cycleTextStrokeColor);
        layout->addWidget(m_strokeColorButton);
        // 计数组：序号工具用得上，点一下走一档（第 1 组 → 第 2 组 → 第 3 组 → 第 1 组）
        m_numberGroupButton = makeButton("NumberGroup", GyanisScribe::Core::uiText("计数组"), false);
        QObject::connect(m_numberGroupButton, &QToolButton::clicked, this, &AnnotationToolbar::cycleNumberGroup);
        layout->addWidget(m_numberGroupButton);
        m_backgroundButton = makeButton("TextBackground", GyanisScribe::Core::uiText("背景"), true);
        QObject::connect(m_backgroundButton, &QToolButton::clicked, this, [this] { setTextBackground(m_backgroundButton->isChecked()); });
        layout->addWidget(m_backgroundButton);
        layout->addWidget(makeSeparator());

        m_undoButton = makeButton("Undo", GyanisScribe::Core::uiText("撤销（Ctrl+Z）"), false);
        m_redoButton = makeButton("Redo", GyanisScribe::Core::uiText("重做（Ctrl+Y）"), false);
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
                ActionSpec{"Copy", GyanisScribe::Core::uiText("复制到剪贴板"), &AnnotationToolbar::copyClicked},
                ActionSpec{"Save", GyanisScribe::Core::uiText("保存为文件"), &AnnotationToolbar::saveClicked},
                ActionSpec{"Pin", GyanisScribe::Core::uiText("贴到屏幕上"), &AnnotationToolbar::pinClicked},
                ActionSpec{"Ocr", GyanisScribe::Core::uiText("识别文字"), &AnnotationToolbar::ocrClicked},
                ActionSpec{"QrCode", GyanisScribe::Core::uiText("识别二维码"), &AnnotationToolbar::scanClicked},
        };
        for (const auto &action: actions)
        {
            auto *button = makeButton(action.iconName, action.label, false);
            QObject::connect(button, &QToolButton::clicked, this, [this, signal = action.signal] { (this->*signal)(); });
            layout->addWidget(button);
        }

        // 起点状态：第一个工具、第一个色块按下，线宽与字号取「上次用的那一档」（配置里没有就回到 4 / 14）。
        // 此刻还没有任何外部接收方（宿主是在构造之后才接线的），所以不需要挡信号，
        // 只需要把状态摆对——尤其是 m_activeTool 要与按下的那个工具同解，否则第一次点矩形会被守卫吞掉
        toolGroup->button(static_cast<int>(ToolKind::Rectangle))->setChecked(true);
        colorGroup->button(0)->setChecked(true);
        applyStoredDefaults();
        m_stateIsInitial = false;
        m_currentColor   = kAnnotationPalette[0];
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
            const auto label = m_fillStyle == 0   ? GyanisScribe::Core::uiText("填充：无")
                               : m_fillStyle == 1 ? GyanisScribe::Core::uiText("填充：半透明")
                                                  : GyanisScribe::Core::uiText("填充：实心");
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
            const auto label = enabled ? GyanisScribe::Core::uiText("圆角：开") : GyanisScribe::Core::uiText("圆角：关");
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
        m_lineStyle = (m_lineStyle + 1) % static_cast<int>(kAnnotationLineStyles.size());
        if (m_lineStyleButton != nullptr)
        {
            // 与另几档同一套口径：提示就是这一档的读数
            const auto label = m_lineStyle == 0   ? GyanisScribe::Core::uiText("线型：实线")
                               : m_lineStyle == 1 ? GyanisScribe::Core::uiText("线型：虚线")
                                                  : GyanisScribe::Core::uiText("线型：点线");
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
            const auto label = enabled ? GyanisScribe::Core::uiText("双向箭头：开") : GyanisScribe::Core::uiText("双向箭头：关");
            m_doubleArrowButton->setToolTip(label);
            m_doubleArrowButton->setAccessibleName(label);
        }
        emit doubleHeadedChanged(enabled);
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
            const auto label = enabled ? GyanisScribe::Core::uiText("椭圆形状：椭圆") : GyanisScribe::Core::uiText("椭圆形状：矩形");
            m_ellipticalButton->setToolTip(label);
            m_ellipticalButton->setAccessibleName(label);
        }
        emit ellipticalChanged(enabled);
    }

    int AnnotationToolbar::currentMosaicBlockSize() const
    {
        return kMosaicBlockSizes[static_cast<std::size_t>(m_mosaicBlockStep)];
    }

    void AnnotationToolbar::cycleMosaicBlockSize()
    {
        m_mosaicBlockStep = (m_mosaicBlockStep + 1) % static_cast<int>(kMosaicBlockSizes.size());
        if (m_mosaicGrainButton != nullptr)
        {
            // 档名落在提示里：一枚图标说不清现在是哪一档，鼠标一停就该看得见
            const auto label = m_mosaicBlockStep == 0   ? GyanisScribe::Core::uiText("马赛克粒度：细")
                               : m_mosaicBlockStep == 1 ? GyanisScribe::Core::uiText("马赛克粒度：中")
                                                        : GyanisScribe::Core::uiText("马赛克粒度：粗");
            m_mosaicGrainButton->setToolTip(label);
            m_mosaicGrainButton->setAccessibleName(label);
        }
        emit mosaicBlockSizeChanged(currentMosaicBlockSize());
    }

    int AnnotationToolbar::currentHighlightAlpha() const
    {
        return kHighlightAlphas[static_cast<std::size_t>(m_highlightAlphaStep)];
    }

    void AnnotationToolbar::cycleHighlightAlpha()
    {
        m_highlightAlphaStep = (m_highlightAlphaStep + 1) % static_cast<int>(kHighlightAlphas.size());
        if (m_highlightAlphaButton != nullptr)
        {
            // 与粒度同一套口径：提示就是这一档的读数
            const auto label = m_highlightAlphaStep == 0   ? GyanisScribe::Core::uiText("高亮透明度：轻")
                               : m_highlightAlphaStep == 1 ? GyanisScribe::Core::uiText("高亮透明度：中")
                                                           : GyanisScribe::Core::uiText("高亮透明度：重");
            m_highlightAlphaButton->setToolTip(label);
            m_highlightAlphaButton->setAccessibleName(label);
        }
        emit highlightAlphaChanged(currentHighlightAlpha());
    }

    int AnnotationToolbar::currentHighlightWidthFactor() const
    {
        return kHighlightWidthFactors[static_cast<std::size_t>(m_highlightWidthStep)];
    }

    void AnnotationToolbar::cycleHighlightWidthFactor()
    {
        m_highlightWidthStep = (m_highlightWidthStep + 1) % static_cast<int>(kHighlightWidthFactors.size());
        if (m_highlightWidthButton != nullptr)
        {
            // 与另两档同一套口径：提示就是这一档的读数
            const auto label = m_highlightWidthStep == 0   ? GyanisScribe::Core::uiText("高亮加宽：窄")
                               : m_highlightWidthStep == 1 ? GyanisScribe::Core::uiText("高亮加宽：中")
                                                           : GyanisScribe::Core::uiText("高亮加宽：宽");
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
        m_arrowHeadStyle = (m_arrowHeadStyle + 1) % kArrowHeadStyleCount;
        if (m_arrowHeadStyleButton != nullptr)
        {
            // 与另几档同一套口径：提示就是这一档的读数
            const auto label = m_arrowHeadStyle == kArrowHeadFilled   ? GyanisScribe::Core::uiText("箭头样式：实心")
                               : m_arrowHeadStyle == kArrowHeadHollow ? GyanisScribe::Core::uiText("箭头样式：空心")
                                                                      : GyanisScribe::Core::uiText("箭头样式：开口");
            m_arrowHeadStyleButton->setToolTip(label);
            m_arrowHeadStyleButton->setAccessibleName(label);
        }
        emit arrowHeadStyleChanged(currentArrowHeadStyle());
    }

    qreal AnnotationToolbar::currentArrowHeadScale() const
    {
        return kArrowHeadScales[static_cast<std::size_t>(m_arrowHeadScaleStep)];
    }

    void AnnotationToolbar::cycleArrowHeadScale()
    {
        m_arrowHeadScaleStep = (m_arrowHeadScaleStep + 1) % static_cast<int>(kArrowHeadScales.size());
        if (m_arrowHeadScaleButton != nullptr)
        {
            // 与另几档同一套口径：提示就是这一档的读数
            const auto label = m_arrowHeadScaleStep == 0   ? GyanisScribe::Core::uiText("箭头大小：小")
                               : m_arrowHeadScaleStep == 1 ? GyanisScribe::Core::uiText("箭头大小：中")
                                                           : GyanisScribe::Core::uiText("箭头大小：大");
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
            const auto label = enabled ? GyanisScribe::Core::uiText("粗体：开") : GyanisScribe::Core::uiText("粗体：关");
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
            const auto label = enabled ? GyanisScribe::Core::uiText("斜体：开") : GyanisScribe::Core::uiText("斜体：关");
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
            const auto label = enabled ? GyanisScribe::Core::uiText("描边：开") : GyanisScribe::Core::uiText("描边：关");
            m_outlineButton->setToolTip(label);
            m_outlineButton->setAccessibleName(label);
        }
        emit textOutlineChanged(enabled);
    }

    int AnnotationToolbar::currentTextStrokeColorStep() const
    {
        return m_textStrokeColorStep;
    }

    void AnnotationToolbar::cycleTextStrokeColor()
    {
        m_textStrokeColorStep = (m_textStrokeColorStep + 1) % kTextStrokeColorCount;
        if (m_strokeColorButton != nullptr)
        {
            // 与另几档同一套口径：提示就是这一档的读数
            const auto label = m_textStrokeColorStep == kTextStrokeColorWhite   ? GyanisScribe::Core::uiText("描边色：白")
                               : m_textStrokeColorStep == kTextStrokeColorBlack ? GyanisScribe::Core::uiText("描边色：黑")
                                                                                : GyanisScribe::Core::uiText("描边色：自动");
            m_strokeColorButton->setToolTip(label);
            m_strokeColorButton->setAccessibleName(label);
        }
        emit textStrokeColorStepChanged(m_textStrokeColorStep);
    }

    int AnnotationToolbar::currentNumberGroup() const
    {
        return m_numberGroup;
    }

    void AnnotationToolbar::cycleNumberGroup()
    {
        m_numberGroup = m_numberGroup % kNumberGroupCount + 1;
        if (m_numberGroupButton != nullptr)
        {
            // 与另几档同一套口径：提示就是这一档的读数
            const auto label = m_numberGroup == 1   ? GyanisScribe::Core::uiText("计数组：第 1 组")
                               : m_numberGroup == 2 ? GyanisScribe::Core::uiText("计数组：第 2 组")
                                                    : GyanisScribe::Core::uiText("计数组：第 3 组");
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
            const auto label = enabled ? GyanisScribe::Core::uiText("背景：开") : GyanisScribe::Core::uiText("背景：关");
            m_backgroundButton->setToolTip(label);
            m_backgroundButton->setAccessibleName(label);
        }
        emit textBackgroundChanged(enabled);
    }

    void AnnotationToolbar::applyStoredDefaults()
    {
        const auto storedWidth = m_settings.intValue(Settings::kAnnotationDefaultWidth, kAnnotationWidths[kDefaultWidthStep]);
        if (const auto step = stepIndexOf(kAnnotationWidths, storedWidth); step)
        {
            m_widthSlider->setValue(*step);
        } else
        {
            // 就近取一档会把「配置里存的是 5」这件事藏起来：界面摆了 4，用户以为自己要的那档生效了
            Core::Logger::warning(std::format("默认线宽 {} 不在阶梯（2/4/6/8）里，本次按默认档显示", storedWidth));
            m_widthSlider->setValue(kDefaultWidthStep);
        }

        const auto storedFontSize = m_settings.intValue(Settings::kAnnotationDefaultFontSize, kFontSizes[kDefaultFontSizeIndex]);
        if (const auto index = stepIndexOf(kFontSizes, storedFontSize); index)
        {
            m_fontSizeCombo->setCurrentIndex(*index);
        } else
        {
            Core::Logger::warning(std::format("默认字号 {} 不在阶梯（12/13/14/16/20/24）里，本次按默认档显示", storedFontSize));
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
        if (auto written = m_settings.setValue(Settings::kAnnotationDefaultWidth, currentWidth()); !written)
        {
            Core::Logger::warning(std::format("默认线宽没能保存：{}", written.error().message));
        }
        if (auto written = m_settings.setValue(Settings::kAnnotationDefaultFontSize, currentFontSize()); !written)
        {
            Core::Logger::warning(std::format("默认字号没能保存：{}", written.error().message));
        }
    }

    void AnnotationToolbar::setActiveTool(const ToolKind toolKind)
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
            return;
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
        // 预设八格 + 最近使用若干 + 自定义一枚；最近使用是空的时候这一格与改动前逐字相同
        return static_cast<int>(Ui::kAnnotationPalette.size() + m_recentButtons.size()) + 1;
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
        const auto picked = QColorDialog::getColor(m_currentColor, this, GyanisScribe::Core::uiText("选择标注颜色"));
        if (!picked.isValid())
        {
            // 用户按了取消：什么都不发，颜色停在上一格
            return;
        }
        // 自定义色也记在「当前形状」那一格上，与色板那两排同一条路
        applyColorToActiveTool(picked);

        // 记进「最近使用」并立刻重建色块行：需求要求这一排跟着用户最近的动作走，
        // 落盘失败只记日志——颜色本身已经生效了，不该因为存不上就回退
        m_recentColors = Pick::pushRecentColor(m_recentColors, picked);
        rebuildRecentColors();
        if (auto written = m_settings.setValue(Settings::kAnnotationRecentColors, Pick::serializeRecentColors(m_recentColors)); !written)
        {
            Core::Logger::warning(std::format("最近使用颜色没能保存：{}", written.error().message));
        }
    }

    void AnnotationToolbar::applyColorToActiveTool(const QColor &color)
    {
        m_currentColor = color;
        // 按 ToolKind 的底层值作键：往后加一档工具不必改这张表，也不用假设工具数与按钮数相等
        m_colorsByTool.insert(static_cast<int>(m_activeTool), color);
        emit colorChanged(color);
    }

    void AnnotationToolbar::adoptColorForTool(const ToolKind kind)
    {
        const auto found = m_colorsByTool.find(static_cast<int>(kind));
        // 这个形状没被挑过色就什么都不动：把用户正在用的颜色凭空换成色板第一格，比沿用更让人莫名
        if (found == m_colorsByTool.end() || found.value() == m_currentColor)
        {
            return;
        }
        m_currentColor = found.value();
        // 按下的那一格要跟着走：互斥组会把别的放开，留着旧按下态就是在说「现在是上一个形状的颜色」
        syncColorButtonChecked();
        emit colorChanged(m_currentColor);
    }

    void AnnotationToolbar::syncColorButtonChecked()
    {
        for (std::size_t index = 0; index < m_colorButtons.size(); ++index)
        {
            if (m_colorButtons[index] != nullptr && Ui::kAnnotationPalette[index] == m_currentColor)
            {
                m_colorButtons[index]->setChecked(true);
                return;
            }
        }
        for (std::size_t index = 0; index < m_recentButtons.size() && index < m_recentColors.size(); ++index)
        {
            if (m_recentButtons[index] != nullptr && m_recentColors[index] == m_currentColor)
            {
                m_recentButtons[index]->setChecked(true);
                return;
            }
        }
        // 两排色块都对不上就是自定义色：按下那一枚
        if (m_customColorButton != nullptr)
        {
            m_customColorButton->setChecked(true);
        }
    }

    void AnnotationToolbar::rebuildRecentColors()
    {
        // 先摘旧的：编号与布局位置都要按当前清单重排，留着上一批会插出两排色块
        for (auto it = m_recentButtons.rbegin(); it != m_recentButtons.rend(); ++it)
        {
            m_colorGroup->removeButton(*it);
            m_layout->removeWidget(*it);
            delete *it;
        }
        m_recentButtons.clear();

        for (std::size_t index = 0; index < m_recentColors.size(); ++index)
        {
            const auto color  = m_recentColors[index];
            auto      *button = makeButton("Close", GyanisScribe::Core::uiText("最近使用 %1").arg(static_cast<int>(index) + 1), true);
            // 图标换成色块：与预设那八格同一个画法，两排放在一起才看不出是两套控件
            button->setIcon(QIcon{colorSwatchPixmap(color, m_icons.tintColor())});
            m_colorGroup->addButton(button, static_cast<int>(Ui::kAnnotationPalette.size() + index));
            QObject::connect(button, &QToolButton::clicked, this, [this, color] { applyColorToActiveTool(color); });
            m_layout->insertWidget(m_recentInsertIndex + static_cast<int>(index), button);
            m_recentButtons.push_back(button);
        }
    }

} // namespace GyanisScribe::Annotation
