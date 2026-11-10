#include "Plugins/CaptureSources/DxgiCaptureSource.h"

#ifdef Q_OS_WIN

#include "Core/Capture/BgraFrame.h"
#include "Plugins/CaptureSources/DxgiAcquirePolicy.h"
#include "Plugins/CaptureSources/QScreenCaptureSource.h"
#include "Core/Capture/ScreenUtils.h"
#include "Base/Logger.h"
#include "Base/Text.h"

#include <QCursor>
#include <QGuiApplication>
#include <QImage>
#include <QScreen>

#include <d3d11.h>
#include <dxgi.h>
#include <dxgi1_2.h>
#include <winrt/base.h>

#include <algorithm>
#include <array>
#include <format>
#include <vector>

namespace GyanisScribe::Plugins
{
    namespace
    {
        /// 等一帧的时间：到点说明桌面这一格没变过，交回上一张——录屏要的是「每一格都有一帧可写」
        constexpr UINT kAcquireTimeoutMs = 100;

        /// 回溯抓帧只取这一刻已就绪的帧：0 表示不等，静态画面直接走缓存
        constexpr unsigned int kTryGrabTimeoutMs = 0;

        /// 一帧的等待与重建最多试两轮：中途切分辨率/锁屏会让重复接口失效，重建一次能救回来
        constexpr int kMaximumAttempts = 2;

        QString fromDeviceName(const wchar_t *deviceName)
        {
            return QString::fromWCharArray(deviceName);
        }

        /// 认出这次抓哪块屏；认不出时交回与 grabScreen 同一套原因
        Base::Expected<QScreen *> resolveTargetScreen(const QString &screenName)
        {
            QScreen *target = screenName.isEmpty() ? Core::screenAtGlobalPoint(QCursor::pos()) : Core::screenByName(screenName);
            if (target != nullptr)
            {
                return target;
            }
            if (!screenName.isEmpty())
            {
                // 屏名对不上多半是多显示器被拔掉：让调用方按光标所在屏重抓，别按名字猜
                return std::unexpected(Base::Error{Base::ErrorCode::ScreenUnavailable,
                                                   Base::uiFormat(Base::uiTextUtf8("找不到名为「{}」的屏幕：该屏幕可能已被移除。请在不指定屏幕名的情况下重新截取（此时按光标所在屏幕处理）。"),
                                                                  screenName.toStdString())});
            }
            // 空屏名走光标所在屏；这里为空说明光标点不在任何屏上，通常是 Qt 还没报出屏幕
            return std::unexpected(Base::Error{Base::ErrorCode::ScreenUnavailable, Base::uiTextUtf8("Qt 没有报出任何屏幕：请在 QGuiApplication 建好之后再抓图")});
        }
    } // namespace

    /**
     * @brief 设备、每块屏的重复接口与暂存纹理
     * @details 重复接口按屏缓存：建一次要几十毫秒，而录屏每秒都要抓；DXGI 也要求
     *          同一块屏在同一进程里只留一份重复接口。设备按适配器建：混合显卡的机器上两块屏
     *          常常挂在两块适配器上，拿一块适配器的设备去开另一块的输出会被拒。
     */
    class DxgiScreenDuplicator
    {
    public:
        DxgiScreenDuplicator()
        {
            // 枚举适配器要经过工厂；设备按需建（见 ensureDevice），建不起来的适配器在那一步记日志
            winrt::com_ptr<IDXGIFactory1> factory;
            if (FAILED(CreateDXGIFactory1(__uuidof(IDXGIFactory1), factory.put_void())))
            {
                Base::Logger::warning("DXGI 抓帧：建不了 DXGI 工厂，这一路用不了");
                return;
            }

            for (UINT index = 0;; ++index)
            {
                auto adapter = winrt::com_ptr<IDXGIAdapter>{};
                if (factory->EnumAdapters(index, adapter.put()) == DXGI_ERROR_NOT_FOUND)
                {
                    break;
                }
                m_adapters.push_back(AdapterContext{std::move(adapter), nullptr, nullptr});
            }
        }

