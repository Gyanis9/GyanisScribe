#include "Gui/Controls/IconLabel.h"

#include "Gui/Theme/ThemeManager.h"

#include <QEvent>
#include <QHBoxLayout>
#include <QLayout>
#include <QPixmap>

namespace GyanisScribe::Gui
{
    IconLabel::IconLabel(IconProvider &icons, ThemeManager &theme, const std::string_view name, const QString &text, QWidget *parent) :
        QLabel(parent), m_icons(icons), m_name(name), m_text(text)
    {
        setObjectName(QStringLiteral("icon-%1").arg(QString::fromStdString(m_name)));
        // 图标与文字排在一条里：QLabel 只能二选一，所以自己搭一条横排
        auto *row = new QHBoxLayout{this};
        row->setContentsMargins(0, 0, 0, 0);
        row->setSpacing(6);
        auto *icon = new QLabel{this};
        icon->setObjectName(QStringLiteral("icon-glyph"));
        row->addWidget(icon);
        row->addWidget(new QLabel{m_text, this});
        QObject::connect(&theme, &ThemeManager::themeChanged, this, [this] { refresh(); });
        refresh();
    }

    QSize IconLabel::sizeHint() const
    {
        const auto *row = layout();
        return row != nullptr ? row->totalSizeHint() : QLabel::sizeHint();
    }

    QSize IconLabel::minimumSizeHint() const
    {
        const auto *row = layout();
        return row != nullptr ? row->totalMinimumSize() : QLabel::minimumSizeHint();
    }

    void IconLabel::changeEvent(QEvent *event)
    {
        QLabel::changeEvent(event);
        if (event->type() == QEvent::PaletteChange || event->type() == QEvent::ApplicationPaletteChange)
        {
            refresh();
        }
    }

    void IconLabel::refresh()
    {
        if (auto *glyph = findChild<QLabel *>(QStringLiteral("icon-glyph")); glyph != nullptr)
        {
            glyph->setPixmap(m_icons.pixmap(m_name, IconSize::Small, devicePixelRatioF()));
        }
    }
} // namespace GyanisScribe::Gui
