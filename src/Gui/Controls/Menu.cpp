#include "Gui/Controls/Menu.h"

#include "Core/Geometry/ContinuousCorner.h"
#include "Gui/Theme/CornerStyle.h"
#include "Gui/Theme/DesignTokens.h"

#include <QAction>
#include <QPainter>
#include <QPen>
#include <QWidgetAction>

namespace
{
    /**
     * @brief 让菜单窗口不铺系统底色，圆角外才能真的透明
     * @param menu 目标菜单
     * @note 菜单面板与边框由 SquircleStyle 按 PE_PanelMenu/PE_FrameMenu 跳过原生绘制；
     *       Qt 原生 QMenu 不在此列，属性同时把样式表规则限定到自绘类，
     *       系统文本控件的标准右键菜单因此保留原生面板与高亮
     */
    void applyTranslucentWindow(GyanisScribe::Gui::Menu &menu)
    {
        menu.setAttribute(Qt::WA_TranslucentBackground);
        menu.setAutoFillBackground(false);
    }
} // namespace

namespace GyanisScribe::Gui
{
    Menu::Menu(QWidget *parent) : QMenu(parent)
    {
        setProperty("squircle", true);
        applyTranslucentWindow(*this);
    }

    Menu::Menu(const QString &title, QWidget *parent) : QMenu(title, parent)
    {
        setProperty("squircle", true);
        applyTranslucentWindow(*this);
    }

    void Menu::paintEvent(QPaintEvent *event)
    {
        QPainter painter{this};
        Core::paintContinuousCorner(painter, QRectF{rect()}, Core::cornerShapeFor(Core::CornerRole::Panel), activePalette().surface,
                                    QPen{activePalette().borderStrong, static_cast<qreal>(themeBorderWidth(activeThemeKind()))});

        // 当前项高亮由本类画：只给普通动作上底，分隔线没有任何可点的东西，
        // QWidgetAction（色板那一格）自带一整块控件，再垫一层高亮会把它的四角露出来
        QAction *active = activeAction();
        if (active != nullptr && !active->isSeparator() && qobject_cast<QWidgetAction *>(active) == nullptr)
        {
            const QRect item = actionGeometry(active);
            if (!item.isEmpty())
            {
                Core::paintContinuousCorner(painter, QRectF{item}, Core::cornerShapeFor(Core::CornerRole::Compact), activePalette().hover,
                                            QPen{Qt::NoPen});
            }
        }
        QMenu::paintEvent(event);
    }
} // namespace GyanisScribe::Gui
