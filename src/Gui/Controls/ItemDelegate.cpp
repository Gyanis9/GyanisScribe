#include "Gui/Controls/ItemDelegate.h"

#include "Core/Geometry/ContinuousCorner.h"
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
            painter->save();
            painter->setRenderHint(QPainter::Antialiasing, true);
            Core::paintContinuousCorner(*painter, QRectF{opt.rect}, Core::cornerShapeFor(Core::CornerRole::Compact), selected ? activePalette().accentSoft : activePalette().hover,
                                        QPen{Qt::NoPen});
            painter->restore();
        }
        // 选中底已经画过：把 Highlight 画刷清空，基类才不会在圆角底上再补一块方角
        opt.palette.setBrush(QPalette::Highlight, Qt::NoBrush);
        QStyledItemDelegate::paint(painter, opt, index);
    }
} // namespace GyanisScribe::Gui
