/**
 * @file CrashGuard.h
 * @brief 崩溃与未捕获异常的兜底：把现场落到日志，再交给系统写转储
 * @author Gyanis
 * @date 2026-10-10
 * @version 0.0.1
 * @copyright Copyright (c) . All rights reserved.
 */

#pragma once

#include <QString>

#include <cstdint>
#include <exception>
#include <filesystem>
#include <string>
#include <string_view>

namespace GyanisScribe::App
{

    /// 未捕获异常导致退出时的进程退出码：非 0 且与「正常退出」分得开，CI 才抓得住
    inline constexpr int kCrashExitCode = 70;

    /**
     * @brief 把一次未捕获的异常写成人能读懂的一句
     * @param error 异常指针，空指针表示不是异常引起的（例如纯 abort）
     * @param logDirectory 日志目录，供用户当场去找现场
     * @return 中文文案，含原因位置与下一步
     * @details 异常类型未知时也要给一句能读的话：留空或只报 "unknown" 等于没记。
     */
    [[nodiscard]] QString crashMessageFor(const std::exception_ptr &error, std::string_view logDirectory);

    /**
     * @brief 装上 std::terminate 兜底
     * @details 兜底里先记日志再 std::abort()：只记日志不中止，进程会带着坏状态继续跑，
     *          之后写出来的东西比崩溃更难查；abort 才能让系统按常规留下崩溃转储。
     *          这条路径要靠真的崩一次才能验，因此本文件把「写文案」那一步留成纯函数单独测。
     */
    void installTerminateHandler();

    /**
     * @brief 崩溃时刻的年月日时分秒
     * @details 命名规则要有一处真源，而崩溃处理器里不能构造 Qt 的时间对象（会分配、会取锁），
     *          所以两边都把时间拆成这几个整数再交给 crashDumpFileName。
     */
    struct CrashStamp
    {
        int year   = 1970; ///< 四位年
        int month  = 1;    ///< 1 到 12
        int day    = 1;    ///< 1 到 31
        int hour   = 0;    ///< 0 到 23
        int minute = 0;    ///< 0 到 59
        int second = 0;    ///< 0 到 59
    };

    /**
     * @brief 崩溃转储的文件名（命名规则的唯一出处）
     * @param applicationVersion 版本号，与 --version 报的是同一份
     * @param stamp 崩溃时刻
     * @param processId 进程号，同秒内崩两次也要分得开
     * @return 形如 GyanisScribe_0.0.1_20261010-191504_4712.dmp 的宽字符文件名
     * @details 只吃整数与窄串、不分配堆：崩溃处理器里也要调它；用例侧写死期望值核对形状。
     */
    [[nodiscard]] std::wstring crashDumpFileName(std::wstring_view applicationVersion, const CrashStamp &stamp, std::uint32_t processId);

    /**
     * @brief 装上「崩了就往这个目录写 minidump」的兜底
     * @param dumpDirectory 已存在或即将由本函数创建的目录
     * @param applicationVersion 版本号，进文件名
     * @details Windows 用 SetUnhandledExceptionFilter + MiniDumpWriteDump；处理器里只做三件事：
     *          开文件、写转储、按 kCrashExitCode 结束进程——不写日志（日志的锁可能正被崩掉的那条线程
     *          持有），也不走 Qt 类型。
     * @note 会覆盖先前装好的异常过滤器：本程序只有这一处装，链到上游过滤器留给系统自己处理崩溃。
     */
    void installCrashDumpWriter(const std::filesystem::path &dumpDirectory, std::wstring_view applicationVersion);

} // namespace GyanisScribe::App
