#include "Platform/IStepEventSource.h"

#include "Base/Text.h"

#ifdef Q_OS_WIN
#include "Platform/Windows/WindowsStepEventSource.h"
#endif

namespace
{
    /**
     * @brief 没有全局输入监听的平台的降级实现
     */
    class UnsupportedStepEventSource final : public GyanisScribe::Platform::IStepEventSource
    {
    public:
        /**
         * @brief 拒绝启动，见 IStepEventSource::start()
         * @return PlatformUnsupported，并指一条替代做法
         */
        [[nodiscard]] GyanisScribe::Base::Expected<void> start() override
        {
            return std::unexpected(GyanisScribe::Base::Error{GyanisScribe::Base::ErrorCode::PlatformUnsupported,
                                                             GyanisScribe::Base::uiTextUtf8("这台平台还不能记录操作步骤：请用 Windows 版再试")});
        }

        /**
         * @brief 没装过任何钩子，见 IStepEventSource::stop()
         */
        void stop() override
        {
        }

        /**
         * @brief 永远不会在监听，见 IStepEventSource::isRunning()
         * @return false
         */
        [[nodiscard]] bool isRunning() const noexcept override
        {
            return false;
        }
    };
} // namespace

namespace GyanisScribe::Platform
{
    std::unique_ptr<IStepEventSource> createStepEventSource()
    {
#ifdef Q_OS_WIN
        return std::make_unique<WindowsStepEventSource>();
#else
        return std::make_unique<UnsupportedStepEventSource>();
#endif
    }
} // namespace GyanisScribe::Platform
