#include "Ui/IconProvider.h"

#include "Core/Logger.h"

#include <QApplication>
#include <QColor>
#include <QDir>
#include <QFile>
#include <QIcon>
#include <QPalette>
#include <QPainter>
#include <QSvgRenderer>

#include <algorithm>
#include <cmath>
#include <format>
#include <string>
#include <tuple>

namespace
{
    /// 图标资源的前缀目录，路径规则只在这里出现一次，调用方只报文件名
    constexpr const char *kIconDirectory = ":/icons";

    /**
     * @brief 把 qrc 里的图标资源注册进当前进程
     * @details 见 IconProvider 构造处的说明：这份初始化没有任何符号引用它，链接器会整份丢掉。
     */
    void ensureIconResourcesRegistered()
    {
        static const bool registered = []
        {
            Q_INIT_RESOURCE(AppResources);
            return true;
        }();
        std::ignore = registered;
    }

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
} // namespace

namespace GyanisScribe::Ui
{
    IconProvider::IconProvider()
    {
        ensureIconResourcesRegistered();
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
            GyanisScribe::Core::Logger::error(std::format("图标 {} 取不到描边色：既没有设置主题色，也没有可用的应用对象", name));
            return {};
        }

        const auto source = loadTintedSource(name, tint);
        if (source.isEmpty())
        {
            GyanisScribe::Core::Logger::error(std::format("图标 {} 不在资源里：请把同名 SVG 放进 Resources/Icons 并更新 AppResources.qrc，"
                                                          "现有 {} 枚可用名字由 availableIconNames() 列出",
                                                          name, availableIconNames().size()));
            return {};
        }

        QSvgRenderer renderer(source.toUtf8());
        if (!renderer.isValid())
        {
            // 只有源文件语法坏了才会走到这里：报文件本身，不报名字，免得用户去翻名字表
            GyanisScribe::Core::Logger::error(std::format("图标 {} 的 SVG 源码解析失败，请检查 Resources/Icons 里那一份", name));
            return {};
        }

        // 按缩放比渲染实际像素，再把比例写回位图，Qt 才会在高 DPI 屏上按逻辑尺寸摆放它
        const auto ratio = std::clamp(devicePixelRatio, 1.0, 3.0);
        QPixmap    bitmap(static_cast<int>(std::ceil(static_cast<qreal>(size) * ratio)), static_cast<int>(std::ceil(static_cast<qreal>(size) * ratio)));
        bitmap.fill(Qt::transparent);
        bitmap.setDevicePixelRatio(ratio);

        QPainter painter(&bitmap);
        painter.setRenderHint(QPainter::Antialiasing);
        renderer.render(&painter);
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

        // 每档尺寸都备齐 1 到最大缩放比的副本：QIcon 按目标屏幕自己挑，跨 DPI 屏拖动不会糊
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

    QStringList IconProvider::availableIconNames()
    {
        // 静态入口也要注册：调用方可能一个图标对象都没建就来列名单
        ensureIconResourcesRegistered();

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
