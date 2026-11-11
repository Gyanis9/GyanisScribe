#include "Platform/IStepEventSource.h"

#include <gtest/gtest.h>

#include <QCoreApplication>

using GyanisScribe::Platform::createStepEventSource;

/// 工厂必须给出一份对象；非 Windows 档由桩实现把 start() 报成 PlatformUnsupported
TEST(StepEventSource, FactoryReturnsAnObjectThatIsNotRunning)
{
    const auto source = createStepEventSource();
    ASSERT_NE(source, nullptr);
    EXPECT_FALSE(source->isRunning());
}

/// 真实钩子只在显式环境门控下装/卸：CI 与普通开发跑不会去碰全局鼠标
TEST(StepEventSource, RealHooksInstallAndUninstallOnlyWhenGated)
{
    if (!qEnvironmentVariableIsSet("GYANISSCRIBE_REAL_INPUT_HOOKS"))
    {
        GTEST_SKIP() << "set GYANISSCRIBE_REAL_INPUT_HOOKS=1 to exercise the real Win32 hooks";
    }
    const auto source = createStepEventSource();
    ASSERT_NE(source, nullptr);
    const auto started = source->start();
    ASSERT_TRUE(started.has_value()) << started.error().message;
    EXPECT_TRUE(source->isRunning());
    source->stop();
    EXPECT_FALSE(source->isRunning());
    source->stop(); // 幂等
}
