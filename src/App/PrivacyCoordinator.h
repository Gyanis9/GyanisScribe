/**
 * @file PrivacyCoordinator.h
 * @brief 一张图的本地隐私扫描：选后端、单飞、把结果交回控制器
 * @author Gyanis
 * @date 2026-11-09
 * @version 0.0.1
 * @copyright Copyright (c) . All rights reserved.
 */

#pragma once

#include "Base/Types.h"
#include "Core/Ocr/OcrTypes.h"
#include "Gui/Controls/Toast.h"

#include <QImage>
#include <QObject>
#include <QString>

#include <memory>

namespace GyanisScribe::Core
{
    class IOcrEngine;
    class ISettingsStore;
    class RecognitionRunner;
} // namespace GyanisScribe::Core

namespace GyanisScribe::Platform
{
    class IPlatformIntegration;
} // namespace GyanisScribe::Platform

namespace GyanisScribe::App
{

    /**
     * @brief 隐私扫描这一条链的协作对象
     * @details 只做「一张图的敏感信息检查」：不打开识别结果窗，不保存也不记录识别文本。
     *          要出到界面上的话走 messageForUser 交回控制器，与识别链的窗口族同一口径。
     */
    class PrivacyCoordinator : public QObject
    {
        Q_OBJECT

    public:
        /**
         * @brief 生产构造：按设置与平台能力选一个识别后端
         * @param settings 配置来源，读 Ocr/Language
         * @param integration 平台能力来源，后端选型用
         * @param parent Qt 父对象
         */
        PrivacyCoordinator(GyanisScribe::Core::ISettingsStore &settings, GyanisScribe::Platform::IPlatformIntegration &integration, QObject *parent = nullptr);

        /**
         * @brief 测试注入构造：跳过后端工厂，直接接管一个引擎
         * @param settings 配置来源，读 Ocr/Language
         * @param engine 识别后端，所有权移入本对象
         * @param parent Qt 父对象
         */
        PrivacyCoordinator(GyanisScribe::Core::ISettingsStore &settings, std::unique_ptr<GyanisScribe::Core::IOcrEngine> engine, QObject *parent = nullptr);

        /**
         * @brief 析构
         * @note 声明在头、定义在 .cpp：引擎与搬运工只前向声明，内联析构会在别的 TU 里对不完整类型求 sizeof
         */
        ~PrivacyCoordinator() override;

        /**
         * @brief 检查一张图里有没有可打码的敏感信息
         * @param image 待检查的图，物理像素
         * @details 单飞：上一次检查没回来时只提示「正在检查」，不把第二张排进队列。
         *          识别语言取 Ocr/Language，缺省为 kDefaultOcrLanguage。
         */
        void requestScan(const QImage &image);

        /**
         * @brief 当前有没有一次检查在跑
         * @return true 表示已交给搬运工且结果还没回来
         */
        [[nodiscard]] bool isScanning() const noexcept;

    signals:
        /**
         * @brief 一次检查成功
         * @param image 发出请求时那一张，结果里的方框与它同系
         * @param result 识别结果，一行都没认出来也走这里
         */
        void scanSucceeded(const QImage &image, const GyanisScribe::Core::OcrResult &result);

        /**
         * @brief 一次检查没能开始或没能完成
         * @param error 故障类别与中文可操作文案
         */
        void scanFailed(const GyanisScribe::Base::Error &error);

        /**
         * @brief 要说给用户的一句话
         * @param message 中文提示
         * @param kind 提示种类
         */
        void messageForUser(const QString &message, GyanisScribe::Gui::ToastKind kind);

    private:
        /**
         * @brief 接上搬运工的两个结果信号
         * @details 引擎为空时什么都不接，后续 requestScan 会按「后端不可用」给中文原因。
         */
        void initializeRunner();

        /**
         * @brief 搬运工认完了：清掉单飞标记，把图与结果原样交出去
         * @param image 请求时那一张
         * @param language 本次识别的语言；隐私扫描不记住它，也不写设置
         * @param result 识别结果
         */
        void handleSucceeded(const QImage &image, const QString &language, const GyanisScribe::Core::OcrResult &result);

        /**
         * @brief 搬运工失败了：清掉单飞标记，把原因报给用户
         * @param error 后端给的故障类别与文案
         */
        void handleFailed(const GyanisScribe::Base::Error &error);

        GyanisScribe::Core::ISettingsStore                    &m_settings;         ///< 语言来源，不拥有
        std::unique_ptr<GyanisScribe::Core::IOcrEngine>        m_engine;           ///< 识别后端，按设置与平台能力选一次
        std::unique_ptr<GyanisScribe::Core::RecognitionRunner> m_runner;           ///< 线程搬运工；它拿着引擎的引用，必须排在 m_engine 之后析构
        bool                                                   m_scanning = false; ///< 单飞标记，只在界面线程上读写
    };

} // namespace GyanisScribe::App
