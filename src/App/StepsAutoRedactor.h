/**
 * @file StepsAutoRedactor.h
 * @brief 批量扫步骤截图并覆写命中的那些
 * @author Gyanis
 * @date 2026-11-12
 * @version 0.0.1
 * @copyright Copyright (c) . All rights reserved.
 */

#pragma once

#include <QObject>

#include <cstddef>
#include <filesystem>
#include <functional>
#include <memory>
#include <vector>

class QImage;

namespace GyanisScribe::Core
{
    class IOcrEngine;
    class ISettingsStore;
    struct OcrResult;
} // namespace GyanisScribe::Core

namespace GyanisScribe::Platform
{
    class IPlatformIntegration;
} // namespace GyanisScribe::Platform

namespace GyanisScribe::App
{
    class PrivacyCoordinator;

    /**
     * @brief 步骤截图的批量打码器
     * @details 逐张识别、命中即用马赛克覆写原文件；没命中的不动，读不出来或写不进去的记失败并保留原图。
     *          自带的协调器不与画布那条共用，扫描期间它只服务这一批。
     */
    class StepsAutoRedactor : public QObject
    {
        Q_OBJECT

    public:
        /**
         * @brief 生产构造
         * @param settings 配置来源
         * @param integration 平台能力来源，后端选型用
         * @param parent Qt 父对象
         */
        StepsAutoRedactor(GyanisScribe::Core::ISettingsStore &settings, GyanisScribe::Platform::IPlatformIntegration &integration, QObject *parent = nullptr);

        /**
         * @brief 用例注入构造
         * @param settings 配置来源
         * @param engine 识别后端，所有权移入
         * @param parent Qt 父对象
         */
        StepsAutoRedactor(GyanisScribe::Core::ISettingsStore &settings, std::unique_ptr<GyanisScribe::Core::IOcrEngine> engine, QObject *parent = nullptr);

        /**
         * @brief 析构
         * @note 声明在头、定义在 .cpp：协调器与识别后端只前向声明，内联析构会在别的 TU 里对不完整类型求 sizeof
         */
        ~StepsAutoRedactor() override;

        /**
         * @brief 跑一批
         * @param images 要处理的文件路径
         * @param done 全部结束后的回调：扫了几张、打了几张、失败几张
         * @details 回调可能同步触发（一张都没有、第一张就读不出来，或上一批还没跑完）也可能延后；
         *          调用方不要在回调里销毁本对象。上一批没跑完时这一批不排队：当场收尾，
         *          回调里的读数属于正在跑的那一批。
         */
        void run(std::vector<std::filesystem::path> images, std::function<void(int scanned, int redacted, int failed)> done);

        /// 现在有没有一批在跑
        [[nodiscard]] bool isRunning() const noexcept;

    private:
        /// 处理下一张；没有下一张就收尾
        void startNext();

        /// 一张的结果回来了：认码、打码、覆写，再走下一张
        void handleOne(const QImage &image, const GyanisScribe::Core::OcrResult &result);

        GyanisScribe::Core::ISettingsStore                    &m_settings;              ///< 配置来源，不拥有
        GyanisScribe::Platform::IPlatformIntegration          *m_integration = nullptr; ///< 平台能力来源，不拥有
        std::unique_ptr<GyanisScribe::Core::IOcrEngine>        m_injectedEngine;        ///< 用例注入的后端
        std::unique_ptr<GyanisScribe::App::PrivacyCoordinator> m_scanner;               ///< 懒建的协调器
        std::vector<std::filesystem::path>                     m_images;                ///< 这一批的路径
        std::size_t                                            m_next     = 0;          ///< 下一张的序号
        int                                                    m_redacted = 0;          ///< 打了几张
        int                                                    m_failed   = 0;          ///< 失败几张
        std::function<void(int, int, int)>                     m_done;                  ///< 收尾回调
        qint64                                                 m_inFlightKey = 0;       ///< 正在识别的那张图的 cacheKey：结果回来按它认领
        bool                                                   m_running     = false;   ///< 是否在跑
    };

} // namespace GyanisScribe::App
