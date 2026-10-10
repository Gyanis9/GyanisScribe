// 崩溃转储端到端用例的「被检者」：装好兜底，然后真的崩一次。
// 它刻意不是产品的一部分——「让程序崩」这件事不该有产品入口，所以单独一个测试专用可执行体。
#include "App/CrashGuard.h"

#include <QtGlobal>

#include <filesystem>
#include <string>

#ifdef Q_OS_WIN
int wmain(const int argumentCount, wchar_t **arguments)
{
    if (argumentCount < 2)
    {
        return 2;
    }

    const std::wstring dumpDirectory = arguments[1];
    const std::wstring version       = argumentCount >= 3 ? std::wstring{arguments[2]} : std::wstring{L"unknown"};
    GyanisScribe::App::installCrashDumpWriter(std::filesystem::path{dumpDirectory}, version);

    // 读空指针：走的是真正的访问违例，与产品里最可能崩的那一类（坏指针、越界解引用）同一条路径
    volatile const int *probe = nullptr;
    return *probe;
}
#else
// 非 Windows 上 installCrashDumpWriter 是空操作，端到端那条用例也跳过；这里只保证它能编
int main()
{
    return 0;
}
#endif
