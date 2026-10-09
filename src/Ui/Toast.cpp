#include "Ui/Toast.h"

#include "Core/Logger.h"
#include "Ui/IconProvider.h"
#include "Ui/ThemeManager.h"

#include <QGuiApplication>
#include <QHBoxLayout>
#include <QLabel>
#include <QPointer>
#include <QScreen>
#include <QTimer>
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

namespace GyanisScribe::Ui
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

    /// 气泡本体：一个无边框、不抢焦点的顶层小窗
    class Toast::Bubble : public QWidget
    {
    public:
        explicit Bubble(IconProvider &icons) : QWidget(nullptr, Qt::ToolTip | Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint | Qt::WindowDoesNotAcceptFocus), m_icons(icons)
        {
            setAttribute(Qt::WA_ShowWithoutActivating);
            // 名称是给读屏与自动化测试的：一个没有名字的顶层窗在辅助技术里就是一块空白
            setAccessibleName(QStringLiteral("提示"));

            auto *layout = new QHBoxLayout{this};
            layout->setContentsMargins(kToastIconSpacing, kToastIconSpacing, kToastIconSpacing * 2, kToastIconSpacing);
            layout->setSpacing(kToastIconSpacing);

            m_icon = new QLabel{this};
            m_icon->setAccessibleName(QStringLiteral("提示图标"));
            m_text = new QLabel{this};
            m_text->setWordWrap(true);
            m_text->setTextInteractionFlags(Qt::TextSelectableByMouse);
            layout->addWidget(m_icon);
            layout->addWidget(m_text);
        }

        void setText(const QString &text, const ToastKind kind)
        {
            const auto accent = toastColorFor(kind, mCurrentPalette);

            m_text->setText(text);
            m_text->setStyleSheet(QStringLiteral("color: %1; background: transparent;").arg(accent.name(QColor::HexRgb)));
            // 图标跟着种类换：颜色一样时，认图形比认文字快
            const auto iconName = kind == ToastKind::Success ? std::string_view{"Check"} : (kind == ToastKind::Danger ? "Warning" : "Info");
            m_icon->setPixmap(m_icons.pixmap(iconName, IconSize::Medium));
            m_icon->setFixedSize(QSize{static_cast<int>(IconSize::Medium), static_cast<int>(IconSize::Medium)});
        }

        void setPalette(const ThemePalette &palette)
        {
            mCurrentPalette = palette;
            // 背景走面板色、边框走语义边框色：气泡是浮层，需求允许轻量阴影但禁玻璃拟态
            setStyleSheet(QStringLiteral("background-color: %1; border: 1px solid %2; border-radius: 8px;")
                                  .arg(mCurrentPalette.surface.name(QColor::HexRgb), mCurrentPalette.border.name(QColor::HexRgb)));
        }

        [[nodiscard]] QString currentText() const
        {
            return m_text->text();
        }

    private:
        IconProvider &m_icons;           ///< 图标出口，不拥有
        QLabel       *m_icon = nullptr;  ///< 种类图标
        QLabel       *m_text = nullptr;  ///< 提示正文
        ThemePalette  mCurrentPalette{}; ///< 当前主题的语义色，由 Toast 在显示前灌进来
    };

    Toast::Toast(IconProvider &icons, const ThemeManager &theme, QObject *parent) : QObject(parent), m_icons(icons), m_theme(theme)
    {
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

} // namespace GyanisScribe::Ui
