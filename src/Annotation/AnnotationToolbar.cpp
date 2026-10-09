#include "Annotation/AnnotationToolbar.h"

#include "Annotation/AnnotationTool.h"
#include "Ui/DesignTokens.h"

#include <QButtonGroup>
#include <QColorDialog>
#include <QComboBox>
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
#include <string_view>
#include <tuple>

namespace
{
    using GyanisScribe::Annotation::ToolKind;
    using GyanisScribe::Ui::IconSize;
    using GyanisScribe::Ui::kAnnotationPalette;
    using GyanisScribe::Ui::kAnnotationWidths;
    using GyanisScribe::Ui::kFontSizes;

    /// 中文字面量走 UTF-8：源文件是 UTF-8，而 QString::fromUtf8 需要显式的字节序列
    QString fromUtf8Text(const std::string_view text)
    {
        return QString::fromUtf8(text.data(), static_cast<qsizetype>(text.size()));
    }

    /**
     * @brief 工具按钮的登记表：顺序、图标名与中文名一处对齐
     * @details 顺序必须与 ToolKind 的声明顺序一致，用例按序号逐个核图标名与标签。
     */
    constexpr std::array<std::tuple<ToolKind, std::string_view, std::string_view>, 8> kToolTable = {
            std::tuple{ToolKind::Rectangle, "Rectangle", "矩形"}, std::tuple{ToolKind::Ellipse, "Ellipse", "椭圆"},
            std::tuple{ToolKind::Arrow, "Arrow", "箭头"},         std::tuple{ToolKind::Pen, "Pen", "画笔"},
            std::tuple{ToolKind::Highlight, "Highlight", "高亮"}, std::tuple{ToolKind::Mosaic, "Mosaic", "马赛克"},
            std::tuple{ToolKind::Text, "Text", "文字"},           std::tuple{ToolKind::Number, "Number", "序号"},
    };

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
        return separator;
    }
} // namespace

