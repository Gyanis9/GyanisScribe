// 崩溃兜底的两层判据：命名规则（纯函数）与「真崩一次要留下转储」（端到端起探针进程）。
#include "Platform/CrashGuard.h"

#include <QtGlobal>

#include <QProcess>
#include <QString>

#include <gtest/gtest.h>

#include <filesystem>
#include <string>
#include <vector>

using GyanisScribe::Platform::crashDumpFileName;
using GyanisScribe::Platform::CrashStamp;
using GyanisScribe::Platform::kCrashExitCode;

namespace
{
    /// 探针可执行体的位置由 CMake 塞进来（$<TARGET_FILE:CrashProbe>）
    [[nodiscard]] std::filesystem::path scratchDirectory()
    {
        // 目录名带着这个用例的名字：ctest 把每条用例当独立进程并行起，撞名会互相删掉对方的转储
        return std::filesystem::temp_directory_path() / "GyanisScribe-CrashGuardEndToEnd";
    }
} // namespace

/**
 * @brief 钉住转储文件名的形状（手算的期望值，不是「像不像」那种半句话）
 * @details 文件名是用户反馈时要一起发回来的东西：版本号、时刻、进程号三样少一样就分不清是哪一次崩的。
 *          个位数补零也要钉住——不补的话同一天的两次崩溃按字面排序会乱。
 */
TEST(CrashGuardNaming, DumpNameCarriesVersionTimestampAndProcessId)
{
    EXPECT_EQ(crashDumpFileName(L"0.0.1", CrashStamp{2026, 10, 10, 19, 5, 4}, 4712), L"GyanisScribe_0.0.1_20261010-190504_4712.dmp");
    EXPECT_EQ(crashDumpFileName(L"1.2", CrashStamp{2026, 1, 2, 3, 4, 5}, 7), L"GyanisScribe_1.2_20260102-030405_7.dmp");
}

/**
 * @brief 端到端：真的崩一次进程，要求它留下能用的 minidump
 * @details 兜底这种代码「看起来接上了」和「真能落盘」是两件事：过滤器没装、目录建不出来、
 *          dbghelp 没链上，全都只在真崩一次时才暴露。所以起一个测试专用的探针进程，
 *          让它读空指针，再检查落地的文件。
 */
TEST(CrashGuardEndToEnd, CrashingProcessLeavesAMiniDump)
{
#ifndef Q_OS_WIN
    // 非 Windows 上 installCrashDumpWriter 是空操作（崩溃现场由系统自己的转储策略落地），
    // 探针那一头也只保证能编；这一条只在装过滤器的那一档上有意义
    GTEST_SKIP() << "非 Windows 没有进程内转储可装";
#endif

    const auto      directory = scratchDirectory();
    std::error_code ignored;
    std::filesystem::remove_all(directory, ignored);
    std::filesystem::create_directories(directory);

    QProcess probe;
    probe.start(QString::fromUtf8(GYANISSCRIBE_CRASH_PROBE), {QString::fromStdWString(directory.wstring()), QStringLiteral("9.9.9-probe")});
    ASSERT_TRUE(probe.waitForStarted(10000)) << "探针起不来：" << GYANISSCRIBE_CRASH_PROBE;
    ASSERT_TRUE(probe.waitForFinished(20000)) << "探针没有按时崩完（兜底可能把自己卡住了）";
    EXPECT_EQ(probe.exitCode(), kCrashExitCode) << "崩溃的退出码要与 CI 认的那一个同解，否则崩了也被当成正常退出";

    std::vector<std::filesystem::path> dumps;
    for (const auto &entry: std::filesystem::directory_iterator(directory))
    {
        if (entry.path().extension() == ".dmp")
        {
            dumps.push_back(entry.path());
        }
    }
    ASSERT_EQ(dumps.size(), 1U) << "崩一次应当恰好留下一份转储";
    EXPECT_GT(std::filesystem::file_size(dumps.front()), 1024U) << "转储小得不像话：多半是空文件，写的时候 dbghelp 没拿到现场";
    EXPECT_NE(dumps.front().filename().wstring().find(L"9.9.9-probe_"), std::wstring::npos) << "文件名里要带版本号，反馈时才知道是哪一版崩的";

    std::filesystem::remove_all(directory, ignored);
}
