#include "Core/Logger.h"

#include <gtest/gtest.h>

#include <filesystem>
#include <format>
#include <fstream>
#include <string>

using GyanisScribe::Core::ErrorCode;
using GyanisScribe::Core::Logger;
using GyanisScribe::Core::Severity;

namespace
{
    /// 每个用例带自己的目录名，并行跑时不会共用同一棵临时树
    std::filesystem::path probeDirectory(std::string_view name)
    {
        return std::filesystem::temp_directory_path() / std::format("GyanisScribe-Logger-{}", name);
    }

    std::string readFile(const std::filesystem::path &path)
    {
        std::ifstream input(path, std::ios::binary);
        return std::string{std::istreambuf_iterator<char>{input}, std::istreambuf_iterator<char>{}};
    }

    bool fileContains(const std::filesystem::path &path, std::string_view text)
    {
        return readFile(path).find(text) != std::string::npos;
    }
} // namespace

/**
 * @brief 钉住 Logger 从「未初始化」到「重复初始化不切走」的整条生命周期
 * @details 状态是进程级单例，所以本文件刻意只留这一条用例：拆成多条会让「先跑哪条」决定成败，
 *          而 ctest 逐用例起进程与直接跑整枚可执行体两种顺序并不相同。顺序排成「先在未关闭的句柄上
 *          验早退分支，再 shutdown 验冲刷」——中途一旦 shutdown，重复初始化就变成合法的新建。
 */
TEST(Logger, LifecycleKeepsFirstSinkAcrossRepeatedInitialize)
{
    const auto      firstDirectory  = probeDirectory("LifecycleFirst");
    const auto      secondDirectory = probeDirectory("LifecycleSecond");
    const auto      firstLogFile    = firstDirectory / "GyanisScribe.log";
    std::error_code cleanupError;
    std::filesystem::remove_all(firstDirectory, cleanupError);
    std::filesystem::remove_all(secondDirectory, cleanupError);

    // 兜底通道：初始化之前的写入要出声（stderr）而不是被吞掉，且不许把状态置成已初始化
    ASSERT_FALSE(Logger::isInitialized()) << "用例开始前不该有人先把日志接管走，那样后面的判据全是空的";
    Logger::log(Severity::Warning, "初始化之前的探针");
    ASSERT_FALSE(Logger::isInitialized()) << "初始化之前的写入把日志接管了，兜底分支就变成了主分支";

    // 拒绝面：路径中段是一个已存在的普通文件，目录建不出来时必须返回原因而不是静默成功
    const auto blocker = firstDirectory.parent_path() / "GyanisScribe-Logger-blocker";
    std::filesystem::remove(blocker, cleanupError);
    {
        std::ofstream blockerFile(blocker);
        blockerFile << "占位";
    }
    const auto rejected = Logger::initialize(blocker / "logs");
    ASSERT_FALSE(rejected.has_value()) << "目录建不出来却返回了成功，调用方将无法知道日志根本没落地";
    EXPECT_EQ(rejected.error().code, ErrorCode::FileWriteFailed) << "故障类别应当是落盘失败，而不是别的兜底档";
    EXPECT_NE(rejected.error().message.find("日志目录建不出来"), std::string::npos) << "文案要写给用户看的中文原因，实际是：" << rejected.error().message;
    ASSERT_FALSE(Logger::isInitialized()) << "初始化失败之后仍然标记为已接管，后续写入会进一条不存在的通道";

    // 接管之后重复 initialize 必须早退而不换目录：换了就把已经拿着句柄的调用方的读数挪走。
    // 这一步在 shutdown 之前，句柄还开着；flush_on(warn) 保证这条 Warning 当场可见。
    ASSERT_TRUE(Logger::initialize(firstDirectory).has_value()) << "临时目录下应当能建出日志目录";
    ASSERT_TRUE(Logger::isInitialized());
    ASSERT_TRUE(Logger::initialize(secondDirectory).has_value()) << "重复调用按约定返回成功";
    Logger::warning("落在哪一路的判据");
    EXPECT_TRUE(fileContains(firstLogFile, "落在哪一路的判据")) << "第二次 initialize 把输出换到了另一棵目录：已经拿着句柄的调用方从此看不见日志";
    EXPECT_FALSE(std::filesystem::exists(secondDirectory / "GyanisScribe.log")) << "第二次 initialize 真的建了新文件，早退分支没生效";

    // Info 级也要落盘，并在关闭时冲刷干净
    Logger::info("日志探针：区域截图已确认");
    Logger::shutdown();
    EXPECT_FALSE(Logger::isInitialized()) << "shutdown 之后仍报已接管，后续写入会走一条已关闭的通道";
    EXPECT_TRUE(fileContains(firstLogFile, "日志探针：区域截图已确认")) << "关闭时没冲刷，Info 级那条丢在缓冲区里";
}
