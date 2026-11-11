/**
 * @file StepTypes.h
 * @brief 步骤记录的值类型、上限常量与自动说明
 * @author Gyanis
 * @date 2026-11-11
 * @version 0.0.1
 * @copyright Copyright (c) . All rights reserved.
 */

#pragma once

#include "Base/Text.h"

#include <QDateTime>
#include <QImage>
#include <QPoint>
#include <QString>

namespace GyanisScribe::Core
{
    /**
     * @brief 一步的来源
     */
    enum class StepKind
    {
        Click,        ///< 鼠标点击
        WindowChange, ///< 前台窗口切换
        Manual,       ///< 审核时手动补的一步
    };

    /**
     * @brief 一步记录
     * @details index 是显示序号，从 1 开始，删除或调序后整体重排；它不是步骤数组下标。
     *          数组下标只出现在 StepSession 的方法参数里，从 0 开始。
     */
    struct RecordedStep
    {
        int       index = 0;               ///< 显示序号，从 1 开始；删除或调序后重排
        StepKind  kind  = StepKind::Click; ///< 步骤来源
        QDateTime at;                      ///< 发生时刻
        QPoint    globalLogicalPoint;      ///< 点击位置（逻辑全局坐标）；窗口切换时为当时光标位置
        bool      rightButton = false;     ///< 是否为右键点击
        QString   windowTitle;             ///< 前台窗口标题；取不到为空
        QString   processName;             ///< 前台窗口进程名；取不到为空
        QString   elementName;             ///< UIA 元素名；取不到为空
        QString   controlType;             ///< 本地化控件类型标签；取不到为空
        QString   description;             ///< 说明；追加时为空按 kind 生成，可编辑，最长 500 字符
        QString   imagePath;               ///< 整屏 PNG 路径；抓屏失败为空
        QImage    thumbnail;               ///< 320 像素宽缩略图，仅内存
        QString   captureError;            ///< 抓屏或落盘失败原因；成功为空
    };

    /// 单次记录的步骤上限，到达后协调器停止录制并要求先导出或放弃
    inline constexpr int kMaximumSteps = 200;

    /// 单条说明的字符上限，编辑与追加都按这一档截断
    inline constexpr int kMaximumStepDescriptionLength = 500;

    /// 审核窗缩略图宽度；高度按原图比例缩放
    inline constexpr int kStepThumbnailWidth = 320;

    /// 同一元素重复点击的合并时间窗
    inline constexpr int kClickMergeWindowMs = 1500;

    /// 同一元素重复点击的合并位移窗，按曼哈顿距离计
    inline constexpr int kClickMergeDistancePixels = 4;

    /**
     * @brief 生成点击类步骤的自动说明
     * @param elementName 元素名；空或纯空白时回落坐标
     * @param controlType 控件类型标签；为空时按「控件」显示
     * @param point 点击位置（逻辑全局坐标）
     * @param rightButton 是否右键点击
     * @return 有元素名时为「点击「名字」（类型）」，否则为「点击（x, y）」；右键统一加前缀
     */
    inline QString clickStepDescription(const QString &elementName, const QString &controlType, const QPoint &point, const bool rightButton)
    {
        const QString name = elementName.trimmed();
        QString       type = controlType.trimmed();
        if (type.isEmpty())
        {
            type = Base::uiText("控件");
        }
        QString text;
        if (name.isEmpty())
        {
            text = Base::uiText("点击（%1, %2）").arg(point.x()).arg(point.y());
        } else
        {
            text = Base::uiText("点击「%1」（%2）").arg(name, type);
        }
        return rightButton ? Base::uiText("右键%1").arg(text) : text;
    }

    /**
     * @brief 生成窗口切换步骤的自动说明
     * @param windowTitle 前台窗口标题
     * @param processName 前台窗口进程名
     * @return 有标题时为「切换到「标题」」，否则回落进程名；两者都取不到时只写「切换窗口」
     */
    inline QString windowChangeStepDescription(const QString &windowTitle, const QString &processName)
    {
        const QString title = windowTitle.trimmed();
        if (!title.isEmpty())
        {
            return Base::uiText("切换到「%1」").arg(title);
        }
        const QString process = processName.trimmed();
        return process.isEmpty() ? Base::uiText("切换窗口") : Base::uiText("切换窗口（%1）").arg(process);
    }

    /**
     * @brief 手动补步的自动说明
     * @return 固定文案「手动补充」，之后可在审核窗改写
     */
    inline QString manualStepDescription()
    {
        return Base::uiText("手动补充");
    }

} // namespace GyanisScribe::Core
