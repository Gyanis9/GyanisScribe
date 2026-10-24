#include "Gui/Controls/Toast.h"

#include "Base/Logger.h"
#include "Base/Text.h"
#include "Gui/Controls/StreamingText.h"
#include "Gui/Resources/IconProvider.h"
#include "Gui/Theme/Motion.h"
#include "Gui/Theme/ThemeManager.h"

#include <QEvent>
#include <QFontInfo>
#include <QFontMetrics>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QLabel>
#include <QPainter>
#include <QPixmap>
#include <QPointer>
#include <QScreen>
#include <QTimer>
#include <QVBoxLayout>
#include <QWindow>

#include <algorithm>

namespace
{
    /// 气泡的最小宽度，逻辑像素：两个字的提示也不该缩成一条竖着的胶囊
    constexpr int kToastMinimumWidth = 180;

    /// 气泡的最大宽度，逻辑像素：超过这个宽度一行字就太长，读起来要转头
    constexpr int kToastMaximumWidth = 420;

    /// 图标与文字之间的间距，取 4/8/12/16 阶梯里的 8
    constexpr int kToastIconSpacing = 8;
} // namespace

namespace GyanisScribe::Gui
{
    QColor toastColorFor(const ToastKind kind, const ThemePalette &palette)
    {
        switch (kind)
        {
            case ToastKind::Success:
                return palette.success;
            case ToastKind::Danger:
                return palette.danger;
            case ToastKind::Information:
                break;
        }
        // 中性信息用次要文本色：它不该抢操作区的注意力，但又要读得出
        return palette.secondaryText;
    }

    QPoint toastPositionFor(const QRect &screenAvailable, const QSize &toastSize)
    {
        if (screenAvailable.isEmpty() || toastSize.isEmpty())
        {
            return {};
        }

        // 右下角留边距；尺寸超过可用区时夹到屏幕内，露一半的气泡比不出现更难读
        const auto x = std::max(screenAvailable.left(), screenAvailable.right() - toastSize.width() - kToastMargin + 1);
        const auto y = std::max(screenAvailable.top(), screenAvailable.bottom() - toastSize.height() - kToastMargin + 1);
        return {x, y};
    }

    bool shouldStreamToast(const ToastKind kind, const bool animationsEnabled) noexcept
    {
        return animationsEnabled && kind != ToastKind::Danger;
    }

    int iconTextTopPad(const int iconHeight, const int ascent, const int emHeight, const int iconMargin) noexcept
    {
        return std::max(0, qRound(iconMargin + iconHeight - ascent - 0.125 * emHeight));
    }

    namespace
    {
        /**
         * @brief 气泡左侧的种类图标
         * @details 成功提示从 0.6 弹到 1.0；四周留出 6px，过冲到约 1.08 倍时字形仍在控件内。
         */
        class SpringIcon final : public QWidget
        {
        public:
            /// 图标四周的留白，逻辑像素
            static constexpr int kMargin = 6;

            /**
             * @brief 构造图标控件
             * @param parent Qt 父控件
             */
            explicit SpringIcon(QWidget *parent = nullptr) : QWidget{parent}
            {
                setAttribute(Qt::WA_TransparentForMouseEvents);
                setFocusPolicy(Qt::NoFocus);
                setAccessibleName(GyanisScribe::Base::uiText("提示图标"));
                m_scale.setSpec(kPopSpring);
                m_scale.setImmediate(1.0);
                QObject::connect(&m_scale, &SpringValue::valueChanged, this, [this](qreal) { update(); });
            }

            /**
             * @brief 换一枚图标
             * @param pixmap 已按语义色光栅化的位图
             * @details 尺寸随位图逻辑尺寸加两侧留白固定下来，布局不会因为换图标而跳。
             */
            void setPixmap(const QPixmap &pixmap)
            {
                m_icon = pixmap;
                setFixedSize(sizeHint());
                update();
            }

            /// 从 0.6 弹到 1.0；动画关闭时直接停在终值，不闪一下 0.6
            void pop()
            {
                if (!m_scale.animationsEnabled())
                {
                    m_scale.setImmediate(1.0);
                    return;
                }
                m_scale.setImmediate(0.6);
                m_scale.animateTo(1.0);
            }

