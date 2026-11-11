#include "Gui/Steps/StepRecorderHud.h"

#include "Base/Text.h"
#include "Core/Geometry/ContinuousCorner.h"
#include "Gui/Controls/Button.h"
#include "Gui/Theme/CornerStyle.h"
#include "Gui/Theme/DesignTokens.h"
#include "Gui/Theme/ThemeManager.h"

#include <QApplication>
#include <QHBoxLayout>
#include <QLabel>
#include <QPainter>
#include <QPalette>
#include <QPen>
#include <QPushButton>
#include <QString>

#include <algorithm>

namespace
{
    /// 控制条固定高度，逻辑像素：摆位与命中测试都按这一档算
    constexpr int kHudHeight = 48;

    /// 宽度下限：步数从 9 跳到 10 时控制条不该跟着抽一下
    constexpr int kHudMinimumWidth = 300;

    /// 状态圆点直径，逻辑像素
    constexpr int kHudDotDiameter = 10;
} // namespace

namespace GyanisScribe::Gui
{
    /**
     * @brief 控制条上的录制状态圆点
     * @details 圆点是一枚自绘填充椭圆：样式表只能把方框磨出圆角，渲染还要多过一层样式表引擎，
     *          缩放比例变化时边缘容易发虚；自绘直接按控件矩形画椭圆，直径与抗锯齿边缘都由
     *          本控件掌控。填充色由控制条按暂停态与主题写入。
     */
    class DotWidget final : public QWidget
    {
    public:
        /**
         * @brief 构造圆点并固定直径
         * @param parent 父窗口，通常就是控制条
         */
        explicit DotWidget(QWidget *parent) : QWidget{parent}
        {
            setFixedSize(kHudDotDiameter, kHudDotDiameter);
            setAttribute(Qt::WA_TransparentForMouseEvents);
        }

        /**
         * @brief 换填充色
         * @param color 圆点颜色
         */
        void setColor(const QColor &color)
        {
            if (m_color == color)
            {
                return;
            }
            m_color = color;
            update();
        }

    protected:
        /**
         * @brief 画圆点
         * @details 只画这枚填充椭圆，绘制事件本身不参与；控件区域之外的底色留给控制条面板
         */
        void paintEvent(QPaintEvent *) override
        {
            QPainter painter{this};
            painter.setRenderHint(QPainter::Antialiasing);
            painter.setPen(Qt::NoPen);
            painter.setBrush(m_color);
            painter.drawEllipse(QRectF{rect()});
        }

    private:
        QColor m_color; ///< 圆点填充色
    };

    StepRecorderHud::StepRecorderHud(ThemeManager &theme, QWidget *parent) : QWidget(parent), m_theme(theme)
    {
        // 工具窗 + 不吃焦点：用户接着要操作的是别的程序，控制条抢了焦点就点不中目标
        setWindowTitle(Base::uiText("步骤记录控制条"));
        setAccessibleName(Base::uiText("步骤记录控制条"));
        setWindowFlags(Qt::Tool | Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint);
        setObjectName(QStringLiteral("stepsHud"));
        setAttribute(Qt::WA_ShowWithoutActivating);
        // 圆角外不能落系统底色：面板由 paintEvent 画，窗口本身透明
        setAttribute(Qt::WA_TranslucentBackground);
        setAutoFillBackground(false);
        setFixedHeight(kHudHeight);

        auto *root = new QHBoxLayout{this};
        root->setContentsMargins(kSpacingMedium, kSpacingSmall, kSpacingMedium, kSpacingSmall);
        root->setSpacing(kSpacingSmall);

        m_dot = new DotWidget{this};
        m_dot->setObjectName(QStringLiteral("stepsHudDot"));
        m_dot->setAccessibleName(Base::uiText("录制状态"));
        root->addWidget(m_dot);

        m_count = new QLabel{this};
        m_count->setObjectName(QStringLiteral("stepsHudCount"));
        m_count->setAccessibleName(Base::uiText("已记录步数"));
        // 读数含中文，只上等宽数字，不换字族也不降字重
        applyTextRole(*m_count, TextRole::Metric, TextScript::Mixed);
        root->addWidget(m_count);
        root->addStretch(1);

        m_pause = new Button{Base::uiText("暂停"), this};
        m_pause->setObjectName(QStringLiteral("stepsHudPause"));
        m_pause->setAccessibleName(Base::uiText("暂停或继续步骤记录"));
        root->addWidget(m_pause);

        m_stop = new Button{Base::uiText("停止"), this};
        m_stop->setObjectName(QStringLiteral("stepsHudStop"));
        m_stop->setAccessibleName(Base::uiText("停止步骤记录"));
        m_stop->setToolTip(Base::uiText("结束录制并生成步骤列表。"));
        root->addWidget(m_stop);

        QObject::connect(m_pause, &QPushButton::clicked, this, &StepRecorderHud::pauseToggled);
        QObject::connect(m_stop, &QPushButton::clicked, this, &StepRecorderHud::stopRequested);
        QObject::connect(&m_theme, &ThemeManager::themeChanged, this,
                         [this]
                         {
                             refreshColors();
                             refreshDot();
                             update();
                         });

        refreshColors();
        setStepCount(0);
        setPaused(false);
    }

