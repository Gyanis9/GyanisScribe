/**
 * @file PinWindow.h
 * @brief 一张贴图的窗口：无边框、置顶、可拖动缩放旋转调透明度与穿透
 * @author Gyanis
 * @date 2026-10-09
 * @version 0.0.1
 * @copyright Copyright (c) . All rights reserved.
 */

#pragma once

#include "Pin/IPinWindow.h"
#include "Pin/PinTransform.h"

#include <QImage>
#include <QPoint>
#include <QRectF>
#include <QWheelEvent>
#include <QWidget>

#include <optional>
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

    /**
     * @brief 贴图窗口
     * @details 窗口的矩形始终是「旋转后的外接框」，绘制时按中心旋转回去：这样拖动、命中与手柄
     *          都在一个轴对齐的框里算，不必到处处理斜框。
     * @note 一次操作要改的只有中心位置、缩放、旋转与透明度四项，其余（窗口尺寸、手柄位置）都由这四项派生，
     *       所以任何一处改动都走 applyGeometry() 这一个出口，避免出现第二套尺寸算法。
     */
    class PinWindow : public QWidget, public IPinWindow
    {
        Q_OBJECT

    public:
        /**
         * @brief 用一张图构造贴图
         * @param image 图片（物理像素），显示尺寸按其 devicePixelRatio 折算成逻辑尺寸
         * @param settings 配置来源，保存贴图时从这里取目录
         * @param integration 平台集成，用于查鼠标穿透能力
         * @param parent 父窗口，默认无（顶层）
         */
        PinWindow(const QImage &image, Settings::ISettingsStore &settings, const Platform::IPlatformIntegration &integration, QWidget *parent = nullptr);

        /**
         * @brief 析构窗口
         */
        ~PinWindow() override;

        /**
         * @brief 设缩放，见 IPinWindow::setScale()
         * @param scale 缩放因子，越界按边界取
         */
        void setScale(double scale) override;

        /// 当前缩放，见 IPinWindow::scale()
        [[nodiscard]] double scale() const noexcept override;

        /**
         * @brief 设旋转角度，见 IPinWindow::setRotationDegrees()
         * @param degrees 顺时针角度，折进 [0,360)
         */
        void setRotationDegrees(double degrees) override;

        /// 当前旋转角度，见 IPinWindow::rotationDegrees()
        [[nodiscard]] double rotationDegrees() const noexcept override;

        /**
         * @brief 设不透明度，见 IPinWindow::setOpacityLevel()
         * @param opacity 取值 (0,1]，越界按边界取
         */
        void setOpacityLevel(double opacity) override;

        /// 当前不透明度，见 IPinWindow::opacityLevel()
        [[nodiscard]] double opacityLevel() const noexcept override;

        /**
         * @brief 开关鼠标穿透，见 IPinWindow::setMouseThrough()
         * @param through 是否穿透
         * @return 成功返回空载荷；平台没有该能力时返回 PlatformUnsupported 并写明替代做法
         * @details 重写接口：走 Qt 的 WindowTransparentForInput 标志（平台原生能力），
         *          穿透时同时去掉焦点接受，避免贴图吃不到鼠标却又抢走焦点。
         */
        Core::Expected<void> setMouseThrough(bool through) override;

        /// 是否穿透，见 IPinWindow::isMouseThrough()
        [[nodiscard]] bool isMouseThrough() const noexcept override;

        /**
         * @brief 开关置顶，见 IPinWindow::setAlwaysOnTop()
         * @param pinned 是否保持置顶
         */
        void setAlwaysOnTop(bool pinned) override;

        /// 是否置顶，见 IPinWindow::isAlwaysOnTop()
        [[nodiscard]] bool isAlwaysOnTop() const noexcept override;

        /**
         * @brief 收起或摆回这张贴图，见 IPinWindow::setConcealed()
         * @param concealed 收起还是摆回
         * @details 重写基类接口：收 = hide()，摆回 = show() 再 raise()——不 raise 的话
         *          它回到桌面时落在别的窗口后面，看起来就像没收回来。图片与摆位都不动。
         */
        void setConcealed(bool concealed) override;

        /**
         * @brief 此刻是否被收着，见 IPinWindow::isConcealed()
         * @return 窗口不在屏上时为真
         */
        [[nodiscard]] bool isConcealed() const noexcept override;

        /**
         * @brief 交进剪贴板，见 IPinWindow::copyToClipboard()
         */
        void copyToClipboard() override;

        /**
         * @brief 存成文件，见 IPinWindow::saveToFile()
         * @param directory 目标目录，不存在时先创建
         * @param fileName 目录里的文件名，原样使用，不加时间戳也不改名；后缀决定写成什么格式
         * @param quality 有损档的质量，-1 表示交给 Qt 的默认档
         * @return 成功返回写下的路径；建目录或写文件失败时返回带路径的原因
         * @details 与基类一致，差别在命名：文件名归调用方，管理器一次写整批时靠序号区分，
         *          否则同一毫秒里的两张会写到同一个文件上，只剩最后一张。
         */
        [[nodiscard]] Core::Expected<QString> saveToFile(const std::filesystem::path &directory, const QString &fileName, int quality) override;

        /// 全屏坐标里的位置，见 IPinWindow::globalRect()
        [[nodiscard]] QRect globalRect() const override;

        /// 布局记录，见 IPinWindow::state()
        [[nodiscard]] PinState state() const override;


        /**
         * @brief 手柄占位框，供绘制与用例核对
         * @return 八个框，顺序与 HandleKind 一致
         */
        [[nodiscard]] std::vector<QRectF> handleRects() const;

    signals:
        /**
         * @brief 用户要求识别这张图上的文字
         * @param image 当前图片
         * @note 由 PinManager 逐张转发出去，界面层在 AppController 里接；菜单项恒是可点的，
         *       能不能识别由后端回答——拿不到后端时是一条中文原因，而不是把项灰掉让人猜
         */
        void ocrRequested(const QImage &image);

        /**
         * @brief 贴图被关闭，管理器据此把自己记的那一份摘掉
         */
        void closedByUser();

    protected:
        /**
         * @brief 绘制图片、选中边框与手柄
         * @param event 绘制事件，本实现不使用其载荷
         */
        void paintEvent(QPaintEvent *event) override;

        /**
         * @brief 按下：在手柄上起拖缩放，否则开始移动
         * @param event 鼠标事件
         */
        void mousePressEvent(QMouseEvent *event) override;

        /**
         * @brief 拖动：改缩放或移动窗口
         * @param event 鼠标事件
         */
        void mouseMoveEvent(QMouseEvent *event) override;

        /**
         * @brief 松开：结束本次拖拽
         * @param event 鼠标事件
         */
        void mouseReleaseEvent(QMouseEvent *event) override;

        /**
         * @brief 左键双击：回到原始比例与角度
         * @param event 鼠标事件，非左键时交回 Qt 继续分发
         * @details 重写 QWidget 的空实现：这一击与按 `0` 走同一个出口，已经在 1:1 且没转角时是幂等。
         */
        void mouseDoubleClickEvent(QMouseEvent *event) override;

        /**
         * @brief 滚轮：默认缩放，Ctrl 旋转，Shift 改透明度
         * @param event 滚轮事件
         */
        void wheelEvent(QWheelEvent *event) override;

        /**
         * @brief 键盘：Esc 关闭，`R`/`Shift+R` 转正一挡，`0` 回到原始比例与角度
         * @param event 键盘事件；带 Ctrl 的字母不归这里管，交回 Qt
         */
        void keyPressEvent(QKeyEvent *event) override;

        /**
         * @brief 右键弹出菜单
         * @param event 上下文菜单事件，取它的全局位置
         */
        void contextMenuEvent(QContextMenuEvent *event) override;

    private:
        /**
         * @brief 建右键菜单
         * @return 菜单对象，所有权交调用方（Qt 的 exec 路径）
         */
        [[nodiscard]] class QMenu *buildContextMenu();

        /// 按当前的缩放与旋转重算窗口尺寸与位置，保持中心不动
        void applyGeometry();

        /// 画布矩形（未旋转、已缩放）在本窗口内的位置
        [[nodiscard]] QRectF contentRect() const;

        /**
         * @brief 回到原始比例（1:1）与角度（0°），透明度与位置不动
         * @details 键盘 `0` 与左键双击共用这一个出口：两处各写一份复位就会在将来只改一处。
         *          位置不跟着改中心：复位尺寸不该把人摆好的那一站挪走。
         */
        void resetScaleAndRotation();

        QImage                                m_image;                     ///< 贴图内容，物理像素
        QSizeF                                m_logicalSize;               ///< 图片逻辑尺寸
        double                                m_scale           = 1.0;     ///< 缩放因子
        double                                m_rotationDegrees = 0.0;     ///< 旋转角度
        double                                m_opacity         = 1.0;     ///< 不透明度
        bool                                  m_mouseThrough    = false;   ///< 是否鼠标穿透
        bool                                  m_pinned          = true;    ///< 是否置顶
        QPoint                                m_dragOffset;                ///< 移动时起笔点与窗口左上角的差
        std::optional<HandleKind>             m_draggedHandle;             ///< 正在拖的手柄，没拖时为空
        double                                m_dragInitialScale    = 1.0; ///< 起拖时的缩放
        double                                m_dragInitialDistance = 0.0; ///< 起拖时指针到中心的距离
        Settings::ISettingsStore             &m_settings;                  ///< 配置来源（保存目录）
        const Platform::IPlatformIntegration &m_integration;               ///< 平台能力查询
    };

} // namespace GyanisScribe::Pin