            /// 收回终值：上一条成功提示的弹簧还在跑时，换到不弹的种类不能接着播
            void reset()
            {
                m_scale.setImmediate(1.0);
            }

            /**
             * @brief 图标控件的建议尺寸
             * @return 位图的逻辑尺寸加两侧留白；位图为空时为零
             */
            [[nodiscard]] QSize sizeHint() const override
            {
                if (m_icon.isNull())
                {
                    return {};
                }
                const auto logical = m_icon.deviceIndependentSize().toSize();
                return logical + QSize{2 * kMargin, 2 * kMargin};
            }

        protected:
            /**
             * @brief 按当前弹簧比例居中画图标
             * @details 重写 QWidget::paintEvent()：缩放围绕控件中心，字形不会从左上角往外长；
             *          目标矩形按位图的逻辑尺寸算，高 DPI 副本不会被按物理像素放大一圈。
             */
            void paintEvent(QPaintEvent *) override
            {
                if (m_icon.isNull())
                {
                    return;
                }
                QPainter painter{this};
                painter.setRenderHint(QPainter::SmoothPixmapTransform);
                const QSizeF logical = m_icon.deviceIndependentSize();
                const QSizeF scaled  = logical * m_scale.value();
                const QRectF target{(width() - scaled.width()) / 2.0, (height() - scaled.height()) / 2.0, scaled.width(), scaled.height()};
                painter.drawPixmap(target, m_icon, QRectF{QPointF(0.0, 0.0), QSizeF(m_icon.size())});
            }

        private:
            QPixmap     m_icon;  ///< 按语义色光栅化的图标
            SpringValue m_scale; ///< 入场缩放，静息为 1.0
        };
    } // namespace

    /// 气泡本体：一个无边框、不抢焦点的顶层小窗
    class Toast::Bubble : public QWidget
    {
    public:
        explicit Bubble(IconProvider &icons) : QWidget(nullptr, Qt::ToolTip | Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint | Qt::WindowDoesNotAcceptFocus), m_icons(icons)
        {
            setAttribute(Qt::WA_ShowWithoutActivating);
            // 名称是给读屏与自动化测试的：一个没有名字的顶层窗在辅助技术里就是一块空白
            setAccessibleName(GyanisScribe::Base::uiText("提示"));

            auto *layout = new QHBoxLayout{this};
            // 图标自带 kMargin 的过冲余量：从外边距与间距里扣掉，字形离气泡边、离文字仍是 8px
            layout->setContentsMargins(kToastIconSpacing - SpringIcon::kMargin, kToastIconSpacing - SpringIcon::kMargin, kToastIconSpacing * 2, kToastIconSpacing);
            layout->setSpacing(kToastIconSpacing - SpringIcon::kMargin);

            m_icon = new SpringIcon{this};

            auto *textColumn   = new QWidget{this};
            auto *columnLayout = new QVBoxLayout{textColumn};
            // 气泡的底色与胶囊描边只画在气泡上，文字列保持透明
            textColumn->setStyleSheet(QStringLiteral("background: transparent; border: none;"));
            columnLayout->setSpacing(0);

            m_text = new QLabel{textColumn};
            m_text->setWordWrap(true);
            m_text->setTextInteractionFlags(Qt::TextSelectableByMouse);
            // 覆盖层从标签左上角画起，原生文字也钉在同一处，动画收尾时不会跳一行
            m_text->setAlignment(Qt::AlignLeft | Qt::AlignTop);
            const QFont font = m_text->font();
            // em 高取真实像素字号；QFontMetrics::height() 是行距，会把 -0.125em 的契约撑大
            m_iconTextPad = iconTextTopPad(static_cast<int>(IconSize::Medium), QFontMetrics{font}.ascent(), QFontInfo{font}.pixelSize(), SpringIcon::kMargin);
            columnLayout->setContentsMargins(0, m_iconTextPad, 0, 0);
            columnLayout->addWidget(m_text);

            layout->addWidget(m_icon, 0, Qt::AlignTop);
            layout->addWidget(textColumn, 1);
        }