        /**
         * @brief 这一路在这台机器上能不能用
         * @return true 表示至少有一块适配器能建起 D3D11 设备
         */
        [[nodiscard]] bool isUsable()
        {
            for (auto &context: m_adapters)
            {
                if (ensureDevice(context))
                {
                    return true;
                }
            }
            return false;
        }

        /// 真正从 DXGI 取到过几帧（回落发生时它停在原处）
        [[nodiscard]] int framesFromDxgi() const noexcept
        {
            return m_dxgiFrames;
        }

        /**
         * @brief 抓一块屏的当前画面
         * @param screenRect Qt 报的那块屏的物理矩形：原点原样保留，宽高按缩放因子放大
         * @return 成功返回物理像素的图；这一台/这一块屏做不到时返回原因
         * @details 认屏按桌面坐标而不是屏名：Qt 在 Windows 上报的是显示器友好名（例如 H27T22S），
         *          与 DXGI 的设备名不是一回事，按名字找必然全部对不上。
         */
        Base::Expected<QImage> grab(const QRect &screenRect)
        {
            return grab(screenRect, kAcquireTimeoutMs);
        }

        /**
         * @brief 抓一块屏的当前画面，最多等 timeoutMs 毫秒
         * @param screenRect Qt 报的那块屏的物理矩形
         * @param timeoutMs 等新帧的上限；0 表示只取这一刻已就绪的帧，静态画面走缓存
         * @return 成功返回物理像素的图；这一台/这一块屏做不到时返回原因
         */
        Base::Expected<QImage> tryGrab(const QRect &screenRect, const UINT timeoutMs)
        {
            return grab(screenRect, timeoutMs);
        }

        /**
         * @brief 把刚交付出去的一帧种回缓存
         * @param screenRect Qt 报的那块屏的物理矩形
         * @param frame 这一格要交回的帧
         * @details 缓存必须始终是「最近一次交付出去的那一格」：全黑帧被换成 Qt 帧之后，
         *          留黑帧在这里会让下一次 0 超时取帧原样交回黑图，又触发一次阻塞的 Qt 抓帧。
         */
        void rememberFrame(const QRect &screenRect, const QImage &frame)
        {
            if (OutputState *state = stateFor(screenRect); state != nullptr)
            {
                state->lastFrame = frame;
            }
        }

    private:
        /// 抓帧的公共路径：认屏、建设备、取帧，两步之间按 Busy 重开一轮
        Base::Expected<QImage> grab(const QRect &screenRect, const UINT timeoutMs)
        {
            // 分辨率或排布变了之后，旧屏幕的重复接口与暂存纹理都认不回新输出：先按当前各屏的
            // 物理矩形清掉旧条目，失效的重复接口随状态一起释放，下一次按新尺寸重建
            std::vector<QRect> currentRects;
            for (auto *screen: QGuiApplication::screens())
            {
                currentRects.push_back(Core::physicalScreenRect(screen->geometry(), screen->devicePixelRatio()));
            }
            std::erase_if(m_outputs, [&currentRects](const OutputState &state) { return std::ranges::find(currentRects, state.screenRect) == currentRects.end(); });

            auto *state = stateFor(screenRect);
            if (state == nullptr)
            {
                return std::unexpected(Base::Error{
                        Base::ErrorCode::ScreenUnavailable,
                        Base::uiFormat(Base::uiTextUtf8("DXGI 桌面重复认不出屏幕（{}×{}，原点 {}）：Qt 报的几何与适配器的输出坐标对不上。请重试一次，或改用 Qt 的抓帧原语"),
                                       screenRect.width(), screenRect.height(), screenRect.left())});
            }
            const auto &deviceName = state->deviceName;

            auto &adapter = m_adapters[state->adapterIndex];
            if (!ensureDevice(adapter))
            {
                return std::unexpected(
                        Base::Error{Base::ErrorCode::PlatformUnsupported,
                                    Base::uiFormat(Base::uiTextUtf8("这块屏所挂的显示适配器建不起 D3D11 设备（{}）：请改用 Qt 的抓帧原语"), deviceName.toStdString())});
            }

            for (int attempt = 0; attempt < kMaximumAttempts; ++attempt)
            {
                if (state->duplication == nullptr && !openDuplication(*state, adapter, deviceName))
                {
                    return std::unexpected(
                            Base::Error{Base::ErrorCode::PlatformUnsupported,
                                        Base::uiFormat(Base::uiTextUtf8("这块屏开不了 DXGI 桌面重复（{}）：远程会话、驱动或桌面格式可能不支持。请改用 Qt 的抓帧原语"),
                                                       deviceName.toStdString())});
                }

                const auto frame = acquireFrame(*state, adapter, timeoutMs);
                if (frame)
                {
                    ++m_dxgiFrames;
                    return frame;
                }
                if (frame.error().code != Base::ErrorCode::Busy)
                {
                    return frame;
                }
                // Busy 表示重复接口已经失效（切分辨率、锁屏、别的进程抢走了）：放掉重开一轮
                state->reset();
            }

            return std::unexpected(Base::Error{Base::ErrorCode::ScreenUnavailable,
                                               Base::uiFormat(Base::uiTextUtf8("DXGI 抓帧连续两轮失效（{}）：画面刚切过分辨率或会话被锁。请重试一次"), deviceName.toStdString())});
        }

