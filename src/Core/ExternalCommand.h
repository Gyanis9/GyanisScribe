/**
 * @file ExternalCommand.h
 * @brief 截图成功后交给外部程序的那一条命令行：拆分、占位符替换与起跑（判据与起进程分开）
 * @author Gyanis
 * @date 2026-10-14
 * @version 0.0.1
 * @copyright Copyright (c) . All rights reserved.
 */

#pragma once

#include "Core/Types.h"

#include <QString>
#include <QStringList>

#include <string_view>

namespace GyanisScribe::Core
{

    /// 成品路径的占位符：命令里写这一串，起跑前换成这一轮落盘的那份文件；没落盘时带它的命令会被拒
    inline constexpr std::string_view kExternalCommandFilePlaceholder = "%file%";

    /**
     * @brief 一条外部命令拆好的样子：可执行体与参数
     */
    struct ExternalCommandPlan
    {
        QString     program;   ///< 要起的可执行体
        QStringList arguments; ///< 参数，`%file%` 已换成成品路径
    };

    /**
     * @brief 把一条命令行拆成可执行体与参数，并把参数里的 `%file%` 换成成品路径
     * @param commandLine 设置页或命令行上写的那一串，形如「上传工具 --path "%file%"」
     * @param filePath 这一轮落盘的成品路径；空串表示这一轮没有落成文件
     * @return 拆好的计划；命令行空、占位符落在程序名上、或命令要 `%file%` 而这一轮没落盘时返回中文原因
     * @details 按 shell 的引号规则自己拆，不交给 cmd.exe /c：交给 shell 就是把引号与元字符的解释权送出去，
     *          而路径带空格时 shell 那一侧的转义写法更难对。替换按整串逐个参数做，
     *          所以 `%file%` 与它前面的文字可以连写（`--path=%file%` 这种也认）。
     */
    [[nodiscard]] Core::Expected<ExternalCommandPlan> planExternalCommand(const QString &commandLine, const QString &filePath);

    /**
     * @brief 起跑一份拆好的计划：不等它结束
     * @param plan planExternalCommand 交回的那一份
     * @return 成功返回空载荷；起不来时返回中文原因，文案里点名是哪一个程序
     * @details 起进程这一步没有「拆错了」这一类可判的中间态：能 startDetached 就算交出去了，
     *          外部程序自己跑得怎样不由这一层负责（它多半是个上传脚本，等它就等于卡住截图这条路）。
     */
    [[nodiscard]] Core::Expected<void> startExternalCommand(const ExternalCommandPlan &plan);

} // namespace GyanisScribe::Core
