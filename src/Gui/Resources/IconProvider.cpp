#include "Gui/Resources/IconProvider.h"

#include "Base/Logger.h"
#include "Gui/Resources/AppResources.h"

#include <QApplication>
#include <QColor>
#include <QDir>
#include <QFile>
#include <QGuiApplication>
#include <QIcon>
#include <QWidget>
#include <QAbstractButton>
#include <QPalette>
#include <QPainter>
#include <QRectF>
#include <QScreen>
#include <QSvgRenderer>

#include <algorithm>
#include <array>
#include <cmath>
#include <format>
#include <string>
#include <vector>

namespace
{
    /// 图标资源的前缀目录，路径规则只在这里出现一次，调用方只报文件名
    constexpr const char *kIconDirectory = ":/icons";

    /// 品牌资源的前缀目录
    constexpr const char *kBrandDirectory = ":/brand";

    /// 品牌图的八档原生边长
    constexpr std::array<int, 8> kBrandSizes{16, 24, 32, 48, 64, 128, 256, 512};

    /**
     * @brief 收集所有屏幕的缩放比
     * @return 以 1.0 打底、按升序去重的倍率表，每项夹在 1 到 3
     * @details 125%/150%/175% 屏必须拿到精确倍率；只备整数倍时 QIcon 会拿 2× 再缩。
     *          读的是全部已连接屏幕，不再只看主屏。
     */
    std::vector<qreal> screenDevicePixelRatios()
    {
        std::vector<qreal> ratios{1.0};
        for (const auto *screen: QGuiApplication::screens())
        {
            const auto ratio = std::clamp(screen->devicePixelRatio(), 1.0, 3.0);
            ratios.push_back(ratio);
        }
        std::ranges::sort(ratios);
        ratios.erase(std::unique(ratios.begin(), ratios.end()), ratios.end());
        return ratios;
    }

    /// 读 /brand 下某个原生边长的品牌 PNG；不参与主题上色
    QPixmap loadBrandPixmap(int physicalSize)
    {
        return QPixmap{QString::fromUtf8(kBrandDirectory) + QStringLiteral("/AppIcon%1.png").arg(physicalSize)};
    }

    /// 由 applyIcon 记在按钮上的动态属性名：refreshIcons 按它找回图标
    constexpr const char *kIconNameProperty = "uiIconName";

} // namespace

namespace GyanisScribe::Gui
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
            GyanisScribe::Base::Logger::error(std::format("图标 {} 不在资源里：请把同名 SVG 放进 src/Gui/Resources/Icons 并更新 AppResources.qrc，"
                                                          "现有 {} 枚可用名字由 availableIconNames() 列出",
                                                          name, availableIconNames().size()));
            return {};
        }

        QSvgRenderer renderer(source.toUtf8());
        if (!renderer.isValid())
        {
            // 只有源文件语法坏了才会走到这里：报文件本身，不报名字，免得用户去翻名字表
            GyanisScribe::Base::Logger::error(std::format("图标 {} 的 SVG 源码解析失败，请检查 src/Gui/Resources/Icons 里那一份", name));
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

    QPixmap IconProvider::brandPixmap(const int logicalSize, const qreal devicePixelRatio) const
    {
        if (logicalSize <= 0)
        {
            return {};
        }

        const auto ratio  = std::clamp(devicePixelRatio, 1.0, 3.0);
        const auto wanted = static_cast<int>(std::ceil(static_cast<qreal>(logicalSize) * ratio));

        auto physical = kBrandSizes.back();
        for (const auto candidate: kBrandSizes)
        {
            if (candidate >= wanted)
            {
                physical = candidate;
                break;
            }
        }

        auto bitmap = loadBrandPixmap(physical);
        if (bitmap.isNull())
        {
            GyanisScribe::Base::Logger::error(std::format("品牌图标 {}px 读不出：请检查 /brand 下的 PNG", physical));
            return {};
        }

        bitmap.setDevicePixelRatio(static_cast<qreal>(physical) / static_cast<qreal>(logicalSize));
        return bitmap;
    }

    QIcon IconProvider::brandIcon() const
    {
        QIcon result;
        for (const auto size: kBrandSizes)
        {
            const auto bitmap = brandPixmap(size, 1.0);
            if (!bitmap.isNull())
            {
                result.addPixmap(bitmap);
            }
        }
        for (const auto ratio: screenDevicePixelRatios())
        {
            for (const auto size: {16, 24, 32, 48, 64, 128, 256})
            {
                const auto bitmap = brandPixmap(size, ratio);
                if (!bitmap.isNull())
                {
                    result.addPixmap(bitmap);
                }
            }
        }
        return result;
    }

    QIcon IconProvider::icon(const std::string_view name) const
    {        // 先探最小档：名字不存在时 pixmap 已经报过一次原因，这里直接返回，免得同一条错误刷四遍
        if (pixmap(name, IconSize::Small).isNull())
        {
            return {};
        }

        QIcon result;

        // 八档尺寸各备一份所有屏幕倍率的副本：QIcon 按目标屏幕自己挑，跨 DPI 屏拖动不会糊。
        // 光栅化本身由 pixmap() 那张表兜着，重复取同一枚图标不再重画
        for (const auto size: {IconSize::Small, IconSize::Medium, IconSize::Large, IconSize::ExtraLarge,
                               IconSize::Huge, IconSize::Display, IconSize::History, IconSize::Maximum})
        {
            for (const auto ratio: screenDevicePixelRatios())
            {
                result.addPixmap(pixmap(name, size, ratio));
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

} // namespace GyanisScribe::Gui