        /// 一块适配器与它按需建起来的设备
        struct AdapterContext
        {
            winrt::com_ptr<IDXGIAdapter>        adapter; ///< 枚举出来的适配器
            winrt::com_ptr<ID3D11Device>        device;  ///< 按需建
            winrt::com_ptr<ID3D11DeviceContext> context; ///< 立即上下文，拷贝与映射走它
        };

        /// 一块屏的状态
        struct OutputState
        {
            QString                                deviceName;       ///< DXGI 的设备名，开重复接口时靠它认输出
            QRect                                  screenRect;       ///< Qt 报的物理矩形，用来认屏
            std::size_t                            adapterIndex = 0; ///< 这块屏挂在哪块适配器上
            winrt::com_ptr<IDXGIOutputDuplication> duplication;      ///< 这块屏的重复接口
            winrt::com_ptr<ID3D11Texture2D>        staging;          ///< CPU 可读的暂存纹理
            QSize                                  stagingSize;      ///< 暂存纹理的尺寸，切分辨率时要重建
            QImage                                 lastFrame;        ///< 上一帧：等待超时时交回它

            void reset()
            {
                duplication = nullptr;
                staging     = nullptr;
                lastFrame   = {};
            }
        };

        /// 给某一块适配器建设备；建过就直接交回真
        bool ensureDevice(AdapterContext &context)
        {
            if (context.device != nullptr && context.context != nullptr)
            {
                return true;
            }

            constexpr std::array kLevels{D3D_FEATURE_LEVEL_11_0, D3D_FEATURE_LEVEL_10_1};
            // 显式指定适配器时驱动类型必须给 UNKNOWN（D3D11CreateDevice 的约定）；
            // BGRA 支持位是桌面重复要的：少了它 DuplicateOutput 直接回 DXGI_ERROR_UNSUPPORTED
            const HRESULT created = D3D11CreateDevice(context.adapter.get(), D3D_DRIVER_TYPE_UNKNOWN, nullptr, D3D11_CREATE_DEVICE_BGRA_SUPPORT, kLevels.data(),
                                                      static_cast<UINT>(kLevels.size()), D3D11_SDK_VERSION, context.device.put(), nullptr, context.context.put());
            if (FAILED(created))
            {
                Base::Logger::warning(std::format("DXGI 抓帧：这块适配器建不起 D3D11 设备（HRESULT 0x{:08X}）", static_cast<unsigned>(created)));
                context.device  = nullptr;
                context.context = nullptr;
                return false;
            }
            return true;
        }

