/**
 * @file CaptureDelivery.h
 * @brief 一次截图的去处词表：命令行点名的几处目的地，与它们的交付先后
 * @author Gyanis
 * @date 2026-10-14
 * @version 0.0.1
 * @copyright Copyright (c) . All rights reserved.
 */

#pragma once

#include "Core/Types.h"

#include <QString>

#include <array>
#include <string_view>
#include <vector>

namespace GyanisScribe::App
{

    /**
     * @brief 一帧截图落到的一个目的地
     * @details 与「截完之后做什么」那一档不是一回事：那一档选的是走哪条流程（进标注窗、直接识别……），
     *          这里点的是「画面上这份像素最终留在哪儿」，一处以上同时成立。
     */
    enum class CaptureOutput
    {
        File,      ///< 存成文件：目录、格式与命名模板都照设置里那几格走
        Clipboard, ///< 交剪贴板：装饰按不带透明通道的那一套（垫白会让深色底上围一圈白框）
        Pin,       ///< 贴到屏幕上原那块位置：进程得留着才看得见这一张
    };

    /// 一个目的地：命令行上写的 token（词表里唯一出处，报错与用法说明都从这儿列可取值）
    struct CaptureOutputChoice
    {
        CaptureOutput    output; ///< 落哪一处
        std::string_view token;  ///< 命令行与配置里写的值
    };

    /**
     * @brief 全部取值，表序就是这一轮的交付先后
     * @details 先落盘再交剪贴板：盘上那份是别的目的地的前提（`%file%` 要用它），
     *          而贴到屏幕上要占一块窗，放在最后才不会出现「图贴出来了、文件却没写成」。
     */
    inline constexpr std::array<CaptureOutputChoice, 3> kCaptureOutputChoices{
            CaptureOutputChoice{CaptureOutput::File, "file"},
            CaptureOutputChoice{CaptureOutput::Clipboard, "clipboard"},
            CaptureOutputChoice{CaptureOutput::Pin, "pin"},
    };

    /// 点名的几处去处，已按表序去重排好
    using CaptureOutputSet = std::vector<CaptureOutput>;

    /**
     * @brief 命令行没点名时的默认去处
     * @return {file, clipboard}
     * @details 就是「快速保存」那两件事：命令行这条路上一次截图一直同时落这两处，改默认值会让旧脚本丢掉一半输出。
     */
    [[nodiscard]] CaptureOutputSet defaultCaptureOutputs();

    /**
     * @brief 解析逗号分隔的目的地清单
     * @param text 形如 `clipboard, pin`：段前后的空格忽略，取值本身区分大小写（与 --capture 那一档同一口径）
     * @return 按交付顺序排好的去处；空清单、词表外的词、空段（`file,`）与重复点名都返回中文原因
     * @details 重复不代劳去重：`clipboard,clipboard` 是手抖，报出来比按一处算更能让人发现命令写歪了。
     */
    [[nodiscard]] Core::Expected<CaptureOutputSet> parseCaptureOutputs(const QString &text);

    /**
     * @brief 这一轮点没点名某一处
     * @param outputs 点名的去处
     * @param output 要查的那一处
     * @return 在场返回真
     */
    [[nodiscard]] bool captureOutputsContain(const CaptureOutputSet &outputs, CaptureOutput output) noexcept;

    /**
     * @brief 把词表里全部 token 拼成「file / clipboard / pin」
     * @return 报错与用法说明里列可取值用的一句
     */
    [[nodiscard]] std::string captureOutputTokenList();

} // namespace GyanisScribe::App
