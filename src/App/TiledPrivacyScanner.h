/**
 * @file TiledPrivacyScanner.h
 * @brief 分带跑隐私扫描：长图切带逐带识别，收尾把合并结果与原图一起交回
 * @author Gyanis
 * @date 2026-11-12
 * @version 0.0.1
 * @copyright Copyright (c) . All rights reserved.
 */

#pragma once

#include "Base/Types.h"
#include "Core/Ocr/OcrTypes.h"

#include <QImage>
#include <QObject>
#include <QRect>

#include <cstddef>
#include <memory>
#include <vector>

namespace GyanisScribe::Core
{
    class IOcrEngine;
    class ISettingsStore;
} // namespace GyanisScribe::Core

namespace GyanisScribe::Platform
{
    class IPlatformIntegration;
} // namespace GyanisScribe::Platform

namespace GyanisScribe::App
{
    class PrivacyCoordinator;

    /**
     * @brief 分带隐私扫描器
     * @details 识别后端对超高的图会整张拒绝，所以先切带；逐带驱动自带的协调器（单飞天然串行），
     *          每带结果按带原点平移并入，收尾用进来时那张图交回——下游用 cacheKey 认画布，换一张就会被判过期。
     *          自带的协调器不与画布那条共用：那条的 scanSucceeded 已直连截图协调器。
     */
    class TiledPrivacyScanner : public QObject
    {
        Q_OBJECT

    public:
        /**
         * @brief 生产构造
         * @param settings 配置来源
         * @param integration 平台能力来源，后端选型用
         * @param parent Qt 父对象
         */
        TiledPrivacyScanner(GyanisScribe::Core::ISettingsStore &settings, GyanisScribe::Platform::IPlatformIntegration &integration, QObject *parent = nullptr);

        /**
         * @brief 用例注入构造
         * @param settings 配置来源
         * @param engine 识别后端，所有权移入
         * @param parent Qt 父对象
         */
        TiledPrivacyScanner(GyanisScribe::Core::ISettingsStore &settings, std::unique_ptr<GyanisScribe::Core::IOcrEngine> engine, QObject *parent = nullptr);

        /**
         * @brief 析构
         * @note 声明在头、定义在 .cpp：协调器与识别后端只前向声明，内联析构会在别的 TU 里对不完整类型求 sizeof
         */
        ~TiledPrivacyScanner() override;

        /**
         * @brief 开始一次分带扫描
         * @param image 要扫的图（通常是画布底图），物理像素
         * @details 上一次还没收尾时先作废那一轮：新一轮的图与旧框不属于同一张画布。
         *          协调器是单飞的，这一带没回来之前再发请求会被它直接丢掉，所以新图排到那一带回来之后再开。
         */
        void start(const QImage &image);

        /**
         * @brief 现在有没有一次扫描在跑
         * @return true 表示还没收尾
         */
        [[nodiscard]] bool isScanning() const noexcept;

    signals:
        /**
         * @brief 整个图扫完了
         * @param image 进来时那张图，cacheKey 不变
         * @param result 合并后的结果，坐标是整图坐标
         */
        void scanSucceeded(const QImage &image, const GyanisScribe::Core::OcrResult &result);

        /**
         * @brief 扫描失败
         * @param error 后端给的故障类别与中文原因
         */
        void scanFailed(const GyanisScribe::Base::Error &error);

    private:
        /// 从头开一轮
        void beginScan(const QImage &image);

        /// 把下一带交出去；没有下一带就收尾
        void startNextTile();

        /// 一带的结果回来了：按 cacheKey 认领、合并、续下一带
        void handleTileSucceeded(const QImage &tile, const GyanisScribe::Core::OcrResult &result);

        /// 一带失败：整次失败并把排队的图顶上
        void handleTileFailed(const GyanisScribe::Base::Error &error);

        GyanisScribe::Core::ISettingsStore                    &m_settings;              ///< 配置来源，不拥有
        GyanisScribe::Platform::IPlatformIntegration          *m_integration = nullptr; ///< 平台能力来源，不拥有
        std::unique_ptr<GyanisScribe::Core::IOcrEngine>        m_injectedEngine;        ///< 用例注入的后端
        std::unique_ptr<GyanisScribe::App::PrivacyCoordinator> m_scanner;               ///< 懒建的分带协调器
        QImage                                                 m_image;                 ///< 这一轮要扫的图
        std::vector<QRect>                                     m_tiles;                 ///< 这一轮的带表
        std::size_t                                            m_nextTile = 0;          ///< 下一带的序号
        GyanisScribe::Core::OcrResult                          m_merged;                ///< 累计结果
        qint64                                                 m_inFlightKey = 0;       ///< 正在扫的那一带副本的 cacheKey：结果回来按它认领
        QImage                                                 m_pendingImage;          ///< 扫描途中又来了新图：作废旧一轮、记下它
        bool                                                   m_abandoned = false;     ///< 这一轮被新图作废：结果回来既不合也不续下一带
        bool                                                   m_scanning  = false;     ///< 是否在跑
    };

} // namespace GyanisScribe::App
