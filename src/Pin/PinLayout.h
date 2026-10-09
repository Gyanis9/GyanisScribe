/**
 * @file PinLayout.h
 * @brief 贴图布局的记录与存取
 * @author Gyanis
 * @date 2026-10-09
 * @version 0.0.1
 * @copyright Copyright (c) . All rights reserved.
 */

#pragma once

#include "Core/Types.h"

#include <QPoint>
#include <QSizeF>
#include <QString>
#include <QVector>

#include <string_view>

namespace GyanisScribe::Settings
{
    class ISettingsStore;
} // namespace GyanisScribe::Settings

namespace GyanisScribe::Pin
{

    /**
     * @brief 一张贴图的布局记录
     * @details 只存几何与外观，不存图片内容：图片本身在需要时另存文件，
     *          把像素塞进配置会让设置文件涨到不可维护，而重启后要恢复的是「摆在哪、多大、多透」。
     */
    struct PinState
    {
        QPoint position;              ///< 窗口左上角的全局逻辑坐标
        QSizeF logicalSize;           ///< 图片未缩放时的逻辑尺寸
        double scale           = 1.0; ///< 缩放因子
        double rotationDegrees = 0.0; ///< 旋转角度，已折进 [0,360)
        double opacity         = 1.0; ///< 不透明度，(0,1]
    };

    /**
     * @brief 把一组布局序列化成 JSON 文本
     * @param states 全部贴图的状态，顺序即恢复顺序
     * @return 可直接写进配置的文本
     */
    [[nodiscard]] QString serializeLayout(const QVector<PinState> &states);

    /**
     * @brief 解析布局文本
     * @param text 序列化那一侧产出的文本
     * @return 成功返回状态列表（空列表也算成功）；形状不对时返回 InvalidArgument 并点名坏在哪一项
     * @note 单个对象里的字段缺失或取值非法会让整次解析失败，而不是跳过那一项：
     *       悄悄少恢复一张贴图，用户会以为它被删了
     */
    [[nodiscard]] Core::Expected<QVector<PinState>> parseLayout(std::string_view text);

    /**
     * @brief 把布局写进配置
     * @param settings 目标配置
     * @param states 全部贴图的状态
     * @return 成功返回空载荷；配置写入失败时带回原因
     */
    [[nodiscard]] Core::Expected<void> saveLayout(Settings::ISettingsStore &settings, const QVector<PinState> &states);

    /**
     * @brief 从配置读回布局
     * @param settings 来源配置
     * @return 成功返回状态列表；没存过返回空列表；存的文本坏掉返回 InvalidArgument
     */
    [[nodiscard]] Core::Expected<QVector<PinState>> loadLayout(const Settings::ISettingsStore &settings);

} // namespace GyanisScribe::Pin