        void setText(const QString &text, const ToastKind kind)
        {
            // 旧覆盖层的定时器还在跑，先收掉，否则它会把新文字重新盖住
            removeOverlay();
            m_kind = kind;

            const auto accent = toastColorFor(kind, m_currentPalette);
            m_iconTint        = accent;

            m_text->setText(text);
            m_text->setStyleSheet(QStringLiteral("color: %1; background: transparent;").arg(accent.name(QColor::HexRgb)));
            // 图标跟着种类换：颜色一样时，认图形比认文字快
            const auto iconName = kind == ToastKind::Success ? std::string_view{"Check"} : (kind == ToastKind::Danger ? "Warning" : "Info");
            // 按气泡所在屏幕的缩放比取图，高 DPI 上不靠 QPainter 放大糊掉
            m_icon->setPixmap(m_icons.pixmap(iconName, IconSize::Medium, devicePixelRatioF(), accent));
        }

        void setPalette(const ThemePalette &palette)
        {
            m_currentPalette = palette;
            // 背景走面板色、边框走浮层微边缘那一档（borderStrong）：气泡是浮层，不铺投影，
            // 靠这一圈细边与底下的内容分开；圆角取胶囊档——半高以上的大圆角由 Qt 按高度收敛
            setStyleSheet(QStringLiteral("background-color: %1; border: 1px solid %2; border-radius: 999px;")
                                  .arg(m_currentPalette.surface.name(QColor::HexRgb), m_currentPalette.borderStrong.name(QColor::HexRgb)));
        }

        /**
         * @brief 播放入场动效
         * @param animate 是否允许动画；false 时图标直接到 1.0，也不出流式覆盖层
         */
        void startMotion(const bool animate)
        {
            if (m_kind == ToastKind::Success)
            {
                m_icon->pop();
            }
            else
            {
                // 上一条成功提示的弹簧可能还在跑：不收回终值，信息与错误图标会凭空弹一下
                m_icon->reset();
            }
            if (!shouldStreamToast(m_kind, animate) || m_text->text().isEmpty())
            {
                return;
            }
            m_overlay = new StreamingTextOverlay{m_text, m_text->text(), m_text->font(), toastColorFor(m_kind, m_currentPalette), m_currentPalette.surface};
            m_overlay->setGeometry(m_text->rect());
            m_overlay->show();
            m_overlay->raise();
            QObject::connect(m_overlay.data(), &StreamingTextOverlay::finished, m_overlay.data(), &QObject::deleteLater);
            m_overlay->start();
        }

        [[nodiscard]] QString currentText() const
        {
            return m_text->text();
        }

        [[nodiscard]] bool hasStreamingOverlay() const
        {
            return m_overlay != nullptr && !m_overlay->isFinished();
        }

        [[nodiscard]] int streamingOverlayCount() const
        {
            return static_cast<int>(m_text->findChildren<StreamingTextOverlay *>().size());
        }

        [[nodiscard]] QColor currentIconTint() const
        {
            return m_iconTint;
        }

        [[nodiscard]] int currentIconTextPad() const
        {
            return m_iconTextPad;
        }

        /**
         * @brief 把覆盖层的流式动画向前推指定毫秒
         * @param milliseconds 本次推进的毫秒数
         * @details 时间轴到头后覆盖层会先停一帧再收工；测试读口顺手把这一帧走掉，
         *          相当于 16ms 后的那次 tick，用例不必等真实时钟。
         */
        void advanceStreamingForTesting(const int milliseconds)
        {
            if (m_overlay == nullptr || m_overlay->isFinished())
            {
                return;
            }
            m_overlay->advanceForTesting(milliseconds);
            if (m_overlay != nullptr && !m_overlay->isFinished())
            {
                m_overlay->advanceForTesting(0);
            }
        }

