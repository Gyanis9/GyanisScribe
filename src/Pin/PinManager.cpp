#include "Pin/PinManager.h"

#include "Core/Logger.h"
#include "Core/Paths.h"
#include "Core/Text.h"
#include "Pin/PinWindow.h"
#include "Settings/ISettingsStore.h"

#include <QImage>
#include <QPoint>

#include <algorithm>
#include <filesystem>
#include <format>

namespace
{
    /// 布局用的贴图缓存目录：像素放这儿，配置里只留路径
    std::filesystem::path pinCacheDirectory()
    {
        return GyanisScribe::Core::Paths::cacheDirectory() / "Pins";
    }

    /// 缓存里的文件名按张序给：一次写整批时，时间戳名会撞在同一个毫秒上，剩下最后一张
    QString pinCacheFileName(const int index)
    {
        return QStringLiteral("pin-%1.png").arg(index);
    }
} // namespace

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

    Core::Expected<void> PinManager::saveLayout()
    {
        const auto directory = pinCacheDirectory();

        // 先把这个专用目录里上一次留下的清掉：目录只装本功能写下的图，
        // 张数变少时旧的那几张若不清，就成了布局指不到的残留
        std::error_code cleared;
        if (const auto ensured = Core::Paths::ensureDirectory(directory); ensured)
        {
            for (const auto &entry: std::filesystem::directory_iterator(directory, std::filesystem::directory_options::skip_permission_denied, cleared))
            {
                std::filesystem::remove(entry.path(), cleared);
            }
        }

        QVector<PinState> states;
        states.reserve(static_cast<int>(m_pins.size()));
        for (int index = 0; index < count(); ++index)
        {
            auto      *pin   = pinAt(index);
            const auto saved = pin->saveToFile(directory, pinCacheFileName(index));
            if (!saved)
            {
                // 一张写不下去就整批不存：存半份布局会还原出一个与用户留下的桌面不一致的样子
                return std::unexpected(saved.error());
            }

            auto state      = pin->state();
            state.imageFile = *saved;
            states.push_back(state);
        }
        return Pin::saveLayout(m_settings, states);
    }

    Core::Expected<int> PinManager::restoreLayout()
    {
        const auto loaded = Pin::loadLayout(m_settings);
        if (!loaded)
        {
            return std::unexpected(loaded.error());
        }

        int restored = 0;
        for (const auto &state: *loaded)
        {
            QImage image;
            if (!image.load(state.imageFile))
            {
                // 图被用户删掉了：少摆一张是他自己的结果，不该因此把还在的那几张也放弃还原
                Core::Logger::warning(std::format("贴图恢复跳过一张：{} 读不出图来。布局里的几何也一并丢了", state.imageFile.toStdString()));
                continue;
            }

            auto &pin = addPin(image, state.position);
            pin.setScale(state.scale);
            pin.setRotationDegrees(state.rotationDegrees);
            pin.setOpacityLevel(state.opacity);

            // 位置排在几何参数之后：缩放与旋转都绕着图片中心重排，窗口左上会跟着挪，
            // 先摆位置再改尺寸就等于把记下来的那个点当成了「参数生效前」的位置
            m_pins.back()->move(state.position);
            ++restored;
        }
        return restored;
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
