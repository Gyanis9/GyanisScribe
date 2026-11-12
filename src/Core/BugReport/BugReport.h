/**
 * @file BugReport.h
 * @brief Bug 报告的环境、字段、打码状态与 Markdown 生成；纯逻辑，不依赖 Widgets
 * @author Gyanis
 * @date 2026-11-12
 * @version 0.0.1
 * @copyright Copyright (c) . All rights reserved.
 */

#pragma once

#include <QDateTime>
#include <QSize>
#include <QString>

#include <cstdint>
#include <filesystem>

namespace GyanisScribe::Core::BugReport
{
    /**
     * @brief 生成报告时随截图一起带上的环境信息
     * @details 屏幕、尺寸、倍比与截图时间都来自这张截图的来源，不现查当前屏幕；
     *          前台窗口与进程只来自截图开始时的来源，缺失时留空，由生成逻辑省略对应行。
     */
    struct BugReportEnvironment
    {
        QString   appName;                ///< 应用显示名
        QString   appVersion;             ///< 应用版本
        QString   osName;                 ///< 操作系统名称，取自 QSysInfo::prettyProductName()
        QString   osVersion;              ///< 操作系统内核版本，取自 QSysInfo::kernelVersion()
        QString   architecture;           ///< CPU 架构，取自 QSysInfo::currentCpuArchitecture()
        QString   qtVersion;              ///< Qt 版本
        QString   screenName;             ///< 截图来源的屏幕名；不现查当前屏幕
        QSize     logicalSize;            ///< 截图逻辑尺寸
        QSize     physicalSize;           ///< 截图物理像素尺寸
        qreal     devicePixelRatio = 1.0; ///< 截图时的缩放倍比
        QDateTime capturedAt;             ///< 这张截图的捕获时刻；无效时不写
        QString   windowTitle;            ///< 截图时的前台窗口标题
        QString   processName;            ///< 截图时的前台进程名
    };

    /// 用户填写的报告正文
    struct BugReportFields
    {
        QString title;    ///< 标题；空时用默认标题
        QString steps;    ///< 复现步骤
        QString expected; ///< 期望结果
        QString actual;   ///< 实际结果
        QString notes;    ///< 补充说明；空时整段省略
    };

    /// 报告里可选的两段信息
    struct BugReportOptions
    {
        bool includeSystem = true; ///< 是否包含「环境」段
        bool includeWindow = true; ///< 是否包含「窗口」段
    };

    /// 自动打码的检查状态
    enum class RedactionState
    {
        Pending,    ///< 检查还没回来
        Masked,     ///< 检出并已打码
        NoHits,     ///< 检查成功但没检出
        Unavailable ///< 后端不可用、检查失败或打码渲染失败
    };

    /// 写入报告的打码摘要；不含识别原文与 PII 类型
    struct BugReportRedaction
    {
        RedactionState state       = RedactionState::Pending; ///< 当前检查状态
        int            maskedCount = 0;                       ///< 实际打码的处数
        QString        toolLabel;                             ///< 打码方式名，例如「马赛克」或「高斯模糊」
        QString        unavailableReason;                     ///< 不可用时给用户看的中文原因
    };

    /// 由截图协调器组装、随本轮截图一起交给报告对话框的来源信息
    struct BugReportCaptureContext
    {
        QSize     logicalSize;            ///< 画布逻辑尺寸
        qreal     devicePixelRatio = 1.0; ///< 画布缩放倍比
        QString   screenName;             ///< 截图来源的屏幕名
        QDateTime capturedAt;             ///< 截图的捕获时刻
        QString   windowTitle;            ///< 截图时的前台窗口标题
        QString   processName;            ///< 截图时的前台进程名
    };

    /// 保存报告时用到的两个同目录文件路径
    struct BugReportPaths
    {
        std::filesystem::path markdown;   ///< 报告正文路径
        std::filesystem::path screenshot; ///< 截图路径
    };

    /**
     * @brief 生成整份 Bug 报告 Markdown
     * @param environment 环境与窗口信息；各字段有值才写
     * @param fields 用户填写的正文
     * @param redaction 打码状态，决定末尾的自动打码摘要
     * @param options 系统段与窗口段的开关
     * @param screenshotFileName 随报告保存的截图文件名；为空表示本次不带截图
     * @return LF 行尾、末尾恰好一个换行的 Markdown
     * @details 用户文本只做 CRLF 归一与首尾裁剪，不转义 Markdown；
     *          环境与窗口段没有可写内容时整段省略，不用占位符冒充。
     */
    [[nodiscard]] QString buildBugReportMarkdown(const BugReportEnvironment &environment, const BugReportFields &fields, const BugReportRedaction &redaction,
                                                 const BugReportOptions &options, const QString &screenshotFileName = {});

    /**
     * @brief 把时间格式化成带 UTC 偏移的本地时间
     * @param dateTime 要格式化的时间
     * @return 形如「2026-11-12 15:30:12 UTC+08:00」的文本；无效时间返回空串
     */
    [[nodiscard]] QString formatBugReportTime(const QDateTime &dateTime);

    /**
     * @brief 生成默认报告路径
     * @param directory 报告所在目录
     * @param now 用来生成时间戳的时刻
     * @return 形如「bug-report-20261112-153012.md」的路径；同目录同名 .md 或 .png 已存在时追加 -2、-3 直到都不冲突
     */
    [[nodiscard]] std::filesystem::path bugReportDefaultPath(const std::filesystem::path &directory, const QDateTime &now);

    /**
     * @brief 把用户在保存对话框里选中的路径换算成报告与截图两个同目录路径
     * @param chosenPath 用户选中的路径
     * @return 报告路径与截图路径；后缀不是 .md 或 .markdown 时替换成 .md，截图路径始终是 .png
     */
    [[nodiscard]] BugReportPaths bugReportPathsForDialog(const std::filesystem::path &chosenPath);

    /**
     * @brief 报告对话框的初始逻辑尺寸：夹进屏幕可用区域
     * @param availableArea 目标屏幕的可用区域尺寸，逻辑像素；无效时按空尺寸处理
     * @return 目标版面 1000×760；装不下时四周各留 48 像素边距，但不低于 640×480
     */
    [[nodiscard]] QSize bugReportDialogInitialSize(QSize availableArea);
} // namespace GyanisScribe::Core::BugReport