        /// 找或建某块屏的状态；桌面坐标对不上任何输出时返回空
        OutputState *stateFor(const QRect &screenRect)
        {
            const auto found = std::ranges::find_if(m_outputs, [&screenRect](const OutputState &state) { return state.screenRect == screenRect; });
            if (found != m_outputs.end())
            {
                return &*found;
            }

            for (std::size_t index = 0; index < m_adapters.size(); ++index)
            {
                for (UINT outputIndex = 0;; ++outputIndex)
                {
                    auto output = winrt::com_ptr<IDXGIOutput>{};
                    if (m_adapters[index].adapter->EnumOutputs(outputIndex, output.put()) == DXGI_ERROR_NOT_FOUND)
                    {
                        break;
                    }

                    DXGI_OUTPUT_DESC description{};
                    if (FAILED(output->GetDesc(&description)))
                    {
                        continue;
                    }

                    // 认屏按桌面坐标：Qt 的屏名是显示器友好名，与 DXGI 的设备名不是一回事
                    const RECT &box = description.DesktopCoordinates;
                    const QRect physical(box.left, box.top, box.right - box.left, box.bottom - box.top);
                    if (physical != screenRect)
                    {
                        continue;
                    }

                    OutputState state;
                    state.deviceName   = fromDeviceName(description.DeviceName);
                    state.screenRect   = physical;
                    state.adapterIndex = index;
                    m_outputs.push_back(std::move(state));
                    return &m_outputs.back();
                }
            }
            return nullptr;
        }

        /// 开这块屏的重复接口；失败记一条日志并交回假
        bool openDuplication(OutputState &state, AdapterContext &adapter, const QString &deviceName)
        {
            for (UINT outputIndex = 0;; ++outputIndex)
            {
                auto output = winrt::com_ptr<IDXGIOutput>{};
                if (adapter.adapter->EnumOutputs(outputIndex, output.put()) == DXGI_ERROR_NOT_FOUND)
                {
                    return false;
                }

                DXGI_OUTPUT_DESC description{};
                if (FAILED(output->GetDesc(&description)) || fromDeviceName(description.DeviceName) != deviceName)
                {
                    continue;
                }

                // 竖屏（旋转过）的输出拿不到桌面重复：DXGI 对非 0 度旋转一律回 DXGI_ERROR_UNSUPPORTED，
                // 这里先判一次，把「为什么不行」写进日志，而不是让调用方猜
                if (description.Rotation != DXGI_MODE_ROTATION_IDENTITY)
                {
                    Base::Logger::warning(std::format("DXGI 抓帧：屏「{}」是旋转过的输出，桌面重复不支持", deviceName.toStdString()));
                    return false;
                }

                winrt::com_ptr<IDXGIOutput1> output1;
                if (FAILED(output->QueryInterface(__uuidof(IDXGIOutput1), output1.put_void())))
                {
                    return false;
                }

                const HRESULT opened = output1->DuplicateOutput(adapter.device.get(), state.duplication.put());
                if (FAILED(opened))
                {
                    Base::Logger::warning(std::format("DXGI 抓帧：DuplicateOutput 失败（HRESULT 0x{:08X}）", static_cast<unsigned>(opened)));
                    return false;
                }
                return true;
            }
        }

