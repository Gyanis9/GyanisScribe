#include "App/SingleInstance.h"
#include "Platform/CrashGuard.h"

#include <gtest/gtest.h>

#include <QCoreApplication>
#include <QLocalServer>
#include <QString>

#include <chrono>
#include <exception>
#include <memory>
#include <stdexcept>
#include <string>
#include <thread>

using GyanisScribe::App::instanceKey;
using GyanisScribe::App::SingleInstance;
using GyanisScribe::Platform::crashMessageFor;
using GyanisScribe::Platform::kCrashExitCode;

namespace
{
    std::unique_ptr<QCoreApplication> application;

    class InstanceFixture : public ::testing::Test
    {
    protected:
        static void SetUpTestSuite()
        {
            // argv 与 argc 的缓冲要有静态存储期：Qt 只存指针不拷贝，
            // 而 QCoreApplication::applicationDirPath() 这类调用是在装置函数返回之后才读它们。
            static int   argumentCount  = 1;
            static char  argumentZero[] = "TestSingleInstance";
            static char *arguments[]    = {argumentZero, nullptr};
            application                 = std::make_unique<QCoreApplication>(argumentCount, arguments);
        }

        static void TearDownTestSuite()
        {
            application.reset();
        }

        void SetUp() override
        {
            // 套接字名是跨进程共用资源：ctest -j 下两条用例同时监听同一个名字，SetUp 里的
            // removeServer 会把对方的名字删掉，「第二次连得上」就变成随机红。
            // 每条用例把自己的应用名（进实例名）唯一化，收尾只清自己那份
            application->setApplicationName(QStringLiteral("GyanisScribeTest-") + QString::fromUtf8(::testing::UnitTest::GetInstance()->current_test_info()->name()));
            QLocalServer::removeServer(instanceKey());
        }
    };

    /// 有界等一会儿：本地套接字的 newConnection 要跑一轮事件循环才到
    void pumpEventsFor(const std::chrono::milliseconds budget)
    {
        const auto deadline = std::chrono::steady_clock::now() + budget;
        while (std::chrono::steady_clock::now() < deadline)
        {
            QCoreApplication::processEvents();
            std::this_thread::sleep_for(std::chrono::milliseconds{10});
        }
    }

} // namespace

/**
 * @brief 钉住崩溃文案：位置、日志目录与下一步都要在
 * @details 期望值不来自被测函数的输出，而是按「用户接下来会做什么」写死：
 *          只报异常 what() 的日志没人看得下去，也不知道日志去哪找。
 */
TEST(CrashGuard, MessageCarriesReasonLogDirectoryAndNextStep)
{
    const auto fromNothing = crashMessageFor(std::exception_ptr{}, "C:/logs");
    EXPECT_NE(fromNothing.toStdString().find("没有异常信息"), std::string::npos);
    EXPECT_NE(fromNothing.toStdString().find("C:/logs"), std::string::npos);

    const auto fromKnown = crashMessageFor(std::make_exception_ptr(std::runtime_error("帧序列封不上")), "C:/logs");
    EXPECT_NE(fromKnown.toStdString().find("帧序列封不上"), std::string::npos) << "异常里的原因必须原样带上";
    EXPECT_NE(fromKnown.toStdString().find("反馈"), std::string::npos) << "要给下一步，否则用户只能重开一次试试";

    const auto fromForeign = crashMessageFor(std::make_exception_ptr(42), "C:/logs");
    EXPECT_NE(fromForeign.toStdString().find("不是 std::exception"), std::string::npos) << "抛非标准异常也不能留空";

    EXPECT_NE(kCrashExitCode, 0) << "崩掉的退出码必须与「正常退出」分得开，CI 才抓得住";
}

/**
 * @brief 钉住实例名带应用标识
 * @details 名字写死会让开发版与安装版互相把对方当成「已经有人在跑了」。
 */
TEST_F(InstanceFixture, InstanceKeyCarriesApplicationName)
{
    // 应用名由 SetUp 按用例名定下，期望值照那个字面写：既不依赖机器，也不与被测函数同算法
    EXPECT_EQ(instanceKey(), QStringLiteral("GyanisScribe-GyanisScribeTest-InstanceKeyCarriesApplicationName"));
}

/**
 * @brief 钉住锁的排他性与「第二次启动能唤到前台」
 * @details 第二份实例照旧启动会两个进程抢同一批全局热键（后登记的把前一个顶掉），
 *          用户按一次截图却只弹一个窗口，这种不一致最难查。
 */
TEST_F(InstanceFixture, SecondAcquireFailsAndItsKnockReachesTheFirst)
{
    SingleInstance primary;
    ASSERT_TRUE(primary.acquire()) << "第一次 acquire 该拿到锁";
    ASSERT_TRUE(primary.acquire()) << "已经持锁的实例再问一次还是它自己";

    int knocks = 0;
    QObject::connect(&primary, &SingleInstance::secondLaunchRequested, &primary, [&knocks] { ++knocks; });

    // 监听器要跑一轮事件循环才能收下探针连接并补上管道实例：不泵一下，后面的敲门会因为没有可用管道而失败
    for (int i = 0; i < 20; ++i)
    {
        QCoreApplication::processEvents();
        std::this_thread::sleep_for(std::chrono::milliseconds{10});
    }

    SingleInstance secondary;
    EXPECT_FALSE(secondary.acquire()) << "已有持锁者时第二次 acquire 必须失败";
    for (int i = 0; i < 20; ++i)
    {
        QCoreApplication::processEvents();
        std::this_thread::sleep_for(std::chrono::milliseconds{10});
    }
    EXPECT_TRUE(secondary.notifyExisting()) << "敲门该真的连上对方";

    pumpEventsFor(std::chrono::seconds{2});
    EXPECT_GE(knocks, 1) << "连上就算一次敲门：探针与通知都该把主实例唤出来";
}

/**
 * @brief 钉住没人持锁时的通知是失败而不是挂住
 * @details 第二份实例在锁刚释放的窗口里会连不上；连不上要立刻返回 false，
 *          让调用方按「没唤起来」这条支路走，而不是把启动卡在半开的管道上。
 */
TEST_F(InstanceFixture, NotifyWithoutOwnerFailsFast)
{
    SingleInstance lonely;
    EXPECT_FALSE(lonely.notifyExisting());
}
