#include "Ui/IconProvider.h"

#include "Base/Logger.h"
#include "Ui/AppResources.h"

#include <QApplication>
#include <QColor>
#include <QDir>
#include <QFile>
#include <QIcon>
#include <QWidget>
#include <QAbstractButton>
#include <QPalette>
#include <QPainter>
#include <QRectF>
#include <QSvgRenderer>

#include <algorithm>
#include <cmath>
#include <format>
#include <string>

namespace
{
    /// 图标资源的前缀目录，路径规则只在这里出现一次，调用方只报文件名
    constexpr const char *kIconDirectory = ":/icons";

    /**
     * @brief 取要备到几倍缩放比的副本
     * @return 向上取整并夹在 1 到 3 之间的整数倍
     * @details 125% 缩放要按 2 倍光栅化才不糊；3 是需求 DPI 阶梯（最高 200%）之上再留一档的封顶，
     *          超过这个值的屏幕由 Qt 自行缩放。缩放比读的是主屏：更高 DPI 的副屏上图标会由 Qt 就近放大，
     *          那是可接受的退化，不是错位。
     */
    int maximumDevicePixelRatio()
    {
        if (qApp == nullptr)
        {
            return 1;
        }
        return std::clamp<int>(static_cast<int>(std::ceil(qApp->devicePixelRatio())), 1, 3);
    }
    /// 由 applyIcon 记在按钮上的动态属性名：refreshIcons 按它找回图标
    constexpr const char *kIconNameProperty = "uiIconName";

} // namespace

namespace GyanisScribe::Ui
{
    namespace
    {
        /**
         * @brief 拼一枚光栅化结果的缓存键
         * @param name SVG 文件名去掉扩展名
         * @param size 尺寸档位
         * @param tint 描边色
         * @param ratio 夹住之后实际用的缩放比
         * @return 四个分量拼成的一条键
         * @details 描边色不在键里，换主题之后旧色那一份会被当成新图标继续发出去——正是 applyIcon()
         *          的 @details 明令禁止的那种错；缩放比不在键里，换到另一块 DPI 的屏上就还会拿到
         *          低缩放的那一份。传进来的必须是夹过的 ratio：9.0 与 3.0 要落进同一格。
         */
        [[nodiscard]] QString pixmapCacheKey(const std::string_view name, const IconSize size, const QColor &tint, const qreal ratio)
        {
            return QString::fromUtf8(name.data(), static_cast<qsizetype>(name.size())) + QLatin1Char('#') + QString::number(static_cast<int>(size)) + QLatin1Char('#') +
                   QString::number(tint.rgba(), 16) + QLatin1Char('#') + QString::number(static_cast<int>(std::lround(ratio * 10.0)));
        }
    } // namespace

    IconProvider::IconProvider()
    {
        ensureApplicationResourcesRegistered();
    }

    void IconProvider::setTintColor(const QColor &tintColor)
    {
        m_tintColor = tintColor;
    }

    QColor IconProvider::tintColor() const
    {
        // 显式设置过就以主题为准
        if (m_tintColor.isValid())
        {
            return m_tintColor;
        }

        // 没接主题时回落到应用调色板的文本色：跟着系统深浅走，而不是在代码里写死一个黑
        if (qApp == nullptr)
        {
            return {};
        }
        return qApp->palette().color(QPalette::WindowText);
    }

    QString IconProvider::loadTintedSource(const std::string_view name, const QColor &tint)
    {
        const auto path = QString::fromUtf8(kIconDirectory) + QStringLiteral("/") + QString::fromStdString(std::string{name}) + QStringLiteral(".svg");

        QFile file(path);
        if (!file.open(QIODevice::ReadOnly))
        {
            return {};
        }

        // Qt 的 SVG 渲染器不认 currentColor 这个 CSS 关键字，整片描边会退回默认黑；
        // 载入时换成主题色，同一批 SVG 源文件就同时服务深浅两套主题
        auto source = QString::fromUtf8(file.readAll());
        source.replace(QStringLiteral("currentColor"), tint.name(QColor::HexRgb));
        return source;
    }

