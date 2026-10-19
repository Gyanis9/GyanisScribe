/**
 * @file WinRTImageOcrEngine.h
 * @brief Windows 系统自带的文字识别后端
 * @author Gyanis
 * @date 2026-10-10
 * @version 0.0.1
 * @copyright Copyright (c) . All rights reserved.
 */

#pragma once

#include "Base/Types.h"
#include "Core/Ocr/IOcrEngine.h"

#include <QHash>
#include <QString>
#include <QStringList>

namespace GyanisScribe::Plugins
{
    /**
     * @brief 用系统组件 Windows.Media.Ocr 识别
     * @details 只在 Windows + MSVC 下编入（宏 GYANISSCRIBE_HAS_WINRT_OCR），构造函数就会去问系统
     *          装了哪些识别语言。头文件刻意不含 C++/WinRT 投影——那一份只由实现文件吃，
     *          消费者与容器那一档都不会因此看到 Windows 头文件。
     * @note 系统后端不给逐词置信度，因此结果的 averageConfidence 为空；
     *       界面那一格随之显示「未提供」，导出 JSON 里写 null——打一个 0% 会被读成「这张图认得很差」。
     */
    class WinRTImageOcrEngine final : public Core::IOcrEngine
    {
    public:
        /**
         * @brief 问系统要一遍可用的识别语言
         * @details 这一步会初始化 COM 套间并枚举语言，因此放在一次性线程上做；
         *          枚举失败（系统组件缺失）不算异常，只是这份表为空、isAvailable() 随之为假。
         */
        WinRTImageOcrEngine();

        /**
         * @brief 析构
         * @details 重写 Core::IOcrEngine 的虚析构，保持编译器默认实现：本类只有一份语言标签对照表，
         *          随成员自己析构，没有需要显式归还的系统句柄。
         */
        ~WinRTImageOcrEngine() override = default;

        /**
         * @brief 后端显示名
         * @return 「Windows 系统识别」
         * @note 这个名字要翻：它按界面文案目录取词，用例与日志据此辨认工厂选中了哪一个后端
         */
        [[nodiscard]] QString name() const override;

        /**
         * @brief 系统里有没有装本程序认识的识别语言
         * @return true 表示可以调 recognize()
         */
        [[nodiscard]] bool isAvailable() const override;

        /**
         * @brief 不可用的原因
         * @return 中文「原因 + 替代做法」；可用时返回空串
         */
        [[nodiscard]] QString unavailableReason() const override;

        /**
         * @brief 本程序对外语言标签里系统支持的那几项
         * @return 按 Core::supportedOcrLanguageTags() 的顺序，只留系统装了语言包的
         */
        [[nodiscard]] QStringList supportedLanguages() const override;

        /**
         * @brief 识别一张图
         * @param image 待识别图（物理像素），返回的方框就在这一张图的坐标系里
         * @param language 对外语言标签，取值来自 supportedLanguages()；空串按默认语言
         * @return 成功返回结果（一行都没认出来也算成功）
         * @return 失败返回原因：图太小、语言没装、图超过系统上限、系统识别报错
         * @note 超过系统尺寸上限直接报错而不是缩小：缩小会让方框坐标与原图对不上，
         *       而界面上「点文字定位」靠的就是这一套坐标
         */
        [[nodiscard]] Base::Expected<Core::OcrResult> recognize(const QImage &image, const QString &language) override;

    private:
        QHash<QString, QString> m_systemTagByPublicTag; ///< 对外语言标签 → 系统语言标签，只收系统装了的那几项
    };

} // namespace GyanisScribe::Plugins
