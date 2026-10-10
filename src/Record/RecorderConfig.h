/**
 * @file RecorderConfig.h
 * @brief 录屏参数与它们的校验规则
 * @author Gyanis
 * @date 2026-10-10
 * @version 0.0.1
 * @copyright Copyright (c) . All rights reserved.
 */

#pragma once

#include "Core/Types.h"

#include <QRect>
#include <QString>

#include <array>
#include <filesystem>
#include <string_view>

namespace GyanisScribe::Record
{

    /// 录什么：整屏、选区还是一个窗口
    enum class RecordMode
    {
        FullScreen, ///< 整块屏幕
        Region,     ///< 屏幕内的一个矩形
        Window,     ///< 一个平台窗口
    };

    /// 输出容器：MP4 走 H.264，GIF 走两遍调色板
    enum class OutputFormat
    {
        Mp4, ///< 适合长片段，体积小
        Gif, ///< 适合分享的小动画
    };

    /**
     * @brief 一轮录屏的全部参数
     * @details 坐标一律是屏幕内的逻辑坐标，物理像素由抓帧那一层乘 devicePixelRatio；
     *          这里不放屏幕对象，参数要能原样存进配置与历史。
     */
    struct RecorderConfig
    {
        RecordMode            mode                   = RecordMode::FullScreen; ///< 录什么
        OutputFormat          format                 = OutputFormat::Mp4;      ///< 存成什么
        int                   frameRate              = 15;                     ///< 目标帧率，逻辑每秒帧数
        int                   videoBitrateKbps       = 0;                      ///< 画面码率 kbps，0 表示按质量优先（CRF），只对 MP4 起作用
        int                   maximumDurationSeconds = 600;                    ///< 最长录多久，到点自动收
        QRect                 region;                                          ///< Region 模式的选区，屏幕内逻辑坐标
        QString               screenName;                                      ///< 录哪块屏幕，全部模式必填
        quintptr              windowHandle = 0;                                ///< Window 模式的平台窗口句柄（Qt 的 WId）
        std::filesystem::path outputDirectory;                                 ///< 成品文件落在哪个目录
        bool                  cursorHighlight = false;                         ///< 是否在帧上圈出光标位置
        QString               microphoneDevice;                                ///< 麦克风那一路的 dshow 标识，空表示不取这一路
        QString               systemSoundDevice;                               ///< 系统声音那一路的 dshow 标识，空表示不取这一路
    };

    /**
     * @brief 这一档输出容器带不带音轨
     * @param format 输出格式
     * @return true 表示取回来的声音有地方放；GIF 返回 false
     * @details GIF 容器根本没有音轨，起了取声的 ffmpeg 只会得到一份没人用的 wav。
     *          判据只写在这一处：设置页置灰那两格与 RecordingService 跳过取声用的必须是同一句话，
     *          两处不同解就会出现「界面能选而服务不认」或者反过来。
     */
    [[nodiscard]] constexpr bool outputFormatCarriesAudio(const OutputFormat format) noexcept
    {
        return format != OutputFormat::Gif;
    }

    /**
     * @brief 校验一轮录屏的参数
     * @param config 待校验的参数
     * @return 成功返回空载荷；任一项不成立返回 InvalidArgument 与中文原因
     * @details 全部规则在开始之前判，不边录边判：录到一半才发现帧率不合法，
     *          用户已经白等了十几秒，那比开始就拒绝更糟。
     */
    [[nodiscard]] Core::Expected<void> validateRecorderConfig(const RecorderConfig &config);

    /// MP4 允许的最高帧率：再高就是浪费，屏幕本身也只给到 60
    inline constexpr int kMaximumFrameRate = 60;

    /// GIF 允许的最高帧率：两遍调色板的耗时按帧数乘，且 24 帧以上肉眼已经看不出差别
    inline constexpr int kMaximumGifFrameRate = 24;

    /// 单次录屏的时长上限，秒：录屏最容易被忘在跑，超过两小时当场拒
    inline constexpr int kMaximumDurationSeconds = 7200;

    /// 默认帧率的缺省档：配置里没有、或存的值不合法时用它，与 RecorderConfig::frameRate 的初值同解
    inline constexpr int kDefaultFrameRate = 15;