        /// 取一帧：最多等 timeoutMs 毫秒，等待超时交回上一帧，重复接口失效交回 Busy（调用方据此重开）
        Base::Expected<QImage> acquireFrame(OutputState &state, AdapterContext &adapter, const UINT timeoutMs)
        {
            DXGI_OUTDUPL_FRAME_INFO info{};
            auto                    resource = winrt::com_ptr<IDXGIResource>{};
            const HRESULT           acquired = state.duplication->AcquireNextFrame(timeoutMs, &info, resource.put());

            // 超时、首帧未就绪、失效与其余失败都在纯决策层分档，这里只把每一档翻成错误对象
            switch (Dxgi::resolveAcquireOutcome(acquired, !state.lastFrame.isNull()))
            {
                case Dxgi::AcquireOutcome::CachedFrame:
                    // 桌面这一格没变过：交回上一张，录屏的每一格才有帧可写
                    return state.lastFrame;
                case Dxgi::AcquireOutcome::FirstFrameMissing:
                    return std::unexpected(
                            Base::Error{Base::ErrorCode::ScreenUnavailable, Base::uiTextUtf8("DXGI 桌面重复开了之后一直没等到第一帧：画面可能刚被锁屏或切走。请重试一次")});
                case Dxgi::AcquireOutcome::ReopenDuplication:
                    return std::unexpected(Base::Error{Base::ErrorCode::Busy, Base::uiTextUtf8("DXGI 桌面重复已失效（切分辨率或会话切换）：接口要重开一轮")});
                case Dxgi::AcquireOutcome::Failed:
                    return std::unexpected(Base::Error{Base::ErrorCode::ScreenUnavailable,
                                                       Base::uiFormat(Base::uiTextUtf8("DXGI 取帧失败（HRESULT 0x{:08X}）"), static_cast<unsigned>(acquired))});
                case Dxgi::AcquireOutcome::NewFrame:
                    break;
            }

            // 拿到帧之后不管走哪条路都要 ReleaseFrame：漏一次这块屏的重复接口就再也取不到帧
            const auto releaseFrame = [&state]
            {
                if (state.duplication != nullptr)
                {
                    state.duplication->ReleaseFrame();
                }
            };

            // LastPresentTime 为 0 表示这次唤醒只是指针动了：画面没变，交回上一张，省一次整屏拷贝
            const bool pointerOnly = info.LastPresentTime.QuadPart == 0;
            if (pointerOnly && !state.lastFrame.isNull())
            {
                releaseFrame();
                return state.lastFrame;
            }

            auto texture = winrt::com_ptr<ID3D11Texture2D>{};
            if (FAILED(resource->QueryInterface(__uuidof(ID3D11Texture2D), texture.put_void())))
            {
                releaseFrame();
                return std::unexpected(Base::Error{Base::ErrorCode::InternalError, Base::uiTextUtf8("DXGI 交回的帧不是 2D 纹理：这一版取帧路径与驱动对不上")});
            }

            D3D11_TEXTURE2D_DESC description{};
            texture->GetDesc(&description);
            if (description.Format != DXGI_FORMAT_B8G8R8A8_UNORM)
            {
                releaseFrame();
                return std::unexpected(
                        Base::Error{Base::ErrorCode::PlatformUnsupported, Base::uiFormat(Base::uiTextUtf8("桌面的像素格式是 0x{:X}，该通道只认 B8G8R8A8：请改用 Qt 的抓帧原语"),
                                                                                         static_cast<unsigned>(description.Format))});
            }

            const QSize size(static_cast<int>(description.Width), static_cast<int>(description.Height));
            if (state.staging == nullptr || state.stagingSize != size)
            {
                state.staging     = nullptr;
                state.stagingSize = size;

                D3D11_TEXTURE2D_DESC stagingDescription{};
                stagingDescription.Width          = description.Width;
                stagingDescription.Height         = description.Height;
                stagingDescription.MipLevels      = 1;
                stagingDescription.ArraySize      = 1;
                stagingDescription.Format         = description.Format;
                stagingDescription.SampleDesc     = {1, 0};
                stagingDescription.Usage          = D3D11_USAGE_STAGING;
                stagingDescription.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
                if (FAILED(adapter.device->CreateTexture2D(&stagingDescription, nullptr, state.staging.put())))
                {
                    releaseFrame();
                    return std::unexpected(Base::Error{Base::ErrorCode::InternalError, Base::uiTextUtf8("DXGI 抓帧：建暂存纹理失败。请重试一次；一直失败请改用 Qt 的抓帧原语")});
                }
            }

            adapter.context->CopyResource(state.staging.get(), texture.get());
            releaseFrame();

            D3D11_MAPPED_SUBRESOURCE mapped{};
            if (FAILED(adapter.context->Map(state.staging.get(), 0, D3D11_MAP_READ, 0, &mapped)))
            {
                return std::unexpected(Base::Error{Base::ErrorCode::InternalError, Base::uiTextUtf8("DXGI 抓帧：暂存纹理无法读取。请重试一次")});
            }
            QImage image = Core::imageFromBgraRows(static_cast<const uchar *>(mapped.pData), static_cast<int>(mapped.RowPitch), size);
            adapter.context->Unmap(state.staging.get(), 0);

            if (image.isNull())
            {
                return std::unexpected(Base::Error{Base::ErrorCode::InternalError, Base::uiTextUtf8("DXGI 抓帧：读出来的是空图。请重试一次")});
            }

            state.lastFrame = image;
            return image;
        }

        std::vector<AdapterContext> m_adapters;       ///< 枚举出来的适配器，设备按需建
        std::vector<OutputState>    m_outputs;        ///< 每块屏一份状态，按桌面坐标认
        int                         m_dxgiFrames = 0; ///< 真正从 DXGI 取到的帧数，供用例核对没有静默回落
    };