    protected:
        /**
         * @brief 气泡隐藏时收掉流式覆盖层
         * @details 重写 QWidget::hideEvent()：覆盖层的帧定时器不随气泡隐藏停下，
         *          留着会继续推进并在下一次显示时盖住新文字。
         * @param event 隐藏事件
         */
        void hideEvent(QHideEvent *event) override
        {
            removeOverlay();
            QWidget::hideEvent(event);
        }

    private:
        /// 立刻销毁流式覆盖层，并让指针回到空
        void removeOverlay()
        {
            if (m_overlay == nullptr)
            {
                return;
            }
            m_overlay->hide();
            delete m_overlay.data();
        }

        IconProvider                  &m_icons;                         ///< 图标出口，不拥有
        SpringIcon                    *m_icon = nullptr;                ///< 种类图标
        QLabel                        *m_text = nullptr;                ///< 提示正文，流式层收工后露出的真身
        QPointer<StreamingTextOverlay> m_overlay;                       ///< 流式覆盖层，只在动画期间存在
        ToastKind                      m_kind      = ToastKind::Information; ///< 当前提示的种类
        QColor                         m_iconTint;                           ///< 当前图标的描边色，等于该条提示的语义色
        int                            m_iconTextPad = 0;                    ///< 文字列的上边距，按标签真实字体算出
        ThemePalette                   m_currentPalette{};                   ///< 当前主题的语义色，由 Toast 在显示前灌进来
    };

    Toast::Toast(IconProvider &icons, const ThemeManager &theme, QObject *parent) : QObject(parent), m_icons(icons), m_theme(theme)
    {
    }

    Toast::~Toast()
    {
        // 气泡是没有父控件的顶层窗口：提示器销毁时它不会自己跟着走
        delete m_bubble;
    }

    void Toast::show(const QString &text, const ToastKind kind, const QRect &screenAvailable)
    {
        if (m_bubble == nullptr)
        {
            m_bubble = new Bubble{m_icons};
        }

        auto available = screenAvailable;
        if (available.isEmpty())
        {
            // 没指定屏幕就落主屏可用区：任务栏上方那块地方才是「不遮挡操作区」的位置
            if (const auto *screen = QGuiApplication::primaryScreen(); screen != nullptr)
            {
                available = screen->availableGeometry();
            }
        }

        // 每次显示都现取主题色：主题刚换过就冒出来的提示不能还穿着旧衣
        m_bubble->setStyleSheet({});
        m_bubble->setPalette(m_theme.palette());
        m_bubble->setText(text, kind);
        m_bubble->adjustSize();
        const auto width = std::clamp(m_bubble->sizeHint().width(), kToastMinimumWidth, kToastMaximumWidth);
        m_bubble->resize(width, m_bubble->sizeHint().height());
        m_bubble->move(toastPositionFor(available, m_bubble->size()));
        m_bubble->show();
        m_bubble->raise();
        // 几何落定、标签排好版之后才播：覆盖层按标签矩形建层
        m_bubble->startMotion(animationsEnabled());

        // 新提示顶掉旧的气泡：排队会让三秒前的操作看起来像刚发生
        QPointer<Bubble> guard{m_bubble};
        QTimer::singleShot(kToastVisibleMs, this,
                           [guard]
                           {
                               if (guard != nullptr)
                               {
                                   guard->hide();
                               }
                           });
    }

    QString Toast::currentText() const
    {
        return m_bubble == nullptr ? QString{} : m_bubble->currentText();
    }

    bool Toast::hasStreamingOverlay() const
    {
        return m_bubble != nullptr && m_bubble->hasStreamingOverlay();
    }

    int Toast::streamingOverlayCount() const
    {
        return m_bubble == nullptr ? 0 : m_bubble->streamingOverlayCount();
    }

    QColor Toast::currentIconTint() const
    {
        return m_bubble == nullptr ? QColor{} : m_bubble->currentIconTint();
    }

    int Toast::currentIconTextPad() const
    {
        return m_bubble == nullptr ? 0 : m_bubble->currentIconTextPad();
    }

    void Toast::advanceStreamingForTesting(const int milliseconds)
    {
        if (m_bubble != nullptr)
        {
            m_bubble->advanceStreamingForTesting(milliseconds);
        }
    }

} // namespace GyanisScribe::Gui
