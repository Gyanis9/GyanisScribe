#include "Ui/ThemeManager.h"

#include "Core/Logger.h"
#include "Settings/ISettingsStore.h"
#include "Settings/SettingsKeys.h"
#include "Ui/AppResources.h"

#include <QApplication>
#include <QFile>
#include <QGuiApplication>
#include <QStyleHints>

#include <format>
#include <string>

namespace
{
    using GyanisScribe::Ui::ThemeKind;

    /**
     * @brief 主题键的 QString 形态
     * @details 键名常量是 string_view，而配置通知给的是 QString，比较前先造一份；
     *          这一口径与 HotkeyManager 订阅热键改动时一致。
     */
    QString themeModeKey()
    {
        return QString::fromUtf8(GyanisScribe::Settings::kThemeMode.data(), static_cast<qsizetype>(GyanisScribe::Settings::kThemeMode.size()));
    }

    /**
     * @brief 按解算后的档位取样式表的资源路径
     * @param resolvedKind 只会是 Light 或 Dark，其余按浅色取
     */
    QString styleSheetPath(const ThemeKind resolvedKind)
    {
        return resolvedKind == ThemeKind::Dark ? QStringLiteral(":/styles/Dark.qss") : QStringLiteral(":/styles/Light.qss");
    }
} // namespace

namespace GyanisScribe::Ui
{
    ThemeManager::ThemeManager(Settings::ISettingsStore &settings, QObject *parent) :
        QObject(parent), m_settings(settings)
        // 档位默认跟随系统：需求第五节的缺省档，用户没进过设置页时界面不该硬选一套色
        ,
        m_requested(ThemeKind::System)
        // System 在这里当「还没落过样式表」的哨兵：resolveThemeKind 永远不会返回它，
        // 于是第一次 refresh 必然被判成变化，界面拿到一次确定的起点
        ,
        m_resolved(ThemeKind::System)
    {
        // 设置页改档位走配置通知，主题层不认识对话框，也不需要对话框反过来认识它
        QObject::connect(&m_settings, &Settings::ISettingsStore::valueChanged, this,
                         [this](const QString &key)
                         {
                             if (key == themeModeKey())
                             {
                                 applyFromSettings();
                             }
                         });

        // 用户选了「跟随系统」时，系统深浅色变了要立刻跟上，不等重启
        QObject::connect(QGuiApplication::styleHints(), &QStyleHints::colorSchemeChanged, this, [this](Qt::ColorScheme) { refresh(); });
    }

    void ThemeManager::applyFromSettings()
    {
        const auto stored = m_settings.stringValue(Settings::kThemeMode, QStringLiteral("System")).toStdString();
        if (const auto parsed = parseThemeKind(stored); parsed)
        {
            m_requested = *parsed;
        } else
        {
            // 坏值多半来自手改配置文件：先报清是哪个键、只能取哪三个值，再按 System 走，
            // 界面不能因为一个错拼的取值停在「没主题」
            GyanisScribe::Core::Logger::warning(std::format("配置项 {} 的主题取值不合法，本项按 System 处理。{}", std::string{Settings::kThemeMode}, parsed.error().message));
            m_requested = ThemeKind::System;
        }
        refresh();
    }

    Core::Expected<void> ThemeManager::setTheme(const ThemeKind themeKind)
    {
        // 先写配置再改内存：写不进去时状态得留在原样，否则界面与配置文件从此不一致
        if (auto written = m_settings.setValue(Settings::kThemeMode, QString::fromUtf8(std::string{themeKindText(themeKind)})); !written)
        {
            return written;
        }
        m_requested = themeKind;
        refresh();
        return {};
    }

    const ThemePalette &ThemeManager::palette() const
    {
        return paletteFor(m_resolved);
    }

    QColor ThemeManager::iconTint() const
    {
        return palette().icon;
    }

    ThemeKind ThemeManager::requestedKind() const
    {
        return m_requested;
    }

    ThemeKind ThemeManager::resolvedKind() const
    {
        return m_resolved;
    }

    QString ThemeManager::styleSheetFor(const ThemeKind resolvedKind)
    {
        ensureApplicationResourcesRegistered();

        QFile file(styleSheetPath(resolvedKind));
        if (!file.open(QIODevice::ReadOnly))
        {
            // 报路径而不是只报「读不到」：这类缺失要么是 qrc 少了一行，要么是文件没进仓库
            GyanisScribe::Core::Logger::error(
                    std::format("样式表资源 {} 读不出来，请确认它在 Resources/Styles 下且已登记进 AppResources.qrc", styleSheetPath(resolvedKind).toStdString()));
            return {};
        }
        return QString::fromUtf8(file.readAll());
    }

    void ThemeManager::refresh()
    {
        const auto resolved = resolveThemeKind(m_requested, currentSystemScheme());

        // 同一套色不重发通知：样式表整片重设会让所有控件重排重绘，没有色变化就不该付这个代价
        if (resolved == m_resolved)
        {
            return;
        }
        m_resolved = resolved;

        // 只有 QApplication 才认样式表：自检里若只起了 QCoreApplication，就跳过落样式这一步
        if (auto *application = qobject_cast<QApplication *>(QGuiApplication::instance()); application != nullptr)
        {
            application->setStyleSheet(styleSheetFor(resolved));
        }
        emit themeChanged();
    }

    SystemScheme ThemeManager::currentSystemScheme() const
    {
        const auto *hints = QGuiApplication::styleHints();
        return hints == nullptr ? SystemScheme::Unknown : toSystemScheme(hints->colorScheme());
    }

} // namespace GyanisScribe::Ui
