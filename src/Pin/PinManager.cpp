#include "Pin/PinManager.h"

#include "Pin/PinWindow.h"
#include "Settings/ISettingsStore.h"

#include <QPoint>
#include <algorithm>

namespace GyanisScribe::Pin
{

    PinManager::PinManager(Settings::ISettingsStore &settings, Platform::IPlatformIntegration &integration, QObject *parent) :
        QObject(parent), m_settings(settings), m_integration(integration)
    {
    }

    PinManager::~PinManager()
    {
        closeAll();
    }

    IPinWindow &PinManager::addPin(const QImage &image, const QPoint &globalTopLeft)
    {
        auto  pin = std::make_unique<PinWindow>(image, m_settings, m_integration);
        auto *raw = pin.get();

        // 窗口自己关掉了（Esc 或菜单里的关闭）就要从登记里摘掉，不然这里会留一个已析构的指针
        QObject::connect(raw, &PinWindow::closedByUser, this, [this, raw] { forget(raw); });

        m_pins.push_back(std::move(pin));
        m_pins.back()->move(globalTopLeft);
        m_pins.back()->show();
        emit pinCountChanged(count());
        return *raw;
    }

    int PinManager::count() const noexcept
    {
        return static_cast<int>(m_pins.size());
    }

    IPinWindow *PinManager::pinAt(int index) const
    {
        if (index < 0 || index >= static_cast<int>(m_pins.size()))
        {
            return nullptr;
        }
        return m_pins[static_cast<std::size_t>(index)].get();
    }

    QVector<PinState> PinManager::states() const
    {
        QVector<PinState> collected;
        collected.reserve(static_cast<int>(m_pins.size()));
        for (const auto &pin: m_pins)
        {
            collected.push_back(pin->state());
        }
        return collected;
    }

    Core::Expected<void> PinManager::saveLayout()
    {
        return Pin::saveLayout(m_settings, states());
    }

    void PinManager::closeAll()
    {
        // 逐个关掉：每个窗口关的时候会回调 forget，这里不能边遍历边 erase，先整份挪出来再收尾
        auto pending = std::move(m_pins);
        m_pins.clear();
        for (auto &pin: pending)
        {
            pin->close();
        }
        emit pinCountChanged(0);
    }

    void PinManager::forget(PinWindow *window)
    {
        const auto found = std::ranges::find_if(m_pins, [window](const auto &held) { return held.get() == window; });
        if (found == m_pins.end())
        {
            return;
        }

        (*found)->deleteLater(); // 回调可能来自该窗口自己的事件处理，立即析构会踩在栈上
        m_pins.erase(found);
        emit pinCountChanged(count());
    }

} // namespace GyanisScribe::Pin
