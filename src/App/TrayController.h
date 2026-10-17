/**
 * @file TrayController.h
 * @brief 系统托盘：托盘图标、右键菜单与通知
 * @author Gyanis
 * @date 2026-10-10
 * @version 0.0.1
 * @copyright Copyright (c) . All rights reserved.
 */

#pragma once

#include "App/TrayAction.h"
#include "App/TrayClick.h"

#include <QList>
#include <QObject>
#include <QString>
#include <QStringList>

#include <memory>
#include <optional>
#include <string_view>

class QMenu;

namespace GyanisScribe::Core
{
    class ISettingsStore;
} // namespace GyanisScribe::Core

namespace GyanisScribe::Platform
{
    class IPlatformIntegration;
    struct PlatformCapabilities;
} // namespace GyanisScribe::Platform

namespace GyanisScribe::Gui
{
    class IconProvider;
    class ThemeManager;
} // namespace GyanisScribe::Gui

namespace GyanisScribe::App
{

    /**
     * @brief 菜单项的静态描述
     */
    struct TrayMenuEntry
    {
        std::optional<TrayAction> action;    ///< 点了要做什么；空表示这是一条分隔线
        std::string_view          iconName;  ///< 图标名，空表示不放图标
        QString                   label;     ///< 菜单文字
        std::string_view          hotkeyKey; ///< 快捷键文本取自哪个配置键，空表示不带提示
    };

    /// 托盘菜单的条目表，顺序就是菜单里的顺序
    [[nodiscard]] QList<TrayMenuEntry> trayMenuEntries();

    /**
     * @brief 菜单项右侧的快捷键提示
     * @param settings 配置存储，存过的组合优先
     * @param hotkeyKey 这一行对应的配置键
     * @return 该动作当前生效的组合文本
     * @details 默认组合只有 HotkeyManager 一处真源：此前这里按「不是录屏/识别就当作截图」兜底，
     *          滚动长截图、屏幕取色、收起贴图与切组四行都会显示成截图那颗键。
     */
    [[nodiscard]] QString trayHotkeyHint(const Core::ISettingsStore &settings, std::string_view hotkeyKey);

    /**
     * @brief 按平台能力过滤后的菜单条目表
     * @param capabilities 平台能力表，来自 IPlatformIntegration::capabilities()
     * @return 这一台机器上真能做的条目；顺序与 trayMenuEntries() 一致
     * @details 缺能力的项直接从菜单里拿掉，而不是留着让用户点出一条报错：
     *          「截取当前窗口」要平台给得出前台窗口，给不出的档这一项就不该出现
     */
    [[nodiscard]] QList<TrayMenuEntry> visibleTrayMenuEntries(const Platform::PlatformCapabilities &capabilities);

    /**
     * @brief 托盘悬停提示的那一行文本
     * @param pinCount 现在挂着的贴图张数，0 表示没有
     * @param recordedSeconds 已录秒数；负数表示这一轮没在录
     * @param paused 是否停在暂停那一档，只有真在录的时候作数
     * @return 一行提示文本
     * @details 控制条可以收进托盘，收起后托盘就是唯一还能看出「还在录、录了多久」的地方；
     *          没在录时这一行留给贴图张数。做成纯函数是为了逐格点名，不依赖这台机器真有托盘。
     */
    [[nodiscard]] QString trayTooltip(int pinCount, int recordedSeconds, bool paused);

    /**
     * @brief 托盘控制器
     * @details 只负责「图标 + 菜单 + 通知」，动作一律发成信号：
     *          截图、录屏、识别的编排都在 AppController 里，托盘不认识它们。
     */
    class TrayController : public QObject
    {
        Q_OBJECT

    public:
        /**
         * @brief 构造控制器
         * @param icons 图标供给器
         * @param theme 主题管理器，托盘图标颜色跟着主题走
         * @param settings 配置存储，菜单上的快捷键提示从它读
         * @param integration 平台能力来源，决定菜单上放不放「截取当前窗口」这一项
         * @param parent Qt 父对象
         */
        TrayController(GyanisScribe::Gui::IconProvider &icons, GyanisScribe::Gui::ThemeManager &theme, Core::ISettingsStore &settings, Platform::IPlatformIntegration &integration,
                       QObject *parent = nullptr);


