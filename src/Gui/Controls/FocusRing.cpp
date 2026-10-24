#include "Gui/Controls/FocusRing.h"

#include "Gui/Controls/HotkeyEdit.h"

#include <QAbstractButton>
#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QLineEdit>
#include <QPaintEvent>
#include <QPainter>
#include <QSpinBox>
#include <QStyle>
#include <QStyleOptionButton>
#include <QTextEdit>
#include <QToolButton>

#include <utility>

namespace
{
    /// 覆盖层相对目标控件外扩的像素：内环 2px、空隙 2px、外环 2px，两环都落在覆盖层内
    constexpr int kOverlayMargin = 6;

    /// 外环的不透明度：足够看出第二圈，又不与内环抢注意力
    constexpr float kOuterRingOpacity = 0.32F;

    /// 环宽：内环与外环同一笔宽
    constexpr qreal kRingWidth = 2.0;

    /// 判断控件是否落在标注工具栏里
    /// 工具栏是 26px 紧凑档、行距 2px，6px 外扩的环会压到相邻按钮上，改由 QSS 的 1px 焦点边缘表达
    bool inAnnotationToolbar(QWidget *widget)
    {
        for (QWidget *parent = widget; parent != nullptr; parent = parent->parentWidget())
        {
            if (parent->objectName() == QStringLiteral("AnnotationToolbar"))
            {
                return true;
            }
        }
        return false;
    }

    /// 把可编辑下拉框/微调框内部的行编辑映射回外层控件
    /// 焦点由内部行编辑持有，但环必须画在整个字段上，不能只圈中间那段文字
    QWidget *unwrapInput(QWidget *widget)
    {
        if (auto *lineEdit = qobject_cast<QLineEdit *>(widget); lineEdit != nullptr)
        {
            if (auto *combo = qobject_cast<QComboBox *>(lineEdit->parentWidget()); combo != nullptr)
            {
                return combo;
            }
            if (auto *spin = qobject_cast<QSpinBox *>(lineEdit->parentWidget()); spin != nullptr)
            {
                return spin;
            }
        }
        return widget;
    }

    /// 按控件类型取圆角，与 QSS 里对应控件的 border-radius 对齐
    int radiusFor(const QWidget &widget)
    {
        if (qobject_cast<const QCheckBox *>(&widget) != nullptr)
        {
            return 5;
        }
        if (qobject_cast<const QToolButton *>(&widget) != nullptr)
        {
            return 8;
        }
        return 12;
    }
} // namespace

namespace GyanisScribe::Gui
{
    FocusRingOverlay::FocusRingOverlay(QWidget *target, const FocusRingStyle style, const bool indicatorOnly, ThemePalette palette, QWidget *parent) :
        QWidget(parent), m_target(target), m_style(style), m_indicatorOnly(indicatorOnly), m_palette(std::move(palette))
    {
        // 覆盖层只是画布：不吃鼠标、不抢焦点、不参与 Tab 顺序，也不带无障碍名字
        setAttribute(Qt::WA_TransparentForMouseEvents);
        setAttribute(Qt::WA_ShowWithoutActivating);
        setAttribute(Qt::WA_TranslucentBackground);
        setFocusPolicy(Qt::NoFocus);
        setAccessibleName(QString{});

        // 父控件的样式表会把背景色继承下来；这里自己声明透明，避免整块矩形盖住目标控件
        setStyleSheet(QStringLiteral("background: transparent;"));
    }

    void FocusRingOverlay::setPalette(const ThemePalette &palette)
    {
        m_palette = palette;
        update();
    }

    QRect FocusRingOverlay::targetRectInParent() const
    {
        if (m_target.isNull() || m_target->parentWidget() == nullptr)
        {
            return {};
        }
        if (!m_indicatorOnly)
        {
            return m_target->geometry();
        }

        // 复选框的环只套指示器：用样式自己的 subElementRect 取，不按字号与内边距另算一份
        m_target->ensurePolished();
        QStyleOptionButton option;
        option.initFrom(m_target);
        option.rect           = m_target->rect();
        const QRect indicator = m_target->style()->subElementRect(QStyle::SE_CheckBoxIndicator, &option, m_target);
        if (indicator.isEmpty())
        {
            return m_target->geometry();
        }
        return {m_target->mapTo(m_target->parentWidget(), indicator.topLeft()), indicator.size()};
    }

    void FocusRingOverlay::syncGeometry()
    {
        if (m_target.isNull() || m_target->parentWidget() == nullptr || !m_target->isVisible())
        {
            hide();
            return;
        }
        // 单环与双环共用同一外扩：覆盖层透明，单环多出来的空间不画东西，也不占布局
        setGeometry(targetRectInParent().adjusted(-kOverlayMargin, -kOverlayMargin, kOverlayMargin, kOverlayMargin));
        raise();
    }

    void FocusRingOverlay::paintEvent(QPaintEvent *event)
    {
        Q_UNUSED(event)

        if (m_target.isNull())
        {
            return;
        }
        const QRect target = targetRectInParent().translated(-geometry().topLeft());
        if (target.isEmpty())
        {
            return;
        }

        QPainter painter{this};
        painter.setRenderHint(QPainter::Antialiasing);
        painter.setBrush(Qt::NoBrush);
        const int radius = radiusFor(*m_target);

        // 内环贴着控件边缘：rect 外扩 1px 后以 2px 笔宽描边，笔画落在外侧两像素上
        painter.setPen(QPen{m_palette.accent, kRingWidth});
        painter.drawRoundedRect(QRectF(target).adjusted(-1.0, -1.0, 1.0, 1.0), radius + 1.0, radius + 1.0);

        if (m_style == FocusRingStyle::Single)
        {
            return;
        }

        // 外环再外推 4px，中间留出 2px 空隙露出父控件底色
        QColor outer = m_palette.accent;
        outer.setAlphaF(kOuterRingOpacity);
        painter.setPen(QPen{outer, kRingWidth});
        painter.drawRoundedRect(QRectF(target).adjusted(-5.0, -5.0, 5.0, 5.0), radius + 5.0, radius + 5.0);
    }