    StepRecorderHud::~StepRecorderHud() = default;

    void StepRecorderHud::setStepCount(const int stepCount)
    {
        m_count->setText(Base::uiText("步骤记录 · %1").arg(std::max(stepCount, 0)));
    }

    void StepRecorderHud::setPaused(const bool paused)
    {
        m_paused = paused;
        m_pause->setText(paused ? Base::uiText("继续") : Base::uiText("暂停"));
        // 提示跟着按钮当前的动作走：暂停后还写着「暂停」会让人以为按下去是继续暂停
        m_pause->setToolTip(paused ? Base::uiText("继续记录新的点击与窗口切换。") : Base::uiText("暂停后不再记录新的点击与窗口切换。"));
        refreshDot();
    }

    QSize StepRecorderHud::preferredSize() const
    {
        // 控件还没上屏时布局量不出宽度，而摆位恰恰要在 show 之前定下来
        const auto hinted = sizeHint();
        return QSize{std::max(hinted.width(), kHudMinimumWidth), kHudHeight};
    }

    QColor StepRecorderHud::dotColorForTesting() const
    {
        return m_paused ? m_pausedDot : m_danger;
    }

    QPoint StepRecorderHud::positionFor(const QRect &available, const QSize &hudSize)
    {
        return available.topLeft() + QPoint{(available.width() - hudSize.width()) / 2, 0};
    }

    void StepRecorderHud::paintEvent(QPaintEvent *)
    {
        QPainter painter{this};
        Core::paintContinuousCorner(painter, QRectF{rect()}, Core::cornerShapeFor(Core::CornerRole::Panel), m_surface,
                                    QPen{m_border, static_cast<qreal>(themeBorderWidth(activeThemeKind()))});
    }

    void StepRecorderHud::refreshColors()
    {
        const auto &palette  = m_theme.palette();
        const auto  fallback = QApplication::palette();

        // 主题档缺一色时退到应用调色板，控制条不能因为一个无效色块整条不可见
        m_surface   = palette.surface.isValid() ? palette.surface : fallback.color(QPalette::Window);
        m_text      = palette.text.isValid() ? palette.text : fallback.color(QPalette::WindowText);
        m_border    = palette.borderStrong.isValid() ? palette.borderStrong : fallback.color(QPalette::Mid);
        m_danger    = palette.danger.isValid() ? palette.danger : QColor{Qt::red};
        m_pausedDot = palette.secondaryText.isValid() ? palette.secondaryText : fallback.color(QPalette::Disabled, QPalette::WindowText);

        QPalette colors = fallback;
        colors.setColor(QPalette::Window, m_surface);
        colors.setColor(QPalette::Base, m_surface);
        colors.setColor(QPalette::Button, m_surface);
        colors.setColor(QPalette::WindowText, m_text);
        colors.setColor(QPalette::Text, m_text);
        colors.setColor(QPalette::ButtonText, m_text);
        setPalette(colors);
    }

    void StepRecorderHud::refreshDot()
    {
        m_dot->setColor(dotColorForTesting());
    }

} // namespace GyanisScribe::Gui