    QPixmap IconProvider::pixmap(const std::string_view name, const IconSize size, const qreal devicePixelRatio) const
    {
        const auto tint = tintColor();
        if (!tint.isValid())
        {
            GyanisScribe::Base::Logger::error(std::format("图标 {} 取不到描边色：既没有设置主题色，也没有可用的应用对象", name));
            return {};
        }

        // 缩放比先夹定再进键：9.0 与 3.0 说的是同一份像素，各存一格会让同一枚图标重复光栅化
        const auto    ratio = std::clamp(devicePixelRatio, 1.0, 3.0);
        const QString key   = pixmapCacheKey(name, size, tint, ratio);
        // 命中就一次文件读与一次解析都不做：录屏 HUD 每存一帧都按状态重挂暂停按钮的图标，
        // 一枚图标是四档尺寸 × 若干缩放比，逐帧重画纯属白干
        if (const auto cached = m_pixmapCache.constFind(key); cached != m_pixmapCache.constEnd())
        {
            return cached.value();
        }

        const auto source = loadTintedSource(name, tint);
        if (source.isEmpty())
        {
            GyanisScribe::Base::Logger::error(std::format("图标 {} 不在资源里：请把同名 SVG 放进 Resources/Icons 并更新 AppResources.qrc，"
                                                          "现有 {} 枚可用名字由 availableIconNames() 列出",
                                                          name, availableIconNames().size()));
            return {};
        }

        QSvgRenderer renderer(source.toUtf8());
        if (!renderer.isValid())
        {
            // 只有源文件语法坏了才会走到这里：报文件本身，不报名字，免得用户去翻名字表
            GyanisScribe::Base::Logger::error(std::format("图标 {} 的 SVG 源码解析失败，请检查 Resources/Icons 里那一份", name));
            return {};
        }

        // 按缩放比渲染实际像素，再把比例写回位图，Qt 才会在高 DPI 屏上按逻辑尺寸摆放它
        QPixmap bitmap(static_cast<int>(std::ceil(static_cast<qreal>(size) * ratio)), static_cast<int>(std::ceil(static_cast<qreal>(size) * ratio)));
        bitmap.fill(Qt::transparent);
        bitmap.setDevicePixelRatio(ratio);

        QPainter painter(&bitmap);
        painter.setRenderHint(QPainter::Antialiasing);
        // 边界必须显式给逻辑尺寸：不带边界的 render() 把 viewBox 铺到画家的视口矩形上，
        // 而那一步按物理像素计，画家随后又按位图的缩放比放大一次——dpr=2 时实测图形两倍大、
        // 右下半整片裁掉（HIGH DPI 副本渲成 PNG 一眼可见）。传 24 逻辑格的边界，
        // 缩放只剩位图自己这一道，两个缩放比上的输出都完整。
        renderer.render(&painter, QRectF{0.0, 0.0, static_cast<qreal>(size), static_cast<qreal>(size)});
        m_pixmapCache.insert(key, bitmap);
        return bitmap;
    }

    QIcon IconProvider::icon(const std::string_view name) const
    {
        // 先探最小档：名字不存在时 pixmap 已经报过一次原因，这里直接返回，免得同一条错误刷四遍
        if (pixmap(name, IconSize::Small).isNull())
        {
            return {};
        }

        QIcon result;

        // 每档尺寸都备齐 1 到最大缩放比的副本：QIcon 按目标屏幕自己挑，跨 DPI 屏拖动不会糊。
        // 光栅化本身由 pixmap() 那张表兜着，重复取同一枚图标不再重画
        const auto ratios = maximumDevicePixelRatio();
        for (const auto size: {IconSize::Small, IconSize::Medium, IconSize::Large, IconSize::ExtraLarge})
        {
            for (int ratio = 1; ratio <= ratios; ++ratio)
            {
                result.addPixmap(pixmap(name, size, static_cast<qreal>(ratio)));
            }
        }
        return result;
    }

    void IconProvider::applyIcon(QAbstractButton &button, const std::string_view name) const
    {
        // 名字挂在控件上而不是存在本类里：控件被销毁时记录跟着走，不需要谁去反注册
        button.setProperty(kIconNameProperty, QString::fromUtf8(name.data(), static_cast<qsizetype>(name.size())));
        button.setIcon(icon(name));
    }

    void IconProvider::refreshIcons(QWidget &root) const
    {
        const auto buttons = root.findChildren<QAbstractButton *>();
        for (auto *button: buttons)
        {
            const auto name = button->property(kIconNameProperty).toString();
            if (name.isEmpty())
            {
                // 没走过 applyIcon 的按钮有自己的图标来源（例如标注工具栏的色块），重取会画错
                continue;
            }
            button->setIcon(icon(name.toStdString()));
        }
    }

    QStringList IconProvider::availableIconNames()
    {
        // 静态入口也要注册：调用方可能一个图标对象都没建就来列名单
        ensureApplicationResourcesRegistered();

        // 资源目录列出的就是 alias（带 .svg 后缀），去掉后缀才和调用方传进来的名字同形
        QStringList names;
        for (const auto &entry: QDir(QString::fromUtf8(kIconDirectory)).entryList(QDir::Files))
        {
            if (entry.endsWith(QStringLiteral(".svg")))
            {
                names << entry.left(entry.size() - 4);
            }
        }
        std::ranges::sort(names);
        return names;
    }

} // namespace GyanisScribe::Ui
