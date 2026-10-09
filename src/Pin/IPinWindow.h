/**
 * @file IPinWindow.h
 * @brief 一张贴图的对外契约
 * @author Gyanis
 * @date 2026-10-09
 * @version 0.0.1
 * @copyright Copyright (c) . All rights reserved.
 */

#pragma once

#include "Core/Types.h"

#include "Pin/PinLayout.h"

#include <QImage>
#include <QRect>

#include <filesystem>

namespace GyanisScribe::Pin
{

    /**
     * @brief 贴图窗口契约
     * @details 管理器与界面只通过本接口改一张贴图的状态：缩放、旋转、透明度、穿透都是「设进去一个值，
     *          窗口自己重排」，调用方不需要知道窗口内部怎么算。
     */
    class IPinWindow
    {
    public:
        IPinWindow()                              = default;
        virtual ~IPinWindow()                     = default;
        IPinWindow(const IPinWindow &)            = delete;
        IPinWindow &operator=(const IPinWindow &) = delete;

        /**
         * @brief 换一张图上去（再次贴图用）
         * @param image 新的图片，物理像素；缩放因子保持不变
         */
        virtual void setImage(const QImage &image) = 0;

        /**
         * @brief 设缩放因子
         * @param scale 会被夹进合法区间，越界不报错而是按边界取
         */
        virtual void setScale(double scale) = 0;

        /**
         * @brief 当前缩放因子
         * @return 生效中的值
         */
        [[nodiscard]] virtual double scale() const noexcept = 0;

        /**
         * @brief 设旋转角度
         * @param degrees 顺时针角度，会折进 [0,360)
         */
        virtual void setRotationDegrees(double degrees) = 0;

        /**
         * @brief 当前旋转角度
         * @return [0,360) 内的值
         */
        [[nodiscard]] virtual double rotationDegrees() const noexcept = 0;

        /**
         * @brief 设不透明度
         * @param opacity 取值 (0,1]，越界按边界取
         */
        virtual void setOpacityLevel(double opacity) = 0;

        /**
         * @brief 当前不透明度
         * @return 生效中的值
         */
        [[nodiscard]] virtual double opacityLevel() const noexcept = 0;

        /**
         * @brief 开或关鼠标穿透
         * @param through true 表示鼠标事件穿过贴图
         * @note 平台没有这个能力时返回 PlatformUnsupported 并保持原状，不让用户以为开关生效了
         */
        virtual Core::Expected<void> setMouseThrough(bool through) = 0;

        /**
         * @brief 是否处于鼠标穿透
         * @return true 表示鼠标不吃在贴图上
         */
        [[nodiscard]] virtual bool isMouseThrough() const noexcept = 0;

        /**
         * @brief 保持置顶
         * @param pinned true 压在普通窗口之上
         */
        virtual void setAlwaysOnTop(bool pinned) = 0;

        /**
         * @brief 是否置顶
         * @return true 表示带置顶标志
         */
        [[nodiscard]] virtual bool isAlwaysOnTop() const noexcept = 0;

        /**
         * @brief 把当前图片交进剪贴板
         */
        virtual void copyToClipboard() = 0;

        /**
         * @brief 把当前图片存成文件
         * @param directory 目标目录
         * @return 成功返回写下的文件完整路径；失败返回带路径与原因的 Error
         */
        [[nodiscard]] virtual Core::Expected<QString> saveToFile(const std::filesystem::path &directory) = 0;

        /**
         * @brief 贴图当前在全屏坐标里的位置
         * @return 逻辑坐标下的矩形
         */
        [[nodiscard]] virtual QRect globalRect() const = 0;

        /**
         * @brief 汇总成一条布局记录
         * @return 供保存布局使用的那一份状态
         */
        [[nodiscard]] virtual PinState state() const = 0;
    };

} // namespace GyanisScribe::Pin
