/**
 * @file MainWindow.h
 * @brief 可隐藏主窗口：设置页与关于页的入口
 * @author Gyanis
 * @date 2026-10-10
 * @version 0.0.1
 * @copyright Copyright (c) . All rights reserved.
 */

#pragma once

#include "Ui/ThemeKind.h"

#include <QString>
#include <QWidget>

#include <optional>
#include <span>
#include <string_view>

class QTabWidget;

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
    /// 主窗口能直接跳到的页面
    enum class MainWindowPage
    {
        Settings, ///< 设置
        About,    ///< 关于
    };

    /**
     * @brief 主题下拉框的一行
     * @details 档位与它的中文标签绑在同一行里，序号只有这张表一个真源；
     *          分开放就要在控件里再抄一份标签，抄错时用户选「深色」拿到的却是别的档。
     */
    struct ThemeChoice
    {
        GyanisScribe::Ui::ThemeKind kind;  ///< 选中这一行时应用的档位
        std::string_view            label; ///< 下拉框里显示的中文名
    };

    /**
     * @brief 主题下拉框的全部行
     * @return 只读表格，下标即下拉框序号
     */
    [[nodiscard]] std::span<const ThemeChoice> themeChoices();

    /**
     * @brief 下拉框序号与主题档位的互指
     * @param index 下拉框序号
     * @return 对应档位；序号越界返回空
     */
    [[nodiscard]] std::optional<GyanisScribe::Ui::ThemeKind> themeKindAt(int index);

    /**
     * @brief 主题档位在下拉框里的序号
     * @param themeKind 档位
     * @return 序号；档位不在表里时返回 -1
     */
    [[nodiscard]] int indexOfThemeKind(GyanisScribe::Ui::ThemeKind themeKind);

    /**
     * @brief 关于页正文
     * @param version 应用版本
     * @return 一段可以直接显示的文本
     * @details 版本号只从工程真源传进来，这里不写第二份字面量；
     *          许可一句指向仓库里的第三方许可清单，避免在界面上抄一份会漂移的清单。
     */
    [[nodiscard]] QString aboutText(const QString &version);

    /**
     * @brief 主窗口
     * @details 这个工具平时住在托盘与热键上，主窗口只是配置与信息的入口：
     *          关掉它就回到托盘，不退出程序。
     */
    class MainWindow : public QWidget
    {
        Q_OBJECT

    public:
        /**
         * @brief 构造主窗口
         * @param settings 配置存储，页面上的每一项都直接读写它
         * @param theme 主题管理器，主题下拉即时生效走它
         * @param icons 图标供给器
         * @param parent Qt 父对象
         * @note 刻意不认识 AppController：热键与语言的改动由各自的订阅者接走，
         *       设置页不需要为了「生效」去拖一个控制器进来。
         */
        explicit MainWindow(Settings::ISettingsStore &settings, GyanisScribe::Ui::ThemeManager &theme, GyanisScribe::Ui::IconProvider &icons, QWidget *parent = nullptr);

        /**
         * @brief 切到某一页并显示窗口
         * @param page 目标页
         */
        void open(MainWindowPage page);

    private:
        /// 搭设置页：热键、目录、主题、识别语言四组
        QWidget *buildSettingsPage();
        /// 搭关于页
        QWidget *buildAboutPage();

        Settings::ISettingsStore       &m_settings;       ///< 配置存储，不拥有
        GyanisScribe::Ui::ThemeManager &m_theme;          ///< 主题管理器，不拥有
        GyanisScribe::Ui::IconProvider &m_icons;          ///< 图标供给器，不拥有
        QTabWidget                     *m_tabs = nullptr; ///< 两页的容器
    };

} // namespace GyanisScribe::App