    /// 画面码率的缺省档：0 表示不按码率管，交给 CRF，与 RecorderConfig::videoBitrateKbps 的初值同解
    inline constexpr int kDefaultVideoBitrateKbps = 0;

    /// 画面码率的上限，kbps：50 Mbps 已经远超屏幕内容的需要，再高就是把手改配置写成天文数字的那一类
    inline constexpr int kMaximumVideoBitrateKbps = 50000;

    /**
     * @brief 解析配置里存的画面码率
     * @param stored 配置读出的整数值，0 表示按质量优先
     * @return 可用的码率；不在 0..kMaximumVideoBitrateKbps 里时返回带范围与替代做法的中文原因
     * @details 0 是合法值而不是「没填」：它是这一档的名字（交给 CRF），读不出时才回落缺省档。
     */
    [[nodiscard]] Core::Expected<int> parseVideoBitrateSetting(int stored);

    /**
     * @brief 解析配置里存的默认帧率
     * @param stored 配置读出的整数值
     * @return 可用的帧率；不在 1..kMaximumFrameRate 里时返回带范围与替代做法的中文原因
     * @details 帧率的上限按输出格式分两档（GIF 更低），但那要等格式定了才知道；
     *          这里只按两档共用的上限判，格式那一层仍由 validateRecorderConfig 把关。
     */
    [[nodiscard]] Core::Expected<int> parseFrameRateSetting(int stored);

    /// 一档录制范围：存进配置的 token，与设置页显示的中文标签
    struct RecordScopeChoice
    {
        RecordMode       mode;  ///< 这一档对应的录法
        std::string_view token; ///< 配置里存的值，词表在这里唯一
        std::string_view label; ///< 设置页上的中文标签，界面按 UTF-8 取
    };

    /// 全部录制范围档位，顺序就是设置页下拉的顺序
    inline constexpr std::array<RecordScopeChoice, 3> kRecordScopeChoices{
            RecordScopeChoice{RecordMode::FullScreen, "FullScreen", "整屏"},
            RecordScopeChoice{RecordMode::Region, "Region", "上次选区"},
            RecordScopeChoice{RecordMode::Window, "Window", "当前窗口"},
    };

    /**
     * @brief 取一档录制范围存在配置里的那个值
     * @param mode 目标档位
     * @return token；词表外的枚举值返回空，由调用方决定怎么报
     */
    [[nodiscard]] std::string_view recordScopeText(RecordMode mode) noexcept;

    /**
     * @brief 解析配置里存的录制范围
     * @param stored 配置读出的文本
     * @return 对应档位；读不出时返回 InvalidArgument 并列出可取的几个值
     * @details 不替用户猜一档：手改配置写成 "region"（小写）就该报出来，
     *          悄悄按整屏开始录会录到完全不同的画面。
     */
    [[nodiscard]] Core::Expected<RecordMode> parseRecordScope(const QString &stored);

    /// 一档输出格式：存进配置的 token，与设置页显示的中文标签
    struct RecordFormatChoice
    {
        OutputFormat     format; ///< 这一档存成什么容器
        std::string_view token;  ///< 配置里存的值，词表在这里唯一
        std::string_view label;  ///< 设置页上的中文标签，界面按 UTF-8 取
    };

    /// 全部输出格式档位，顺序就是设置页下拉的顺序
    inline constexpr std::array<RecordFormatChoice, 2> kRecordFormatChoices{
            RecordFormatChoice{OutputFormat::Mp4, "Mp4", "MP4 视频"},
            RecordFormatChoice{OutputFormat::Gif, "Gif", "GIF 动图"},
    };

    /**
     * @brief 取一档输出格式存在配置里的那个值
     * @param format 目标档位
     * @return token；词表外的枚举值返回空，由调用方决定怎么报
     */
    [[nodiscard]] std::string_view recordFormatText(OutputFormat format) noexcept;

    /**
     * @brief 解析配置里存的输出格式
     * @param stored 配置读出的文本
     * @return 对应档位；读不出时返回 InvalidArgument 并列出可取的几个值
     * @details 格式定不下来就不能开录：MP4 与 GIF 走的是两条编码路线（后者两遍调色板），
     *          猜错的那一种会把已经抓完的帧按错的容器压出去。
     */
    [[nodiscard]] Core::Expected<OutputFormat> parseRecordFormat(const QString &stored);

} // namespace GyanisScribe::Record
