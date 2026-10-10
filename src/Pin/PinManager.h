/**
 * @file PinManager.h
 * @brief 多张贴图的管理器：创建、关闭、全部关闭与布局存取
 * @author Gyanis
 * @date 2026-10-09
 * @version 0.0.1
 * @copyright Copyright (c) . All rights reserved.
 */

#pragma once

#include "Pin/IPinWindow.h"
#include "Pin/PinLayout.h"

#include <QObject>

#include <memory>
#include <vector>

namespace GyanisScribe::Platform
{
    class IPlatformIntegration;
} // namespace GyanisScribe::Platform

namespace GyanisScribe::Settings
{
    class ISettingsStore;
} // namespace GyanisScribe::Settings

namespace GyanisScribe::Pin
{
    class PinWindow;

    /**
     * @brief 贴图管理器
     * @details 窗口由本管理器创建并持有；用户在窗口上按 Esc 或菜单里选关闭时，
     *          窗口发 closedByUser，管理器把自己记的那一份摘掉——两边都只有一条摘除路径。
     * @note 图片内容不进布局文件：恢复贴图要有图，而图从哪来（历史目录、剪贴板）是界面层的事，
     *       布局只管「摆在哪、多大、多透」。
     */
    class PinManager : public QObject
    {
        Q_OBJECT

    public:
        /**
         * @brief 绑定配置与平台集成
         * @param settings 布局的读写处，生命周期必须长于本管理器
         * @param integration 平台能力来源，透传给每张贴图，生命周期必须长于本管理器
         * @param parent QObject 父对象，默认无
         */
        PinManager(Settings::ISettingsStore &settings, Platform::IPlatformIntegration &integration, QObject *parent = nullptr);

        /**
         * @brief 析构：关掉还在的贴图窗口
         */
        ~PinManager() override;

        PinManager(const PinManager &)            = delete;
        PinManager &operator=(const PinManager &) = delete;

        /**
         * @brief 贴出一张图片
         * @param image 图片（物理像素）
         * @param globalTopLeft 窗口左上角的全局逻辑坐标
         * @return 新建的贴图窗口引用，所有权仍在管理器手里
         */
        IPinWindow &addPin(const QImage &image, const QPoint &globalTopLeft);

        /**
         * @brief 当前贴图张数
         * @return 张数
         */
        [[nodiscard]] int count() const noexcept;

        /**
         * @brief 指定位置的那张贴图
         * @param index 索引
         * @return 窗口引用；越界时返回空指针，调用方应先按 count() 判过界
         */
        [[nodiscard]] IPinWindow *pinAt(int index) const;

        /**
         * @brief 把当前布局写进配置，并把每张贴图的像素落到缓存目录
         * @return 成功返回空载荷；某一张写不下去时整批不存，返回带路径的原因
         * @details 布局里只有几何摆不回像素：保存那一刻给每张在
         *          Core::Paths::cacheDirectory()/Pins 下写一份 PNG，路径记进布局。
         */
        [[nodiscard]] Core::Expected<void> saveLayout();

        /**
         * @brief 按配置里那份布局把贴图摆回来
         * @return 成功返回真的摆出来的张数；布局本身读不出时返回 InvalidArgument 并点名坏在哪一项
         * @details 布局形状坏 → 整次失败（少摆一张会让用户以为那张被删了）；
         *          单个图片文件读不出 → 只跳过那一张并记一条日志（图是用户自己删的，
         *          让剩下的继续摆回来比整次放弃更接近他留下的桌面）。
         */
        [[nodiscard]] Core::Expected<int> restoreLayout();

        /**
         * @brief 关闭全部贴图
         */
        void closeAll();

        /**
         * @brief 让所有正在鼠标穿透的贴图重新吃鼠标
         * @return 恢复的张数；没有一张在穿透时返回 0
         * @details 穿透中的贴图收不到右键，也就关不掉自己那一档——这一条是那个死局的出口，
         *          放在托盘而不是贴在图上。
         */
        int clearMouseThrough();

    signals:
        /**
         * @brief 贴图张数变了
         * @param count 新的张数
         */
        void pinCountChanged(int count);

        /**
         * @brief 某张贴图上的「识别文字」被点了
         * @param image 那张贴图当前的图片
         * @details 管理器只转发，不自己识别：识别要用哪个后端、结果摆在哪个窗口，都是界面层的事，
         *          而贴图窗口归管理器持有，外面拿不到——所以出口收在这一层，一处接线管所有张。
         */
        void pinOcrRequested(const QImage &image);

    private:
        /// 把某个已自行关闭的窗口从登记里摘掉
        void forget(PinWindow *window);

        Settings::ISettingsStore               &m_settings;    ///< 布局读写处
        Platform::IPlatformIntegration         &m_integration; ///< 平台能力来源
        std::vector<std::unique_ptr<PinWindow>> m_pins;        ///< 在册贴图，按创建顺序
    };

} // namespace GyanisScribe::Pin
