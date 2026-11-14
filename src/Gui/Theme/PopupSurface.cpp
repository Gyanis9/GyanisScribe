#include "Gui/Theme/PopupSurface.h"

#include <QEvent>
#include <QObject>
#include <QPainterPath>
#include <QPolygon>
#include <QRegion>
#include <QWidget>

namespace
{
    /// 已经处理过的窗口打个记号：重复处理会把正在显示的弹层重新描一遍
    constexpr auto kPreparedProperty = "gyanisPopupSurface";

    /**
     * @brief 按当前尺寸给窗口套一圈连续曲率掩膜
     * @details 掩膜交给系统裁剪四角：Windows 对弹出窗口的透明合成不可靠，
     *          只靠逐像素 alpha 时圆角外仍会显出黑块
     */
    void applyPopupMask(QWidget *window, const GyanisScribe::Core::CornerRole role)
    {
        if (window == nullptr || window->size().isEmpty())
        {
            return;
        }
        const auto path = GyanisScribe::Core::continuousCornerPath(QRectF{QPointF{0.0, 0.0}, QSizeF{window->size()}},
                                                                   GyanisScribe::Core::cornerShapeFor(role, QSizeF{window->size()}));
        window->setMask(QRegion{path.toFillPolygon().toPolygon()});
    }

    /**
     * @brief 跟着窗口显示与改尺寸重算圆角掩膜
     * @details 弹层每次显示前尺寸才定下来，两个事件都要接
     */
    class MaskFilter final : public QObject
    {
    public:
        MaskFilter(QWidget *window, const GyanisScribe::Core::CornerRole role) : QObject(window), m_window(window), m_role(role)
        {
        }

    protected:
        bool eventFilter(QObject *watched, QEvent *event) override
        {
            if (event->type() == QEvent::Show || event->type() == QEvent::Resize)
            {
                applyPopupMask(m_window, m_role);
            }
            return QObject::eventFilter(watched, event);
        }

    private:
        QWidget                       *m_window = nullptr;
        GyanisScribe::Core::CornerRole m_role;
    };
} // namespace

namespace GyanisScribe::Gui
{
    void preparePopupSurface(QWidget *window, const Core::CornerRole role)
    {
        if (window == nullptr || window->property(kPreparedProperty).toBool())
        {
            return;
        }
        window->setAttribute(Qt::WA_TranslucentBackground);
        window->setAutoFillBackground(false);
        window->setProperty(kPreparedProperty, true);
        // 弹层可能已经显示出来：尺寸就位就先裁一次，之后的显示与改尺寸由过滤器接住
        applyPopupMask(window, role);
        window->installEventFilter(new MaskFilter{window, role});
    }
} // namespace GyanisScribe::Gui