namespace GyanisScribe::Annotation
{
    AnnotationToolbar::AnnotationToolbar(const Ui::IconProvider &icons, QWidget *parent) : QWidget(parent), m_icons(icons), m_activeTool(ToolKind::Rectangle)
    {
        setObjectName(QStringLiteral("AnnotationToolbar"));

        auto *layout = new QHBoxLayout{this};
        layout->setContentsMargins(Ui::kToolbarPadding, Ui::kToolbarPadding, Ui::kToolbarPadding, Ui::kToolbarPadding);
        layout->setSpacing(Ui::kToolbarPadding);

        // 工具组：互斥交给 QButtonGroup，这样鼠标点击与 setActiveTool 走的是同一套按钮状态
        auto *toolGroup = new QButtonGroup{this};
        toolGroup->setExclusive(true);
        for (std::size_t index = 0; index < kToolTable.size(); ++index)
        {
            const auto [kind, iconName, label] = kToolTable[index];
            auto *button                       = makeButton(iconName, fromUtf8Text(label), true);
            m_toolButtons[index]               = button;
            m_toolKinds[index]                 = kind;
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
                                 emit toolChanged(kind);
                             });
            layout->addWidget(button);
        }
        layout->addWidget(makeSeparator());

        // 颜色组：八个预设色块加一枚取色按钮，同样互斥，自定义色选中时也只亮一个
        auto *colorGroup = new QButtonGroup{this};
        colorGroup->setExclusive(true);
        for (std::size_t index = 0; index < kAnnotationPalette.size(); ++index)
        {
            const auto color  = kAnnotationPalette[index];
            auto      *button = makeButton("Close", QStringLiteral("颜色 %1").arg(index + 1), true);
            // 图标换成色块：makeButton 那份图标只保证按钮尺寸合适，色块才是这一格要表达的东西
            button->setIcon(QIcon{colorSwatchPixmap(color, m_icons.tintColor())});
            m_colorButtons[index] = button;
            colorGroup->addButton(button, static_cast<int>(index));
            QObject::connect(button, &QToolButton::clicked, this,
                             [this, color]
                             {
                                 m_currentColor = color;
                                 emit colorChanged(color);
                             });
            layout->addWidget(button);
        }
        m_customColorButton = makeButton("ColorPicker", QStringLiteral("自定义颜色"), true);
        colorGroup->addButton(m_customColorButton, static_cast<int>(kAnnotationPalette.size()));
        QObject::connect(m_customColorButton, &QToolButton::clicked, this, &AnnotationToolbar::chooseCustomColor);
        layout->addWidget(m_customColorButton);
        layout->addWidget(makeSeparator());

        // 线宽：滑块只走阶梯，取值表与 Ui::kAnnotationWidths 同解，不会出现 3.5 这种没定义过的宽度
        m_widthSlider = new QSlider{Qt::Horizontal, this};
        m_widthSlider->setRange(0, static_cast<int>(kAnnotationWidths.size()) - 1);
        m_widthSlider->setPageStep(1);
        m_widthSlider->setFixedWidth(Ui::kToolbarSliderWidth);
        m_widthSlider->setAccessibleName(QStringLiteral("线宽"));
        m_widthSlider->setToolTip(QStringLiteral("线宽"));
        QObject::connect(m_widthSlider, &QSlider::valueChanged, this,
                         [this](int step)
                         {
                             if (step >= 0 && static_cast<std::size_t>(step) < kAnnotationWidths.size())
                             {
                                 emit widthChanged(kAnnotationWidths[static_cast<std::size_t>(step)]);
                             }
                         });
        layout->addWidget(m_widthSlider);

        // 字号：需求给的是离散阶梯（12/13/14/16/20/24），用下拉而不是滑块，避免出现 15 这种没定义的值
        m_fontSizeCombo = new QComboBox{this};
        for (const auto size: kFontSizes)
        {
            m_fontSizeCombo->addItem(QString::number(size), size);
        }
        m_fontSizeCombo->setAccessibleName(QStringLiteral("字号"));
        m_fontSizeCombo->setToolTip(QStringLiteral("字号"));
        QObject::connect(m_fontSizeCombo, &QComboBox::currentIndexChanged, this,
                         [this](int index)
                         {
                             if (index >= 0 && index < m_fontSizeCombo->count())
                             {
                                 emit fontSizeChanged(m_fontSizeCombo->itemData(index).toInt());
                             }
                         });
        layout->addWidget(m_fontSizeCombo);
        layout->addWidget(makeSeparator());

        m_undoButton = makeButton("Undo", QStringLiteral("撤销（Ctrl+Z）"), false);
        m_redoButton = makeButton("Redo", QStringLiteral("重做（Ctrl+Y）"), false);
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
                ActionSpec{"Copy", QStringLiteral("复制到剪贴板"), &AnnotationToolbar::copyClicked},
                ActionSpec{"Save", QStringLiteral("保存为文件"), &AnnotationToolbar::saveClicked},
                ActionSpec{"Pin", QStringLiteral("贴到屏幕上"), &AnnotationToolbar::pinClicked},
                ActionSpec{"Ocr", QStringLiteral("识别文字"), &AnnotationToolbar::ocrClicked},
        };
        for (const auto &action: actions)
        {
            auto *button = makeButton(action.iconName, action.label, false);
            QObject::connect(button, &QToolButton::clicked, this, [this, signal = action.signal] { (this->*signal)(); });
            layout->addWidget(button);
        }

        // 起点状态：第一个工具、第一个色块按下，线宽取 4、字号取 14。
        // 此刻还没有任何外部接收方（宿主是在构造之后才接线的），所以不需要挡信号，
        // 只需要把状态摆对——尤其是 m_activeTool 要与按下的那个工具同解，否则第一次点矩形会被守卫吞掉
        toolGroup->button(static_cast<int>(ToolKind::Rectangle))->setChecked(true);
        colorGroup->button(0)->setChecked(true);
        m_widthSlider->setValue(1);
        m_fontSizeCombo->setCurrentIndex(2);
        m_currentColor = kAnnotationPalette[0];
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

    int AnnotationToolbar::colorButtonCount()
    {
        return static_cast<int>(Ui::kAnnotationPalette.size()) + 1;
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
        const auto picked = QColorDialog::getColor(m_currentColor, this, QStringLiteral("选择标注颜色"));
        if (!picked.isValid())
        {
            // 用户按了取消：什么都不发，颜色停在上一格
            return;
        }
        m_currentColor = picked;
        emit colorChanged(picked);
    }

} // namespace GyanisScribe::Annotation
