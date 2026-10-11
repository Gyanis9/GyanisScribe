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
     * @details 存几何、外观与「像素在哪儿」，不存像素本身：把图片塞进配置会让设置文件涨到
     *          不可维护，而重启后要恢复的是那几张还摆在桌上的图。像素由管理器写进缓存目录，
     *          布局只记它的路径。
     */
    struct PinState
    {
        QPoint  position;              ///< 窗口左上角的全局逻辑坐标
        QSizeF  logicalSize;           ///< 图片未缩放时的逻辑尺寸
        double  scale           = 1.0; ///< 缩放因子
        double  rotationDegrees = 0.0; ///< 旋转角度，已折进 [0,360)
        double  opacity         = 1.0; ///< 不透明度，(0,1]
        QString imageFile;             ///< 像素在盘上的位置，恢复时按它读图
        int     group        = 1;      ///< 所属贴图分组，从 1 起；一张贴图只属于一组
        bool    alwaysOnTop  = true;   ///< 是否保持置顶；缺省按今天的行为（新建的贴图都置顶）
        bool    mouseThrough = false;  ///< 是否鼠标穿透
        QString sourceFile;            ///< 动图的来源文件；空表示这张是静图（剪贴板与标注贴出来的都在内存里）
    };

    /// 分组号的起点：布局里缺 group 字段（上一版存下的）与新建的贴图都落这一组
    inline constexpr int kFirstPinGroup = 1;

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

    /// 新建贴图的默认不透明度（百分比）：配置里没有或值不合法时用它，即完全不透明
    inline constexpr int kDefaultOpacityPercent = 100;

    /**
     * @brief 把配置里存的不透明度百分比换成界面用的 (0,1]
     * @param percent 配置读出的整数百分比
     * @return 可用的不透明度；不在 1..100 里时返回带范围与恢复办法的中文原因
     * @details 取 1 而不是 0 作下限：0% 的贴图在屏幕上是看不见的存在，
     *          用户会以为贴图功能坏了，而鼠标还会被它挡住。
     */
    [[nodiscard]] Core::Expected<double> parseOpacityPercentSetting(int percent);

} // namespace GyanisScribe::Pin
