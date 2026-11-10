#include "Gui/Annotation/CaptureInfoPanel.h"

#include "Core/Geometry/ContinuousCorner.h"
#include "Base/Text.h"
#include "Gui/Controls/Button.h"
#include "Gui/Theme/CornerStyle.h"
#include "Gui/Theme/DesignTokens.h"

#include <QFileInfo>
#include <QFontMetrics>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QPainter>
#include <QPalette>
#include <QPen>
#include <QSizePolicy>
#include <QToolButton>
#include <QVBoxLayout>

namespace
{
    /// 面板固定宽度，逻辑像素：四行里最长的是窗口标题，320 放得下常见标题又不至于占掉半屏
    constexpr int kPanelWidth = 320;

    /// 一行值文本的可用宽度，逻辑像素：放不下的部分按右侧省略号收，完整内容进悬停提示
    constexpr int kPanelValueWidth = 250;

    /// 关闭按钮边长，与工具栏图标同一档
    constexpr int kCloseButtonEdge = 20;

    /**
     * @brief 造一行左侧的说明标签
     * @param text 说明文字
     * @param parent 父控件
     * @return 说明标签，用次要文字色，与右侧的值拉开层级
     */
    QLabel *makeCaption(const QString &text, QWidget *parent)
    {
        auto *label = new QLabel{text, parent};
        GyanisScribe::Gui::applyTextRole(*label, GyanisScribe::Gui::TextRole::Body);
        QPalette palette = label->palette();
        palette.setColor(QPalette::WindowText, GyanisScribe::Gui::activePalette().secondaryText);
        label->setPalette(palette);
        return label;
    }

    /**
     * @brief 造一行右侧的值标签
     * @param objectName 对象名，用例与调试按它取那一项
     * @param parent 父控件
     * @return 值标签，正文色，纯文本显示
     */
    QLabel *makeValueLabel(const QString &objectName, QWidget *parent)
    {
        auto *label = new QLabel{parent};
        label->setObjectName(objectName);
        label->setTextFormat(Qt::PlainText);
        label->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
        GyanisScribe::Gui::applyTextRole(*label, GyanisScribe::Gui::TextRole::Body);
        QPalette palette = label->palette();
        palette.setColor(QPalette::WindowText, GyanisScribe::Gui::activePalette().text);
        label->setPalette(palette);
        return label;
    }

    /**
     * @brief 把一段文本写进值标签，过长时右侧收省略号
     * @param label 目标值标签
     * @param text 完整文本
     * @details 居中的值标签按面板宽度定宽，过长只收显示；完整文本留在悬停提示里，
     *          标题被截断时用户仍能看清全部。
     */
    void applyValueText(QLabel &label, const QString &text)
    {
        const auto shown = label.fontMetrics().elidedText(text, Qt::ElideRight, kPanelValueWidth);
        label.setText(shown);
        label.setToolTip(shown == text ? QString{} : text);
    }
} // namespace

