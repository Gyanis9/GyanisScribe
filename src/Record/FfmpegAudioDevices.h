/**
 * @file FfmpegAudioDevices.h
 * @brief Windows 下经 dshow 暴露的音频输入设备：清单解析与异步枚举
 * @author Gyanis
 * @date 2026-10-10
 * @version 0.0.1
 * @copyright Copyright (c) . All rights reserved.
 */

#pragma once

#include "Core/Types.h"

#include <QObject>
#include <QString>
#include <QVector>

#include <memory>

class QProcess;

namespace GyanisScribe::Record
{

    /**
     * @brief 一路可取的音频输入
     * @details 两个名字都要留着：友好名给人看，Alternative name 给取流那一步用。
     *          后者形如 @device_cm_{GUID}\wave_{GUID}，不随系统显示语言变，
     *          而前者在中文系统上是「麦克风 (Realtek(R) Audio)」、英文系统上是「Microphone (...)」。
     */
    struct AudioInputDevice
    {
        QString friendlyName;    ///< 设备给人看的名字，界面列这一列
        QString alternativeName; ///< dshow 的稳定标识；空表示这台机器的 ffmpeg 没给
    };

    /**
     * @brief 从 ffmpeg 的设备清单输出里解析音频输入
     * @param ffmpegOutput 那一步的合并输出（stderr 为主）
     * @return 按输出顺序排列的音频设备；解析不出就返回空列表
     * @details 认的形状是 `[dshow @ 0x…] "名称" (audio)`，紧跟的
     *          `[dshow @ 0x…]   Alternative name "@device_cm_…"` 归上一台设备。
     *          视频段那几行不带 (audio)，天然被排除；标题行的有无随 ffmpeg 版本变，所以不依赖它。
     */
    [[nodiscard]] QVector<AudioInputDevice> parseDshowAudioDevices(const QString &ffmpegOutput);

    /**
     * @brief 把 ffmpeg 的清单输出交回成「一组设备」或「一句失败」
     * @param ffmpegOutput 那一步的合并输出
     * @return 成功返回设备表；一行都没认出来时返回 InternalError 并带上输出末段
     * @details 一张空表在这里不算「这台机器没有音频设备」：分不清是真相还是解析跟不上 ffmpeg 的新格式，
     *          静默交回空表就等于让界面显示「没有设备」而误导用户。
     */
    [[nodiscard]] Core::Expected<QVector<AudioInputDevice>> audioDevicesFromListing(const QString &ffmpegOutput);

    /**
     * @brief 异步问 ffmpeg 要一份音频输入清单
     * @details 起进程这件事不能压在主线程上：需求第十一条要求耗时操作离开界面线程，
     *          而这里用 QProcess 的信号回调就够了，不引入新线程，也就没有跨线程数据要护。
     */
    class FfmpegAudioDeviceEnumerator : public QObject
    {
        Q_OBJECT

    public:
        /**
         * @brief 建枚举器
         * @param parent QObject 父对象，默认无
         */
        explicit FfmpegAudioDeviceEnumerator(QObject *parent = nullptr);

        /**
         * @brief 析构：收掉还在跑的 ffmpeg
         */
        ~FfmpegAudioDeviceEnumerator() override;

        FfmpegAudioDeviceEnumerator(const FfmpegAudioDeviceEnumerator &)            = delete;
        FfmpegAudioDeviceEnumerator &operator=(const FfmpegAudioDeviceEnumerator &) = delete;

        /**
         * @brief 发起一次枚举，结果走 updated 或 failed
         * @details 上一次还没回来时重复调用会被忽略：同时挂两个 ffmpeg 进程只会让两份清单互相盖。
         */
        void refresh();

    signals:
        /**
         * @brief 清单拿到了
         * @param devices 音频输入，可能为空（这台机器真的没有可取的设备）
         */
        void updated(const QVector<GyanisScribe::Record::AudioInputDevice> &devices);

        /**
         * @brief 这一档拿不到清单
         * @param error 带中文原因：平台不支持、没装 ffmpeg，或进程起不动
         */
        void failed(const GyanisScribe::Core::Error &error);

    private:
        /// 进程收尾：按输出解析，退出码在这里不作判据
        void handleFinished();

        std::unique_ptr<QProcess> m_process;      ///< 正在跑的 ffmpeg；空表示没在查
        bool                      m_busy = false; ///< 是否已有一次查询在飞
    };

} // namespace GyanisScribe::Record