    DxgiCaptureSource::DxgiCaptureSource() : m_duplicator(std::make_unique<DxgiScreenDuplicator>()), m_qtFallback(std::make_unique<QScreenCaptureSource>())
    {
    }

    DxgiCaptureSource::~DxgiCaptureSource() = default;

    bool DxgiCaptureSource::isUsable() const noexcept
    {
        return m_duplicator != nullptr && m_duplicator->isUsable();
    }

    int DxgiCaptureSource::framesFromDxgi() const noexcept
    {
        return m_duplicator != nullptr ? m_duplicator->framesFromDxgi() : 0;
    }

    Base::Expected<Base::CapturedImage> DxgiCaptureSource::grabScreen(const QString &screenName) const
    {
        return grabScreenImpl(screenName, kAcquireTimeoutMs, "DXGI 抓帧", FallbackLogMode::EveryOccurrence);
    }

    Base::Expected<Base::CapturedImage> DxgiCaptureSource::tryGrabScreen(const QString &screenName) const
    {
        return grabScreenImpl(screenName, kTryGrabTimeoutMs, "回溯抓帧", FallbackLogMode::OnTransition);
    }

    Base::Expected<Base::CapturedImage> DxgiCaptureSource::grabScreenImpl(const QString &screenName, const unsigned int timeoutMs, const std::string_view logTag,
                                                                          const FallbackLogMode logMode) const
    {
        const auto resolved = resolveTargetScreen(screenName);
        if (!resolved)
        {
            return std::unexpected(resolved.error());
        }
        QScreen *target = *resolved;

        // 回落日志按入口定密度：一次性抓帧每次记；5Hz 的回溯路径只在进入回落状态时记一条
        const auto reportFallback = [this, logMode, logTag](const std::string_view message)
        {
            if (logMode == FallbackLogMode::EveryOccurrence)
            {
                Base::Logger::warning(message);
                return;
            }
            if (!m_fallbackWarningLogged)
            {
                m_fallbackWarningLogged = true;
                Base::Logger::warning(message);
            }
        };

        // 原点按 Qt 报的原生坐标原样保留、只放大尺寸：DXGI 的输出坐标与它同一套原点
        const QRect physicalRect = Core::physicalScreenRect(target->geometry(), target->devicePixelRatio());

        const auto grabbed = m_duplicator->tryGrab(physicalRect, timeoutMs);
        if (!grabbed)
        {
            // 降级不是错误：这台机器/这一刻这套原语用不了，Qt 那一份能抓就给用户抓到
            reportFallback(std::format("{}回落到 Qt：{}", logTag, grabbed.error().message));
            return m_qtFallback->grabScreen(target->name());
        }

        if (Dxgi::resolveFrameDisposition(Core::imageLooksAllBlack(*grabbed)) == Dxgi::FrameDisposition::FallbackToQt)
        {
            reportFallback(std::format("{}拿到全黑帧，改用 Qt 抓帧一次", logTag));
            const auto fallback = m_qtFallback->grabScreen(target->name());
            if (fallback)
            {
                // 缓存必须跟着交付走：留黑帧在缓存里，下一次 0 超时取帧会原样交回它，
                // 又触发一次阻塞的 Qt 抓帧；把 Qt 帧种回去，静态桌面下一格直接交缓存
                m_duplicator->rememberFrame(physicalRect, fallback->image);
            }
            return fallback;
        }

        // 这一格没有回落：DXGI 已经恢复，下一次再回落要重新记一条
        m_fallbackWarningLogged = false;
        return Core::frameFromGrabbedImage(*grabbed, target, target->geometry().size());
    }

    Base::Expected<Base::CapturedImage> DxgiCaptureSource::grabWindow(const quintptr windowHandle) const
    {
        return m_qtFallback->grabWindow(windowHandle);
    }

    std::optional<Core::CursorSprite> DxgiCaptureSource::grabCursor(const QString &screenName) const
    {
        return m_qtFallback->grabCursor(screenName);
    }

} // namespace GyanisScribe::Plugins

#endif // Q_OS_WIN
