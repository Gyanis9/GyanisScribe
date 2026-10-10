/**
 * @file TrayController.h
 * @brief 系统托盘：托盘图标、右键菜单与通知
 * @author Gyanis
 * @date 2026-10-10
 * @version 0.0.1
 * @copyright Copyright (c) . All rights reserved.
 */

#pragma once

#include <QList>
#include <QObject>
#include <QString>

#include <memory>
#include <optional>
#include <string_view>

class QMenu;
class QSystemTrayIcon;

namespace GyanisScribe::Settings
{
    class ISettingsStore;
} // namespace GyanisScribe::Settings

namespace GyanisScribe::Ui
{
    class IconProvider;
    class ThemeManager;
} // namespace GyanisScribe::Ui

namespace GyanisScribe::App
{

    /**
     * @brief 托盘菜单的一项动作
     * @details 用例按这张表核对「菜单里有几项、各指向哪儿」，不依赖真的托盘图标。
     */
    enum class TrayAction
    {
        CaptureRegion,     ///< 框选截图
        CaptureFullScreen, ///< 整屏截图
        CaptureLastRegion, ///< 重复上一次选区
        ToggleRecord,      ///< 开始或停止录屏
        Recognize,         ///< 框选后识别文字
        PickColor,         ///< 屏幕取色
        OpenSettings,      ///< 打开设置页
        OpenAbout,         ///< 打开关于页
        RecognizeFile,     ///< 挑一张盘上的图去识别
        Quit,              ///< 退出程序
    };

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
         * @param parent Qt 父对象
         */
        TrayController(GyanisScribe::Ui::IconProvider &icons, GyanisScribe::Ui::ThemeManager &theme, Settings::ISettingsStore &settings, QObject *parent = nullptr);


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
         * @brief 发一条托盘通知
         * @param title 标题
         * @param body 正文
         */
        void notify(const QString &title, const QString &body);

        /// 托盘是否已经装上
        [[nodiscard]] bool isInstalled() const noexcept;

        /**
         * @brief 按动作查菜单项文字
         * @param action 目标动作
         * @return 菜单上显示的那句文字（含快捷键提示）；表里没有时返回空串
         */
        [[nodiscard]] QString labelFor(TrayAction action) const;

    signals:
        /// 用户点了某一项
        void actionRequested(TrayAction action);

    private:
        GyanisScribe::Ui::IconProvider &m_icons;          ///< 图标供给器，不拥有
        GyanisScribe::Ui::ThemeManager &m_theme;          ///< 主题管理器，不拥有
        Settings::ISettingsStore       &m_settings;       ///< 配置存储，不拥有
        QSystemTrayIcon                *m_icon = nullptr; ///< 托盘图标，父对象是本控制器
        std::unique_ptr<QMenu>          m_menu;           ///< 右键菜单：QSystemTrayIcon 不接管菜单所有权，由本类持有
    };

} // namespace GyanisScribe::App