namespace GyanisScribe::Gui
{
    CaptureInfoPanel::CaptureInfoPanel(QWidget *parent) : QWidget(parent)
    {
        // 工具窗 + 置顶：面板跟着标注窗浮在桌面上，但自己不抢焦点、不进任务栏
        setWindowTitle(GyanisScribe::Base::uiText("截图信息"));
        setAccessibleName(GyanisScribe::Base::uiText("截图信息"));
        setWindowFlags(Qt::Tool | Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint);
        setObjectName(QStringLiteral("captureInfoPanel"));
        // 圆角外不能落系统底色：面板与描边由 paintEvent 画，窗口本身透明
        setAttribute(Qt::WA_TranslucentBackground);
        setAttribute(Qt::WA_ShowWithoutActivating);
        setAutoFillBackground(false);
        setFocusPolicy(Qt::NoFocus);
        setFixedWidth(kPanelWidth);

        auto *root = new QVBoxLayout{this};
        root->setContentsMargins(kSpacingMedium, kSpacingMedium, kSpacingMedium, kSpacingMedium);
        root->setSpacing(kSpacingSmall);

        auto *header = new QHBoxLayout;
        header->setSpacing(kSpacingSmall);
        root->addLayout(header);

        auto *heading = new QLabel{GyanisScribe::Base::uiText("截图信息"), this};
        heading->setAccessibleName(GyanisScribe::Base::uiText("截图信息"));
        GyanisScribe::Gui::applyTextRole(*heading, GyanisScribe::Gui::TextRole::Body);
        QPalette headingPalette = heading->palette();
        headingPalette.setColor(QPalette::WindowText, GyanisScribe::Gui::activePalette().text);
        heading->setPalette(headingPalette);
        header->addWidget(heading);
        header->addStretch(1);

        m_closeButton = new Gui::ToolButton{this};
        m_closeButton->setObjectName(QStringLiteral("captureInfoClose"));
        m_closeButton->setText(QStringLiteral("×"));
        m_closeButton->setFixedSize(kCloseButtonEdge, kCloseButtonEdge);
        m_closeButton->setAutoRaise(true);
        m_closeButton->setFocusPolicy(Qt::NoFocus);
        m_closeButton->setToolTip(GyanisScribe::Base::uiText("关闭信息面板"));
        m_closeButton->setAccessibleName(GyanisScribe::Base::uiText("关闭信息面板"));
        QObject::connect(m_closeButton, &QToolButton::clicked, this, &CaptureInfoPanel::closePanel);
        header->addWidget(m_closeButton);

        auto *grid = new QGridLayout;
        grid->setHorizontalSpacing(kSpacingSmall);
        grid->setVerticalSpacing(kSpacingTiny);
        grid->setColumnMinimumWidth(0, 32);
        grid->setColumnStretch(1, 1);
        root->addLayout(grid);

        m_appValue     = makeValueLabel(QStringLiteral("captureInfoApp"), this);
        m_titleValue   = makeValueLabel(QStringLiteral("captureInfoTitle"), this);
        m_monitorValue = makeValueLabel(QStringLiteral("captureInfoMonitor"), this);
        m_timeValue    = makeValueLabel(QStringLiteral("captureInfoTime"), this);

        grid->addWidget(makeCaption(GyanisScribe::Base::uiText("应用"), this), 0, 0);
        grid->addWidget(m_appValue, 0, 1);
        grid->addWidget(makeCaption(GyanisScribe::Base::uiText("窗口"), this), 1, 0);
        grid->addWidget(m_titleValue, 1, 1);
        grid->addWidget(makeCaption(GyanisScribe::Base::uiText("屏幕"), this), 2, 0);
        grid->addWidget(m_monitorValue, 2, 1);
        grid->addWidget(makeCaption(GyanisScribe::Base::uiText("时间"), this), 3, 0);
        grid->addWidget(m_timeValue, 3, 1);
    }

    CaptureInfoPanel::~CaptureInfoPanel() = default;

    void CaptureInfoPanel::setSource(const Core::CaptureSourceInfo &source)
    {
        // 应用只留进程名的主名：notepad.exe 与 C:\...\notepad.exe 都显示成 notepad
        const auto processStem = QFileInfo{source.processName.trimmed()}.completeBaseName().trimmed();
        applyValueText(*m_appValue, processStem.isEmpty() ? GyanisScribe::Base::uiText("未知应用") : processStem);

        const auto title = source.windowTitle;
        applyValueText(*m_titleValue, title.trimmed().isEmpty() ? GyanisScribe::Base::uiText("未知窗口") : title);

        const auto screenName = source.screenName;
        applyValueText(*m_monitorValue, screenName.trimmed().isEmpty() ? GyanisScribe::Base::uiText("未知屏幕") : screenName);

        const auto timeText = source.capturedAt.isValid() ? source.capturedAt.toString(QStringLiteral("yyyy-MM-dd HH:mm:ss")) : GyanisScribe::Base::uiText("未知时间");
        applyValueText(*m_timeValue, timeText);
    }

    QSize CaptureInfoPanel::preferredSize() const
    {
        // 控件没上屏时 sizeHint 可能量不出来，而摆放恰恰要在 show 之前定下来；
        // 给一份按内容算的下限，比退化成 0x0 有用
        const auto hinted = sizeHint();
        return hinted.isValid() && !hinted.isEmpty() ? hinted : QSize{kPanelWidth, 132};
    }

    void CaptureInfoPanel::paintEvent(QPaintEvent *)
    {
        QPainter painter{this};
        Core::paintContinuousCorner(painter, QRectF{rect()}, Core::cornerShapeFor(Core::CornerRole::Panel), activePalette().surface,
                                    QPen{activePalette().borderStrong, static_cast<qreal>(themeBorderWidth(activeThemeKind()))});
    }

    void CaptureInfoPanel::keyPressEvent(QKeyEvent *event)
    {
        if (event->key() == Qt::Key_Escape && event->modifiers() == Qt::NoModifier)
        {
            closePanel();
            event->accept();
            return;
        }
        QWidget::keyPressEvent(event);
    }

    void CaptureInfoPanel::closePanel()
    {
        hide();
        emit closed();
    }

} // namespace GyanisScribe::Gui
