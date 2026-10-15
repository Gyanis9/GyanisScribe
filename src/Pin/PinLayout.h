/**
 * @file PinLayout.h
 * @brief 贴图布局的记录与存取
 * @author Gyanis
 * @date 2026-10-09
 * @version 0.0.1
 * @copyright Copyright (c) . All rights reserved.
 */

#pragma once

#include "Base/Types.h"
#include "Pin/PinTransform.h"

#include <QMap>
#include <QPoint>
#include <QSizeF>
#include <QString>
#include <QVector>

#include <string_view>

namespace GyanisScribe::Core
{
    class ISettingsStore;
} // namespace GyanisScribe::Core

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
        QPoint              position;                                             ///< 窗口左上角的全局逻辑坐标
        QSizeF              logicalSize;                                          ///< 图片未缩放时的逻辑尺寸
        double              scale           = 1.0;                                ///< 缩放因子
        double              rotationDegrees = 0.0;                                ///< 旋转角度，已折进 [0,360)
        double              opacity         = 1.0;                                ///< 不透明度，(0,1]
        QString             imageFile;                                            ///< 像素在盘上的位置，恢复时按它读图
        int                 group        = 1;                                     ///< 所属贴图分组，从 1 起；一张贴图只属于一组
        bool                alwaysOnTop  = true;                                  ///< 是否保持置顶；缺省按今天的行为（新建的贴图都置顶）
        bool                mouseThrough = false;                                 ///< 是否鼠标穿透
        QString             sourceFile;                                           ///< 动图的来源文件；空表示这张是静图（剪贴板与标注贴出来的都在内存里）
        QString             colorCardHex;                                         ///< 这张是色卡时它原本那个色（`#AARRGGBB`）；空表示不是色卡，右键「改这一张的颜色…」据此出现
        TransparentBackdrop transparentBackdrop = TransparentBackdrop::ThemeBase; ///< 透明像素底下画哪一档，与右键「透明区」同解
    };

    /// 分组号的起点：布局里缺 group 字段（上一版存下的）与新建的贴图都落这一组
    inline constexpr int kFirstPinGroup = 1;

    /// 「窗口阴影」的留边，逻辑像素：开着时窗口比内容每边大这一圈，投影画在这条带子里
    inline constexpr int kShadowMargin = 8;

    /// 阴影的层数：几圈递减的描边叠出「虚」的感觉，比真高斯便宜得多
    inline constexpr int kShadowLayers = 5;

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
    [[nodiscard]] Base::Expected<QVector<PinState>> parseLayout(std::string_view text);

    /**
     * @brief 把布局写进配置
     * @param settings 目标配置
     * @param states 全部贴图的状态
     * @return 成功返回空载荷；配置写入失败时带回原因
     */
    [[nodiscard]] Base::Expected<void> saveLayout(Core::ISettingsStore &settings, const QVector<PinState> &states);

    /**
     * @brief 从配置读回布局
     * @param settings 来源配置
     * @return 成功返回状态列表；没存过返回空列表；存的文本坏掉返回 InvalidArgument
     */
    [[nodiscard]] Base::Expected<QVector<PinState>> loadLayout(const Core::ISettingsStore &settings);

    /**
     * @brief 把分组名字表序列化成 JSON 对象文本（键是组号，值是名字）
     * @param names 组号 → 名字；空表写成 "{}"
     * @return 可直接写进配置的文本
     */
    [[nodiscard]] QString serializeGroupNames(const QMap<int, QString> &names);

    /**
     * @brief 解析分组名字表
     * @param text serializeGroupNames 产出的文本
     * @return 成功返回名字表（没存过就是空表）；形状不对返回 InvalidArgument 并写明怎么补救
     * @note 值是空串的那一条当作「没起过名字」丢掉：界面本来就按「分组 N」显示它
     */
    [[nodiscard]] Base::Expected<QMap<int, QString>> parseGroupNames(std::string_view text);

    /**
     * @brief 把分组名字写进配置
     * @param settings 目标配置
     * @param names 组号 → 名字
     * @return 成功返回空载荷；配置写入失败时带回原因
     */
    [[nodiscard]] Base::Expected<void> saveGroupNames(Core::ISettingsStore &settings, const QMap<int, QString> &names);

    /**
     * @brief 从配置读回分组名字
     * @param settings 来源配置
     * @return 成功返回名字表；没存过是空表；存的文本坏掉返回 InvalidArgument
     * @note 与 loadLayout 同一条口径：坏记录要报出来，不悄悄当「都没起过名字」——那一桌的标签会集体消失
     */
    [[nodiscard]] Base::Expected<QMap<int, QString>> loadGroupNames(const Core::ISettingsStore &settings);

    /**
     * @brief 读配置里那份分组轮换顺序
     * @param stored 十进制组号，顺序就是切组走的那一圈；空表表示「按组号升序」
     * @return 清掉空白项之后的组号表；出现非数字、非正数或重复组号时返回 InvalidArgument 并写明坏的是哪一格
     * @note 重复组号要拒而不是后写的盖掉先写的：那一格在圈里出现两次，切组就会在同一个地方多走一步，
     *       而界面上只看得到「顺序不对」，说不清是哪一条填歪了。
     */
    [[nodiscard]] Base::Expected<QVector<int>> parseGroupRotation(const QStringList &stored);

    /**
     * @brief 算出真正走一圈的组序
     * @param order parseGroupRotation 的产出（用户定的顺序）
     * @param groupsInUse 桌上现在有哪些组（升序）
     * @return 先按用户顺序列出在册的那些，再把没被点名的组按升序补在末尾
     * @note 没点名的组必须补上：新建一组之后切组切不到它，比顺序没排好更糟——那一组在键盘上成了死角。
     */
    [[nodiscard]] QVector<int> rotationCycle(const QVector<int> &order, const QVector<int> &groupsInUse);

    /// 新建贴图的默认不透明度（百分比）：配置里没有或值不合法时用它，即完全不透明
    inline constexpr int kDefaultOpacityPercent = 100;

    /**
     * @brief 把配置里存的不透明度百分比换成界面用的 (0,1]
     * @param percent 配置读出的整数百分比
     * @return 可用的不透明度；不在 1..100 里时返回带范围与恢复办法的中文原因
     * @details 取 1 而不是 0 作下限：0% 的贴图在屏幕上是看不见的存在，
     *          用户会以为贴图功能坏了，而鼠标还会被它挡住。
     */
    [[nodiscard]] Base::Expected<double> parseOpacityPercentSetting(int percent);

} // namespace GyanisScribe::Pin
