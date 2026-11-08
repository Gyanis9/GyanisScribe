#include "Gui/Controls/FontItemDelegate.h"

#include <QFont>
#include <QFontMetrics>
#include <QStringList>
#include <QVariant>

namespace GyanisScribe::Gui
{
    FontItemDelegate::FontItemDelegate(QObject *parent) : ItemDelegate(parent)
    {
    }

    void FontItemDelegate::prepareOption(QStyleOptionViewItem &option, const QModelIndex &index) const
    {
        const QVariant fontData = index.data(Qt::FontRole);
        if (fontData.canConvert<QFont>())
        {
            // 模型给了完整字体时沿用它的族名与字形，其余属性仍取界面字体
            option.font = fontData.value<QFont>().resolve(option.font);
        } else
        {
            const QString family = index.data(Qt::DisplayRole).toString();
            // 字体下拉框只给族名；空串留给基类字体，避免把界面字体也清掉
            if (!family.isEmpty())
            {
                option.font.setFamilies(QStringList{family});
            }
        }
        // 样式按 fontMetrics 排布与省略文字，换过字体后必须同步，否则长族名按旧字体宽度截断
        option.fontMetrics = QFontMetrics{option.font};
    }
} // namespace GyanisScribe::Gui
