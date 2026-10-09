/**
 * @file IOcrEngine.h
 * @brief 文字识别后端的契约
 * @author Gyanis
 * @date 2026-10-09
 * @version 0.0.1
 * @copyright Copyright (c) . All rights reserved.
 */

#pragma once

#include "Core/Types.h"
#include "Ocr/OcrTypes.h"

#include <QImage>
#include <QStringList>

namespace GyanisScribe::Ocr
{

    /**
     * @brief 识别后端
     * @details 同步接口：一次调用要跑几十到上千毫秒，绝不能放在界面线程上——
     *          放线程与把结果投递回界面是控制器那一批的事，后端本身只管认字。
     * @note 不可用的后端不算错误：recognize() 会返回 PlatformUnsupported，
     *       而 isAvailable() 让界面在用户点之前就把入口标灰并给原因。
     */
    class IOcrEngine
    {
    public:
        IOcrEngine()                              = default;
        virtual ~IOcrEngine()                     = default;
        IOcrEngine(const IOcrEngine &)            = delete;
        IOcrEngine &operator=(const IOcrEngine &) = delete;

        /**
         * @brief 后端显示名
         * @return 出现在设置页与结果标题里的名字，如「Windows 系统识别」
         */
        [[nodiscard]] virtual QString name() const = 0;

        /**
         * @brief 本后端此刻能不能用
         * @return true 表示可以调 recognize()
         * @note 判的是运行期条件（语言包、系统组件），不是「本平台有没有实现」——后者归平台能力表
         */
        [[nodiscard]] virtual bool isAvailable() const = 0;

        /**
         * @brief 不可用的原因
         * @return 中文「原因 + 替代做法」；可用时返回空串
         */
        [[nodiscard]] virtual QString unavailableReason() const = 0;

        /**
         * @brief 可选语言标签
         * @return 供设置页下拉的语言列表，按显示顺序
         */
        [[nodiscard]] virtual QStringList supportedLanguages() const = 0;

        /**
         * @brief 识别一张图
         * @param image 待识别图（物理像素），坐标系与返回的方框一致
         * @param language 语言标签，取值来自 supportedLanguages()
         * @return 成功返回结果（一行都没认出来也算成功，用 OcrResult::isEmpty() 判）
         * @return 失败返回原因：语言不支持、图太小、后端不可用都各自给中文说明
         */
        [[nodiscard]] virtual Core::Expected<OcrResult> recognize(const QImage &image, const QString &language) = 0;
    };

} // namespace GyanisScribe::Ocr
