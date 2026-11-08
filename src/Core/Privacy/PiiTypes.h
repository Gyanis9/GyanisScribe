/**
 * @file PiiTypes.h
 * @brief 隐私打码检测的类型与命中项
 * @author Gyanis
 * @date 2026-11-09
 * @version 0.0.1
 * @copyright Copyright (c) . All rights reserved.
 */

#pragma once

#include <QRect>
#include <QString>

namespace GyanisScribe::Core::Privacy
{

    /**
     * @brief 可识别的敏感信息类型
     */
    enum class PiiKind
    {
        Phone,    ///< 中国大陆手机号
        Landline, ///< 固定电话
        IdCard,   ///< 18 位居民身份证
        BankCard, ///< 银行卡号
        Email,    ///< 电子邮箱
        Ip,       ///< IPv4 或 IPv6 地址
        Barcode   ///< 二维码或条形码
    };

    /**
     * @brief 一条敏感信息命中
     */
    struct PiiItem
    {
        PiiKind kind;         ///< 命中类型
        QRect   physicalBox;  ///< 原图物理像素坐标，已夹回图像范围
        QString maskedSample; ///< 脱敏样例，只在本次审核里使用
    };

} // namespace GyanisScribe::Core::Privacy
