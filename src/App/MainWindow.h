/**
 * @file MainWindow.h
 * @brief 可隐藏主窗口：按类别竖排的设置页、关于页与历史页
 * @author Gyanis
 * @date 2026-10-10
 * @version 0.0.1
 * @copyright Copyright (c) . All rights reserved.
 */

#pragma once

#include "Ui/Language.h"
#include "Ui/ThemeKind.h"
#include "Ui/Toast.h"

#include <QString>
#include <QWidget>

#include <filesystem>
#include <optional>
#include <span>
#include <string_view>

class QListWidget;
class QStackedWidget;

namespace GyanisScribe::Settings
{
    class ISettingsStore;
} // namespace GyanisScribe::Settings

namespace GyanisScribe::Ui
{
    class IconProvider;
    class ThemeManager;
} // namespace GyanisScribe::Ui

namespace GyanisScribe::History
{
    class HistoryStore;
    class HistoryView;
} // namespace GyanisScribe::History

namespace GyanisScribe::App
{
    /// 主窗口能直接跳到的页面
    /// @note 新增一项要同时给 pageChoices() 加一行和一个 buildPage() 分支，两处都齐了界面才完整
    enum class MainWindowPage
    {
        General,    ///< 通用
        Capture,    ///< 截图
        Annotation, ///< 标注
        Pin,        ///< 贴图
        Record,     ///< 录屏
        Ocr,        ///< 文字识别
        Hotkey,     ///< 快捷键
        Theme,      ///< 主题
        About,      ///< 关于
        History,    ///< 历史
    };

    /**
     * @brief 主窗口的一页
     * @details 页、导航标签与导航图标绑在同一行：托盘菜单、导航行文字与 open() 的序号都从这张表来，
     *          分开放就会出现「菜单写着历史、点开是设置」。
     */
    struct PageChoice
    {
        MainWindowPage   page;  ///< 这一行对应的页
        std::string_view label; ///< 导航行上的中文名
        std::string_view icon;  ///< 导航行的图标名，必须是 IconProvider 真的有的那一个
    };

    /**
     * @brief 主窗口的全部页，下标即导航行与堆叠页的序号
     * @return 只读表格
     */
    [[nodiscard]] std::span<const PageChoice> pageChoices();

    /**
     * @brief 某一页的导航行序号
     * @param page 目标页
     * @return 序号；页不在表里时返回 -1
     */
    [[nodiscard]] int indexOfMainWindowPage(MainWindowPage page);

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
     * @brief 界面语言下拉框的一行
     * @details 标签按各自语言自称（「中文」与「English」），所以这一列不进翻译目录：
     *          切成英文后仍要能认出「中文」是哪一项，否则用户回不去。
     */
    struct LanguageChoice
    {
        GyanisScribe::Ui::AppLanguage language; ///< 选中这一行时写入配置的档位
        std::string_view              label;    ///< 下拉框里显示的自称文本
    };

    /**
     * @brief 界面语言下拉框的全部行
     * @return 只读表格，下标即下拉框序号
     */
    [[nodiscard]] std::span<const LanguageChoice> languageChoices();

    /**
     * @brief 下拉框序号与语言档位的互指
     * @param index 下拉框序号
     * @return 对应档位；序号越界返回空
     */
    [[nodiscard]] std::optional<GyanisScribe::Ui::AppLanguage> appLanguageAt(int index);

    /**
     * @brief 语言档位在下拉框里的序号
     * @param language 档位
     * @return 序号；档位不在表里时返回 -1
     */
    [[nodiscard]] int indexOfAppLanguage(GyanisScribe::Ui::AppLanguage language);

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
     *          版面是左侧导航加右侧一页，导航顺序就是 pageChoices() 的顺序。
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
         * @param historyStore 历史清单，历史页从它读列表、往它删记录
         * @param parent Qt 父对象
         * @note 刻意不认识 AppController：热键与语言的改动由各自的订阅者接走，
         *       设置页不需要为了「生效」去拖一个控制器进来。历史页的三个出口同样只发信号。
         */
        explicit MainWindow(Settings::ISettingsStore &settings, GyanisScribe::Ui::ThemeManager &theme, GyanisScribe::Ui::IconProvider &icons, History::HistoryStore &historyStore,
                            QWidget *parent = nullptr);

        /**
         * @brief 切到某一页并显示窗口
         * @param page 目标页
         * @details 页不在表里时只记日志不切换：切到第 0 页会把用户的操作读成「点了没反应」，
         *          而日志里留着的是他真正想要的那一页。
         */
        void open(MainWindowPage page);

        /**
         * @brief 重读历史清单并刷新历史页
         * @details 截图与录屏落盘之后由控制器调用：列表不会自己发现盘上多了文件。
         */
        void refreshHistory();

    signals:
        /**
         * @brief 历史页要求把一条成品重新贴回屏幕
         * @param filePath 成品文件路径
         */
        void historyPinRequested(const std::filesystem::path &filePath);

        /**
         * @brief 历史页要求对一条成品做文字识别
         * @param filePath 成品文件路径
         */
        void historyOcrRequested(const std::filesystem::path &filePath);

        /**
         * @brief 历史页要求从清单里去掉一条
         * @param filePath 成品文件路径
         */
        void historyRemoveRequested(const std::filesystem::path &filePath);

        /**
         * @brief 历史页要落进气泡的一条提示
         * @param message 中文正文
         * @param kind 语义种类
         */
        void historyMessage(const QString &message, GyanisScribe::Ui::ToastKind kind);

    private:
        /**
         * @brief 搭某一页
         * @param page 目标页，取值只会来自 pageChoices()
         * @return 归本窗口所有的页控件；这一页还没有搭法时返回空指针
         * @details 分派只认枚举、不认序号：表里新加一页而这里漏了一支时，
         *          调用方会跳开这一行并在日志里点名，导航与堆叠页因此不会错位半格。
         */
        QWidget *buildPage(MainWindowPage page);

        /// 通用页：界面语言
        QWidget *buildGeneralPage();
        /// 截图页：成品目录与取色复制的写法
        QWidget *buildCapturePage();
        /// 标注页：默认线宽与字号
        QWidget *buildAnnotationPage();
        /// 贴图页：新建贴图的默认不透明度
        QWidget *buildPinPage();
        /// 录屏页：成品目录与默认帧率
        QWidget *buildRecordPage();
        /// 识别页：识别语言与语言包目录
        QWidget *buildOcrPage();
        /// 快捷键页：四个全局热键
        QWidget *buildHotkeyPage();
        /// 主题页：配色方案
        QWidget *buildThemePage();
        /// 关于页
        QWidget *buildAboutPage();
        /// 历史页：缩略图列表与三个出口
        QWidget *buildHistoryPage();

        Settings::ISettingsStore       &m_settings;          ///< 配置存储，不拥有
        GyanisScribe::Ui::ThemeManager &m_theme;             ///< 主题管理器，不拥有
        GyanisScribe::Ui::IconProvider &m_icons;             ///< 图标供给器，不拥有
        QListWidget                    *m_nav   = nullptr;   ///< 左侧导航，一行一页
        QStackedWidget                 *m_stack = nullptr;   ///< 右侧页容器，序号与导航行同解
        History::HistoryStore          &m_historyStore;      ///< 历史清单，历史页从它读
        History::HistoryView           *m_history = nullptr; ///< 历史页控件，重读清单时刷新它
    };

} // namespace GyanisScribe::App
