#include "Gui/Controls/ItemDelegate.h"

#include "Core/Geometry/ContinuousCorner.h"
#include "Gui/Theme/CornerStyle.h"
#include "Gui/Theme/DesignTokens.h"

#include <QPainter>
#include <QPen>

namespace GyanisScribe::Gui
{
    ItemDelegate::ItemDelegate(QObject *parent) : QStyledItemDelegate(parent)
    {
    }

    void ItemDelegate::paint(QPainter *painter, const QStyleOptionViewItem &option, const QModelIndex &index) const
    {
        QStyleOptionViewItem opt{option};
        initStyleOption(&opt, index);
        const bool selected = opt.state.testFlag(QStyle::State_Selected);
        const bool hover    = opt.state.testFlag(QStyle::State_MouseOver);
        if (selected || hover)
        {
            // 高对比度档的选中项补一圈主色边：这一档不靠底色分层，纯色底在纯白面板上分不出选中与悬停
            const QPen border = selected && activeThemeKind() == ThemeKind::HighContrast
                                        ? QPen{activePalette().accent, static_cast<qreal>(themeBorderWidth(ThemeKind::HighContrast))}
                                        : QPen{Qt::NoPen};
            painter->save();
            painter->setRenderHint(QPainter::Antialiasing, true);
            Core::paintContinuousCorner(*painter, QRectF{opt.rect}, Core::cornerShapeFor(Core::CornerRole::Compact), selected ? activePalette().accentSoft : activePalette().hover,
                                        border);
            painter->restore();
        }
        // 样式表不在时基类按 HighlightedText 画选中项文字，那一支是压在主色上的浅色，落到 accentSoft 上读不出
        opt.palette.setColor(QPalette::HighlightedText, opt.palette.color(QPalette::Text));
        // 选中底已经画过：把 Highlight 画刷清空，基类才不会在圆角底上再补一块方角
        opt.palette.setBrush(QPalette::Highlight, Qt::NoBrush);
        QStyledItemDelegate::paint(painter, opt, index);
    }
} // namespace GyanisScribe::Gui
