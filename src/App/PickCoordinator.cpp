#include "App/PickCoordinator.h"

#include "Base/Logger.h"
#include "Base/Text.h"
#include "Core/Capture/IScreenCaptureSource.h"
#include "Core/Pick/ColorFormat.h"
#include "Core/Pick/PickGeometry.h"
#include "Core/Pick/RecentColors.h"
#include "Core/Settings/ISettingsStore.h"
#include "Core/Settings/SettingsKeys.h"
#include "Gui/Pick/ColorPickerOverlay.h"
#include "Gui/Theme/ThemeManager.h"

#include <QClipboard>
#include <QCursor>
#include <QGuiApplication>
#include <QPointer>
#include <QScreen>

#include <format>
#include <string>

namespace GyanisScribe::App
{
    PickCoordinator::PickCoordinator(GyanisScribe::Core::ISettingsStore &settings, GyanisScribe::Gui::ThemeManager &theme, GyanisScribe::Core::IScreenCaptureSource &captureSource,
                                     QObject *parent) : QObject(parent), m_settings(settings), m_theme(theme), m_captureSource(captureSource)
    {
    }

    std::vector<QColor> PickCoordinator::recentColors() const
    {
        const auto parsed = GyanisScribe::Core::parseRecentColors(m_settings.stringListValue(GyanisScribe::Core::kPickRecentColors, {}));
        if (!parsed)
        {
            GyanisScribe::Base::Logger::warning(std::format("最近取色清单读不出，按空清单继续：{}", parsed.error().message));
            return {};
        }
        return *parsed;
    }

    void PickCoordinator::remember(const QColor &color)
    {
        const auto updated = GyanisScribe::Core::pushRecentColor(recentColors(), color, GyanisScribe::Core::kPickColorHistoryCapacity);
        if (auto written = m_settings.setValue(GyanisScribe::Core::kPickRecentColors, GyanisScribe::Core::serializeRecentColors(updated)); !written)
        {
            GyanisScribe::Base::Logger::warning(std::format("最近取色清单没能写入：{}", written.error().message));
        }
    }

    void PickCoordinator::copyPickedColor(const QColor &color)
    {
        // 贴图上取的色也进同一份「最近取色」：下一轮屏幕取色时色块行上就有它
        remember(color);

        // 与屏幕取色同一套：写法从设置现读，读坏了按 HEX 顶上并把原因说给用户
        std::string complaint;
        const auto  format = GyanisScribe::Core::colorFormatOrDefault(m_settings.stringValue(GyanisScribe::Core::kPickFormat, QStringLiteral("Hex")).toStdString(), complaint);
        if (!complaint.empty())
        {
            emit messageForUser(QString::fromStdString(complaint), GyanisScribe::Gui::ToastKind::Danger);
        }

        // 字母档现读配置：这一档只管 HEX 那一形，写法是别的时读出来也一样
        const auto casing = GyanisScribe::Core::hexCasingOf(m_settings.boolValue(GyanisScribe::Core::kPickHexUppercase, true));
        const auto text   = GyanisScribe::Core::formatColor(color, format, casing);
        QGuiApplication::clipboard()->setText(text);
        emit messageForUser(GyanisScribe::Base::uiText("已复制颜色 ") + text, GyanisScribe::Gui::ToastKind::Success);
    }

    void PickCoordinator::start()
    {
        auto *screen = QGuiApplication::screenAt(QCursor::pos());
        if (screen == nullptr)
        {
            screen = QGuiApplication::primaryScreen();
        }
        if (screen == nullptr)
        {
            emit messageForUser(GyanisScribe::Base::uiText("这台机器上没有可用的屏幕，取色没法开始"), GyanisScribe::Gui::ToastKind::Danger);
            return;
        }

        const auto frame = m_captureSource.grabScreen(screen->name());
        if (!frame)
        {
            emit messageForUser(QString::fromUtf8(GyanisScribe::Base::uiFormat(GyanisScribe::Base::uiTextUtf8("这一屏抓不下来：{}。请重试一次"), frame.error().message).c_str()),
                                GyanisScribe::Gui::ToastKind::Danger);
            return;
        }

        // 写法读坏了不拦取色：按默认写法继续，并把这一条告诉用户，免得他复制到的格式莫名其妙变了
        std::string complaint;
        const auto  format = GyanisScribe::Core::colorFormatOrDefault(m_settings.stringValue(GyanisScribe::Core::kPickFormat, QStringLiteral("Hex")).toStdString(), complaint);
        if (!complaint.empty())
        {
            emit messageForUser(QString::fromStdString(complaint), GyanisScribe::Gui::ToastKind::Danger);
        }

        // 放大镜视野与字母档都现读这一处的配置：取色遮罩自己不持设置，读坏了在这里说给用户
        std::string magnifierComplaint;
        const auto  magnifierView = GyanisScribe::Core::magnifierViewOrDefault(
                m_settings.stringValue(GyanisScribe::Core::kCaptureMagnifierView, QStringLiteral("narrow")).toStdString(), magnifierComplaint);
        if (!magnifierComplaint.empty())
        {
            emit messageForUser(QString::fromStdString(magnifierComplaint), GyanisScribe::Gui::ToastKind::Danger);
        }
        m_picker = new GyanisScribe::Gui::ColorPickerOverlay{
                *frame, screen->geometry(), format, GyanisScribe::Core::hexCasingOf(m_settings.boolValue(GyanisScribe::Core::kPickHexUppercase, true)), magnifierView, m_theme};
        // 最近取色跟着这一轮摆上遮罩：点色块或按数字键就能重新取回，不必回原处对像素
        m_picker->setRecentColors(recentColors());
        QObject::connect(m_picker, &GyanisScribe::Gui::ColorPickerOverlay::colorPicked, this,
                         [this](const QColor &color, const QString &text)
                         {
                             remember(color);
                             QGuiApplication::clipboard()->setText(text);
                             emit messageForUser(GyanisScribe::Base::uiText("已复制颜色 ") + text, GyanisScribe::Gui::ToastKind::Success);
                         });
        QObject::connect(m_picker, &GyanisScribe::Gui::ColorPickerOverlay::cancelled, this, [] { GyanisScribe::Base::Logger::info("取色已取消"); });
        m_picker->showFullScreen();
        m_picker->raise();
        m_picker->activateWindow();
    }

} // namespace GyanisScribe::App
