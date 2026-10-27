/**
 * @file CommandLine.h
 * @brief 命令行启动参数的词表与解析（纯逻辑，不碰 QApplication）
 * @author Gyanis
 * @date 2026-10-12
 * @version 0.0.1
 * @copyright Copyright (c) . All rights reserved.
 */

#pragma once

#include "App/CaptureDelivery.h"
#include "Base/Types.h"

#include <QString>
#include <QStringList>

#include <array>
#include <string_view>

namespace GyanisScribe::App
{

    /**
     * @brief 命令行要求的启动动作
     * @details 帮助与版本也在表里：它们与别的动作共用同一条解析路，Main 按结果各自收场。
     */
    enum class CommandLineAction
    {
        None,              ///< 没带参数：正常启动，常驻
        Help,              ///< --help：打用法说明后退出
        Version,           ///< --version：打版本号后退出
        SelfTest,          ///< --selftest：把启动路径跑完就退出，退出码 0 / 1
        CaptureFullScreen, ///< --capture full：截整块屏，落盘后把路径打到标准输出并退出
        CaptureLastRegion, ///< --capture region：复用「上次选区」，其余同上
        CaptureWindow,     ///< --capture window：截当前前台窗口，其余同上
        PinFile,           ///< --pin <文件>：把盘上的图片贴到屏幕上（保持运行）
        ColorPick,         ///< --pick：进入屏幕取色（保持运行）
    };

    /**
     * @brief 解析好的启动请求
     */
    struct CommandLineRequest
    {
        CommandLineAction action = CommandLineAction::None; ///< 要走的动作
        QString           filePath;                         ///< --pin 要用到的文件路径；其余动作为空
        CaptureOutputSet  outputs;                          ///< --capture 这一轮点名的去处，已按交付顺序排好；没点名的动作留空
        QString           execCommand;                      ///< --exec 写的那一条命令；空表示这一轮截完不跑外部命令
        bool              outputsGiven = false;             ///< 命令行上到底写没写 --to：控制器要据此决定「这一次」压过设置里那一格
    };

    /// --capture 的取值词表：token、对应动作与用法说明里那句人话
    struct CommandLineCaptureChoice
    {
        std::string_view  token;       ///< 命令行上写的取值，区分大小写
        CommandLineAction action;      ///< 对应动作
        std::string_view  description; ///< 用法说明里那一句
    };

    /// 全部取值，顺序就是用法说明里的顺序
    inline constexpr std::array<CommandLineCaptureChoice, 3> kCommandLineCaptureChoices{
            CommandLineCaptureChoice{"full", CommandLineAction::CaptureFullScreen, "整块屏"},
            CommandLineCaptureChoice{"region", CommandLineAction::CaptureLastRegion, "复用「上次选区」（没有记过选区时当场报错）"},
            CommandLineCaptureChoice{"window", CommandLineAction::CaptureWindow, "当前前台窗口"},
    };

    /**
     * @brief 一次解析的结果：请求与报错
     * @details 报错时不返回部分结果：错一个参数就整条不执行，免得「以为生效了」——脚本里这种误解最贵。
     */
    struct CommandLineParseResult
    {
        CommandLineRequest request;   ///< 解析成功的动作；出错时为 None
        QString            errorText; ///< 中文原因；成功时为空
    };

    /**
     * @brief 解析启动参数
     * @param arguments 命令行参数（不含 argv[0]）
     * @return 请求或报错
     * @details 认这些：-h/--help、-v/--version、--selftest、--capture <full|region|window>、
     *          --pin <文件>、--pick，以及只配 --capture 用的 --to <去处> 与 --exec <命令>。
     *          取值可以写成「--capture=full」或「--capture full」两种，两种都适用于 --to 与 --exec。
     *          一次只认一个动作：同时给两个就报错，不替用户挑一个执行。
     */
    [[nodiscard]] CommandLineParseResult parseStartupArguments(const QStringList &arguments);

    /**
     * @brief 这个动作是不是「跑一件事就走」的一次性启动
     * @param action 解析出的启动动作
     * @return 自检、截图、贴图与取色为 true；常驻、帮助与版本为 false
     * @details 一次性启动不锁单实例、不去抢常驻实例的全局热键，脚本在托盘常驻时照样能用。
     *          自检也归这一档：它在已有实例时同样要把控制器建起来跑一遍启动路径，
     *          落在这一档之外时，非主实例那次启动不建任何东西，而退出码仍可能是 0。
     */
    [[nodiscard]] bool isTransientCommandLineAction(CommandLineAction action);

    /**
     * @brief 一次性截图之后是否还要进入事件循环
     * @param finished 交付结果（成功或失败）是否已经在进入循环之前送到
     * @param keepRunning 这一轮是否点了「贴到屏幕上」而需要留在桌面上
     * @param exitCode 已经定下的退出码
     * @return true 表示还有事要做：结果尚未送到，或交付成功且需要保活；false 表示可以直接返回退出码
     * @details quit() 在 exec() 之前什么也不做。同步失败时 finished 与 keepRunning 同时为真，
     *          只看这两个布尔就会进循环后一直等下去；失败且退出码非 0 时没有可等的东西，由调用方直接返回。
     */
    [[nodiscard]] bool shouldRunEventLoopAfterOneShot(bool finished, bool keepRunning, int exitCode);

    /// 用法说明全文（走文案目录），Main 在 --help 与解析报错时打印
    [[nodiscard]] QString commandLineUsageText();

} // namespace GyanisScribe::App
