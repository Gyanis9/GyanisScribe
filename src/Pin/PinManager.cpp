#include "Pin/PinManager.h"

#include "Core/Logger.h"
#include "Core/Paths.h"
#include "Core/Text.h"
#include "Pin/PinWindow.h"
#include "Settings/ISettingsStore.h"
#include "Settings/SettingsKeys.h"

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
        // 识别请求逐张转发出去：贴图在外头拿不到，出口不收在这一层就没人能替它识别
        QObject::connect(raw, &PinWindow::ocrRequested, this, &PinManager::pinOcrRequested);
        // 取色同理：窗口只管「点到了哪一格」，复制到剪贴板与提示都是界面层的事
        QObject::connect(raw, &PinWindow::colorPicked, this, &PinManager::pinColorPicked);
        // 分组改动只接意图、由管理器落账：哪张在哪组是这一层的账，窗口自己改会与管理器各记一半
        QObject::connect(raw, &PinWindow::moveToGroupRequested, this,
                         [this, raw](int group)
                         {
                             const auto found = std::ranges::find_if(m_pins, [raw](const auto &held) { return held.get() == raw; });
                             if (found == m_pins.end())
                             {
                                 return; // 菜单还开着时那张已被关掉：这一击没有可落账的对象
                             }
                             if (const auto moved = movePinToGroup(static_cast<int>(found - m_pins.begin()), group); !moved)
                             {
                                 Core::Logger::warning(moved.error().message);
                             }
                         });

        // 新贴图落在当前显示的那一组：它在别的组里会一贴出来就看不见，最容易被当成贴图坏了
        raw->setGroup(m_activeGroup);

        m_pins.push_back(std::move(pin));
        m_pins.back()->move(globalTopLeft);
        m_pins.back()->show();
        applyGroupVisibility();
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
            auto *pin = pinAt(index);
            // 恢复用的那一份永远是 PNG 且交给 Qt 默认质量：设置里的格式只管「用户按保存」那一路，
            // 拿有损档存恢复数据会让桌面每次重启都糊一点，而没人要求过这种事
            const auto saved = pin->saveToFile(directory, pinCacheFileName(index), -1);
            if (!saved)
            {
                // 一张写不下去就整批不存：存半份布局会还原出一个与用户留下的桌面不一致的样子
                return std::unexpected(saved.error());
            }

            auto state      = pin->state();
            state.imageFile = *saved;
            states.push_back(state);
        }

        if (const auto written = Pin::saveLayout(m_settings, states); !written)
        {
            return written;
        }
        // 活动组跟着布局一起记：不记的话重启后停在第一组，别的组里的贴图就像丢了
        return m_settings.setValue(Settings::kPinActiveGroup, m_activeGroup);
    }

    Core::Expected<int> PinManager::restoreLayout()
    {
        const auto loaded = Pin::loadLayout(m_settings);
        if (!loaded)
        {
            return std::unexpected(loaded.error());
        }

        // 活动组先落定再摆图：addPin 把新贴图挂在活动组上，晚落定会让这一批全挤进第一组
        m_activeGroup = m_settings.intValue(Settings::kPinActiveGroup, kFirstPinGroup);

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
            m_pins.back()->setGroup(state.group);

            // 位置排在几何参数之后：缩放与旋转都绕着图片中心重排，窗口左上会跟着挪，
            // 先摆位置再改尺寸就等于把记下来的那个点当成了「参数生效前」的位置
            m_pins.back()->move(state.position);
            ++restored;
        }

        // 记下的活动组可能已经没有贴图了（那一组的图被删了）：回落到第一组并把显隐重算一遍，
        // 否则恢复出来是一张都看不见，用户以为贴图全丢了
        normalizeActiveGroup();
        applyGroupVisibility();
        return restored;
    }

    int PinManager::clearMouseThrough()
    {
        // 只动在穿透的那几张：没在穿透的调一次 setMouseThrough(false) 是空转，
        // 而在没这能力的平台上那一次调用会回错，计数就假了
        auto restored = 0;
        for (const auto &pin: m_pins)
        {
            if (pin == nullptr || !pin->isMouseThrough())
            {
                continue;
            }
            if (const auto result = pin->setMouseThrough(false); result)
            {
                ++restored;
            } else
            {
                Core::Logger::warning(std::format("有一张贴图的穿透没取消掉：{}", result.error().message));
            }
        }
        return restored;
    }

    int PinManager::visiblePinCount() const
    {
        auto visible = 0;
        for (const auto &pin: m_pins)
        {
            // 空位是窗口自己析构后被摘掉的痕迹：那种张既看不见也不该计数
            if (pin != nullptr && !pin->isConcealed())
            {
                ++visible;
            }
        }
        return visible;
    }

    int PinManager::setAllPinsConcealed(const bool concealed)
    {
        m_allConcealed = concealed;
        return applyGroupVisibility();
    }

    int PinManager::activeGroup() const noexcept
    {
        return m_activeGroup;
    }

    QVector<int> PinManager::groupsInUse() const
    {
        QVector<int> groups;
        for (const auto &pin: m_pins)
        {
            if (pin == nullptr || groups.contains(pin->group()))
            {
                continue;
            }
            groups.append(pin->group());
        }
        std::ranges::sort(groups);
        return groups;
    }

    Core::Expected<int> PinManager::switchToNextGroup()
    {
        const auto groups = groupsInUse();
        if (groups.size() < 2)
        {
            return std::unexpected(Core::Error{Core::ErrorCode::InvalidArgument, GyanisScribe::Core::uiTextUtf8("现在只有 1 个贴图分组，没有下一组可切。"
                                                                                                                "在贴图的右键菜单里选「移动到分组 → 新建分组」才会多出一组")});
        }

        // 活动组不在册（刚被关掉最后一张）时从第一组接着走：indexOf 给 -1，加一取模正好落回 0
        const auto current = groups.indexOf(m_activeGroup);
        m_activeGroup      = groups[(current + 1) % groups.size()];
        applyGroupVisibility();
        return m_activeGroup;
    }

    Core::Expected<void> PinManager::movePinToGroup(const int index, const int group)
    {
        if (index < 0 || index >= count() || m_pins[static_cast<std::size_t>(index)] == nullptr)
        {
            return std::unexpected(Core::Error{Core::ErrorCode::InvalidArgument,
                                               GyanisScribe::Core::uiFormat(GyanisScribe::Core::uiTextUtf8("要移到分组的贴图不在册：索引 {}。请重新在贴图上打开菜单"), index)});
        }

        const int clamped = group < kFirstPinGroup ? kFirstPinGroup : group;
        m_pins[static_cast<std::size_t>(index)]->setGroup(clamped);

        // 移出当前组的当场收起、留在当前组的照旧显示：一次调用把显隐与菜单里的分组清单都重算
        normalizeActiveGroup();
        applyGroupVisibility();
        emit pinMovedToGroup(clamped);
        return {};
    }

    void PinManager::normalizeActiveGroup()
    {
        const auto groups = groupsInUse();
        if (groups.isEmpty())
        {
            m_activeGroup = kFirstPinGroup;
            return;
        }
        if (!groups.contains(m_activeGroup))
        {
            // 活动组的最后一张被关掉了：回落到第一组，否则桌上是一张都看不见，用户不知道去哪找
            m_activeGroup = groups.front();
        }
    }

    int PinManager::applyGroupVisibility()
    {
        const auto groups  = groupsInUse();
        auto       changed = 0;
        for (const auto &pin: m_pins)
        {
            if (pin == nullptr)
            {
                continue;
            }
            pin->setGroupChoices(groups);

            const bool concealed = m_allConcealed || pin->group() != m_activeGroup;
            // 已经是那一档的不动：重复 hide()/show() 会让窗口自己闪一下
            if (pin->isConcealed() != concealed)
            {
                pin->setConcealed(concealed);
                ++changed;
            }
        }
        return changed;
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
        // 一张都不剩时活动组回到第一组：下次贴一张回来落在有名字的一组上，而不是一个幽灵组号
        normalizeActiveGroup();
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
        // 关掉的若是活动组的最后一张，剩下的组要顶上来：不然桌上一张不剩，看不出是关掉了还是出错了
        normalizeActiveGroup();
        applyGroupVisibility();
        emit pinCountChanged(count());
    }

} // namespace GyanisScribe::Pin