    FocusRingController::FocusRingController(PaletteProvider palette, QObject *parent) : QObject(parent), m_palette(std::move(palette))
    {
    }

    void FocusRingController::setThemeKind(const ThemeKind kind)
    {
        m_themeKind = kind;
        if (kind == ThemeKind::HighContrast)
        {
            hideRing();
            return;
        }
        if (m_ring != nullptr)
        {
            m_ring->setPalette(m_palette());
            m_ring->syncGeometry();
            return;
        }

        // 从高对比度切回浅色/深色时焦点没有变化，不会再来一次 FocusIn：主动给当前焦点补一个环
        if (auto *focused = QApplication::focusWidget(); focused != nullptr)
        {
            if (const auto spec = resolveTarget(focused); spec)
            {
                showFor(*spec);
            }
        }
    }

    std::optional<FocusRingController::TargetSpec> FocusRingController::resolveTarget(QWidget *focused) const
    {
        if (focused == nullptr || m_themeKind == ThemeKind::HighContrast)
        {
            return {};
        }

        QWidget *target = unwrapInput(focused);
        if (target == nullptr || !target->isEnabled() || target->parentWidget() == nullptr || inAnnotationToolbar(target))
        {
            return {};
        }
        if (auto *lineEdit = qobject_cast<QLineEdit *>(target); lineEdit != nullptr && lineEdit->isReadOnly())
        {
            return {};
        }
        if (qobject_cast<HotkeyEdit *>(target) != nullptr)
        {
            return TargetSpec{target, FocusRingStyle::Double, false};
        }
        if (qobject_cast<QLineEdit *>(target) != nullptr || qobject_cast<QComboBox *>(target) != nullptr || qobject_cast<QSpinBox *>(target) != nullptr)
        {
            return TargetSpec{target, FocusRingStyle::Double, false};
        }
        if (auto *textEdit = qobject_cast<QTextEdit *>(target); textEdit != nullptr)
        {
            if (textEdit->isReadOnly() || textEdit->objectName() != QStringLiteral("annotationTextEditor"))
            {
                return {};
            }
            return TargetSpec{target, FocusRingStyle::Double, false};
        }
        // 复选框按基类匹配而不是按具体类型：后续的自绘复选框是 QCheckBox 的子类，同样要单环
        if (qobject_cast<QCheckBox *>(target) != nullptr)
        {
            return TargetSpec{target, FocusRingStyle::Single, true};
        }
        if (qobject_cast<QAbstractButton *>(target) != nullptr)
        {
            return TargetSpec{target, FocusRingStyle::Single, false};
        }
        return {};
    }

    void FocusRingController::showFor(const TargetSpec &spec)
    {
        hideRing();
        if (spec.widget == nullptr || spec.widget->parentWidget() == nullptr)
        {
            return;
        }

        auto *overlay = new FocusRingOverlay{spec.widget, spec.style, spec.indicatorOnly, m_palette(), spec.widget->parentWidget()};
        m_target      = spec.widget;
        m_ring        = overlay;
        // 目标先于父控件销毁时，覆盖层跟着退场；父控件先销毁时 Qt 会把它一起带走
        QObject::connect(spec.widget, &QObject::destroyed, overlay, &QObject::deleteLater);
        overlay->syncGeometry();
        if (spec.widget->isVisible())
        {
            overlay->show();
            overlay->raise();
        }
    }

    void FocusRingController::hideRing()
    {
        if (m_ring != nullptr)
        {
            m_ring->hide();
            m_ring->deleteLater();
        }
        m_ring   = nullptr;
        m_target = nullptr;
    }

    bool FocusRingController::eventFilter(QObject *watched, QEvent *event)
    {
        auto *widget = qobject_cast<QWidget *>(watched);
        if (widget == nullptr)
        {
            return false;
        }

        switch (event->type())
        {
            case QEvent::FocusIn:
            {
                const auto spec = resolveTarget(widget);
                if (!spec)
                {
                    // 当前目标自己变得不可映射（例如切到高对比度或运行中被禁用）时要先收起来
                    if (m_target == widget)
                    {
                        hideRing();
                    }
                    return false;
                }
                if (m_target == spec->widget)
                {
                    return false;
                }
                showFor(*spec);
                return false;
            }
            case QEvent::FocusOut:
            {
                // 焦点转到另一个可映射控件时由它的 FocusIn 接手；这里只处理「环该消失」的那一半
                const auto nextSpec = resolveTarget(QApplication::focusWidget());
                if (m_target != nullptr && (nextSpec == std::nullopt || nextSpec->widget != m_target))
                {
                    hideRing();
                }
                return false;
            }
            case QEvent::Move:
            case QEvent::Resize:
            case QEvent::Show:
            case QEvent::Hide:
                if (m_target != nullptr && widget == m_target)
                {
                    if (event->type() == QEvent::Hide)
                    {
                        hideRing();
                    } else if (m_ring != nullptr)
                    {
                        m_ring->syncGeometry();
                    }
                }
                return false;
            default:
                return false;
        }
    }
} // namespace GyanisScribe::Gui
