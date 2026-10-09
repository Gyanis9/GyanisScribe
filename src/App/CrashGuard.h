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

#include <exception>
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

} // namespace GyanisScribe::App