        /**
         * @brief 析构：先摘掉托盘图标，再随本类销毁菜单
         * @details 析构必须定义在 .cpp 里：成员是 unique_ptr<QMenu>，头文件里 QMenu 只是前向声明，
         *          在头文件里默认出来会让 GCC 对不完整类型求 sizeof（moc 也会实例化它），
         *          MSVC 放行——这条是容器里跑出来的。
         */
        ~TrayController() override;
        /**
         * @brief 装出托盘图标与菜单
         * @return true 表示这台机器上有托盘可用；false 表示平台不支持托盘，界面层要另找入口
         */
        bool install();

        /**
         * @brief 按当前配置与平台能力往菜单里摆条目
         * @param menu 目标菜单，所有权仍在调用方
         * @param screenNames 屏幕名清单；多屏时「整屏截图」变成带屏名的子菜单
         * @details 独立成公开方法是为了让用例不依赖真托盘：离屏平台装不出托盘，
         *          但菜单条目与信号接线照样要能逐条核。
         */
        void buildMenu(QMenu &menu, const QStringList &screenNames);


        /**
         * @brief 把贴图张数写进悬停提示
         * @param pinCount 当前贴在屏幕上的贴图张数
         * @details 装托盘之前调用也不会丢：install() 用的是这份存下来的张数。
         */
        void setPinCount(int pinCount);

        /**
         * @brief 把录屏状态推给托盘悬停提示
         * @param recordedSeconds 已录秒数；负数表示这一轮没在录，提示交还给贴图张数
         * @param paused 是否停在暂停那一档
         * @details 控制条收起后托盘是唯一的读数处；装托盘之前调用也不丢，提示用的是这份存下来的值。
         */
        void setRecordingStatus(int recordedSeconds, bool paused);

        /**
         * @brief 托盘图标上的一下鼠标按配置该做成什么
         * @param role 左键单击还是中键单击
         * @details 这是那两下的公开出口：真的托盘在这台机器上不一定建得出来，而「这一格配的是什么、
         *          落成哪个动作」直接调用就核得出来。配置每按一下现读一次，不缓存也不等重装托盘，
         *          设置页拨完下一次按就生效。
         */
        void trayClicked(TrayClickRole role);

    signals:
        /// 用户点了某一项
        void actionRequested(TrayAction action);

        /// 用户点名了某一块屏做整屏截图
        void screenCaptureRequested(const QString &screenName);

    private:
        /**
         * @brief 把托盘报回的激活原因换成那一档，再交给 trayClicked
         * @param reason Qt 的激活原因
         * @details 右键、双击与未知原因在这条路上不做事（换成空档），平台自己那一套不去抢。
         */
        void onActivated(QSystemTrayIcon::ActivationReason reason);

        GyanisScribe::Gui::IconProvider &m_icons;                   ///< 图标供给器，不拥有
        GyanisScribe::Gui::ThemeManager &m_theme;                   ///< 主题管理器，不拥有
        Core::ISettingsStore            &m_settings;                ///< 配置存储，不拥有
        Platform::IPlatformIntegration  &m_integration;             ///< 平台能力来源，决定菜单放不放哪几项，不拥有
        QSystemTrayIcon                 *m_icon = nullptr;          ///< 托盘图标，父对象是本控制器
        std::unique_ptr<QMenu>           m_menu;                    ///< 右键菜单：QSystemTrayIcon 不接管菜单所有权，由本类持有
        int                              m_pinCount        = 0;     ///< 现在挂着的贴图张数，只用于悬停提示
        int                              m_recordedSeconds = -1;    ///< 已录秒数，负数表示没在录
        bool                             m_recordingPaused = false; ///< 录制是否停在暂停那一档
    };

} // namespace GyanisScribe::App
