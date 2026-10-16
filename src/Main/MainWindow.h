/**
 * @file MainWindow.h
 * @brief 可隐藏主窗口：按类别竖排的设置页、关于页与历史页
 * @author Gyanis
 * @date 2026-10-10
 * @version 0.0.1
 * @copyright Copyright (c) . All rights reserved.
 */

#pragma once

#include "App/UpdateCheck.h"
#include "Platform/PlatformCapabilities.h"
#include "Gui/Theme/Language.h"
#include "Gui/Theme/ThemeKind.h"
#include "Gui/Controls/Toast.h"

#include <QString>
#include <QWidget>

#include <filesystem>
#include <optional>
#include <span>
#include <string_view>

class QListWidget;
class QStackedWidget;

namespace GyanisScribe::Core
{
    class ISettingsStore;
} // namespace GyanisScribe::Core

namespace GyanisScribe::Gui
{
    class IconProvider;
    class ThemeManager;
} // namespace GyanisScribe::Gui

namespace GyanisScribe::Core
{
    class HistoryStore;
} // namespace GyanisScribe::Core

namespace GyanisScribe::Gui
{
    class HistoryView;
} // namespace GyanisScribe::Gui

namespace GyanisScribe::Plugins
{
    class FfmpegAudioDeviceEnumerator;
    class FfmpegVideoEncoderEnumerator;
} // namespace GyanisScribe::Plugins

namespace GyanisScribe::Platform
{
    class IPlatformIntegration;
} // namespace GyanisScribe::Platform

namespace GyanisScribe::App
{
    /// 托盘动作的枚举，声明在 App/GyanisScribe::App::TrayController.h；这里只要一个不透明的前向声明，
    /// 为的是动作条能发同一个枚举而不用把整个托盘类拖进这张窗口的头
    enum class TrayAction;
} // namespace GyanisScribe::App

namespace GyanisScribe::Main
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
     * @brief 「整屏截图」那一组菜单项：多屏时每块屏一条
     * @param screenNames 现有屏幕名，按平台给的原样
     * @return 每块屏一条的标签；只有一块屏（或一块都没有）时返回空表——单屏上没有「另一块」可点
     * @details 标签里带屏名：两块 1920×1080 的屏在「整屏截图」四个字下分不出谁是谁。
     */
    [[nodiscard]] std::vector<QString> screenCaptureLabels(const QStringList &screenNames);

    /**
     * @brief 主窗口的一页
     * @details 页、导航标签与导航图标绑在同一行：托盘菜单、导航行文字与 open() 的序号都从这张表来，
     *          分开放就会出现「菜单写着历史、点开是设置」。
     */
    struct PageChoice
    {
        MainWindowPage   page;  ///< 这一行对应的页
        std::string_view label; ///< 导航行上的中文名
        std::string_view icon;  ///< 导航行的图标名，必须是 GyanisScribe::Gui::IconProvider 真的有的那一个
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
        GyanisScribe::Gui::ThemeKind kind;  ///< 选中这一行时应用的档位
        std::string_view             label; ///< 下拉框里显示的中文名
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
    [[nodiscard]] std::optional<GyanisScribe::Gui::ThemeKind> themeKindAt(int index);

    /**
     * @brief 主题档位在下拉框里的序号
     * @param themeKind 档位
     * @return 序号；档位不在表里时返回 -1
     */
    [[nodiscard]] int indexOfThemeKind(GyanisScribe::Gui::ThemeKind themeKind);

    /**
     * @brief 界面语言下拉框的一行
     * @details 标签按各自语言自称（「中文」与「English」），所以这一列不进翻译目录：
     *          切成英文后仍要能认出「中文」是哪一项，否则用户回不去。
     */
    struct LanguageChoice
    {
        GyanisScribe::Gui::AppLanguage language; ///< 选中这一行时写入配置的档位
        std::string_view               label;    ///< 下拉框里显示的自称文本
    };


    /**
     * @brief 下拉框序号与语言档位的互指
     * @param index 下拉框序号
     * @return 对应档位；序号越界返回空
     */
    [[nodiscard]] std::optional<GyanisScribe::Gui::AppLanguage> appLanguageAt(int index);

    /**
     * @brief 语言档位在下拉框里的序号
     * @param language 档位
     * @return 序号；档位不在表里时返回 -1
     */
    [[nodiscard]] int indexOfAppLanguage(GyanisScribe::Gui::AppLanguage language);

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
         * @param capabilities 平台能力表：热键页按它决定给不给登记入口，
         *        没有全局热键后端的平台上改组合键不会生效，界面不许装作会生效
         * @param parent Qt 父对象
         * @note 刻意不认识 AppController：热键与语言的改动由各自的订阅者接走，
         *       设置页不需要为了「生效」去拖一个控制器进来。历史页的三个出口同样只发信号。
         */
        explicit MainWindow(GyanisScribe::Core::ISettingsStore &settings, GyanisScribe::Gui::ThemeManager &theme, GyanisScribe::Gui::IconProvider &icons,
                            GyanisScribe::Core::HistoryStore &historyStore, GyanisScribe::Platform::PlatformCapabilities capabilities,
                            GyanisScribe::Platform::IPlatformIntegration &integration, QWidget *parent = nullptr);

        /**
         * @brief 析构：设备枚举器要在这里才看得到完整类型
         * @details 头里对它只有前向声明，而 unique_ptr 的隐式析构会被内联进每个 include 本头的
         *          翻译单元 —— GCC 在那里嫌类型不完整（实测 C++ 侧红，MSVC 侧看不出）。
         */
        ~MainWindow() override;

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

        /**
         * @brief 把当前全部设置导出成一个文件，并把结果落成一条气泡提示
         * @param filePath 目标路径；界面上来自「导出配置…」的对话框
         * @details 写成一条消息而不是返回值：按钮那一层只需要「发生了什么、用户看见什么」，
         *          失败时原因同时进日志（日志保持中文）与气泡（走界面语言）。
         * @note 公开是为了让用例能直接驱动「选好路径以后发生的事」——文件对话框要人点，判据钉不到它身上。
         */
        void exportSettingsToFile(const QString &filePath);

        /**
         * @brief 从一个文件把设置读回来，并把结果与「要重开哪一处」落成一条气泡提示
         * @param filePath 来源路径，须是本程序导出的那种 JSON 形状
         * @details 逐项走的仍是 setValue，所以每个键都会发出变更通知：订阅配置的模块（热键、主题）
         *          立刻跟上新值；而这一页上已经摆好的控件不会自己重读，提示里要说明重开设置窗口。
         */
        void importSettingsFromFile(const QString &filePath);

        /**
         * @brief 把现在用的那份色板写成一份可分享的文件（一行一格 #RRGGBB）
         * @param filePath 目标路径；界面上来自「导出色板…」的对话框
         * @details 公开是为了让判据能驱动「选好路径以后发生的事」——文件对话框要人点。
         *          配置里那份读不出时报原样原因，不拿内置那八格冒充用户存的那份。
         */
        void exportPaletteToFile(const QString &filePath);

        /**
         * @brief 从一份色板文件读回来，整份替换配置里那一格
         * @param filePath 来源路径，一行一格 #RRGGBB（逗号分隔的手写清单也认）
         * @details 整份替换而不是追加：追加会把两次导入变成两份重复的色块；
         *          写坏的那一次保持旧色板，工具栏在下一次打开标注窗时换成新的一份。
         */
        void importPaletteFromFile(const QString &filePath);

    signals:
        /**
         * @brief 动作条或「更多」菜单里点了一步
         * @param action 那一步对应的托盘动作枚举
         * @details 与托盘菜单发的是同一个枚举、界面层也是同一个出口：一条动作要么两处都能做，
         *          要么两处都没了，不允许出现「只在托盘里有」这种在没有托盘的机器上找不到的入口。
         */
        void actionRequested(GyanisScribe::App::TrayAction action);

        /**
         * @brief 点了「整屏截图：<屏名>」那一条
         * @param screenName 要点名抓的那块屏
         * @details 与 actionRequested 分开一条：托盘动作那枚举里不带屏名，
         *          而取值恰好与「光标所在那块」不同的只有这一条路。
         */
        void captureScreenRequested(const QString &screenName);

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
         * @brief 历史页请求打开这条记录成品所在的那个目录
         * @param filePath 选中的那条记录
         */
        void historyOpenFolderRequested(const std::filesystem::path &filePath);

        /// 历史页请求清空整份清单（成品文件不动）
        void historyClearRequested();

        /**
         * @brief 主窗口要落进气泡的一条提示
         * @param message 界面语言的正文（历史页与导入导出共用这一条通道）
         * @param kind 语义种类
         */
        void messageForUser(const QString &message, GyanisScribe::Gui::ToastKind kind);

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
        /// 通用页里「导出 / 导入配置」那一行：两个按钮各开一个文件对话框，活交给上面的两个公开方法做
        QWidget *buildTransferRow();
        /// 标注页里「导出 / 导入色板」那一行：同一对按钮的形状，活交给色板那两个公开方法做
        QWidget *buildPaletteTransferRow();
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
        /// 快捷键页：每个全局动作一行
        QWidget *buildHotkeyPage();
        /// 主题页：配色方案
        QWidget *buildThemePage();

        /**
         * @brief 窗口顶部那条常设动作条
         * @return 装着主操作按钮与「更多」菜单的容器
         * @details 条目直接取自托盘那张表：表里有的这里就有，表里没有的这里也不许多造一份。
         */
        QWidget *buildActionBar();
        /// 关于页
        QWidget *buildAboutPage();
        /// 历史页：缩略图列表与三个出口
        QWidget *buildHistoryPage();

        GyanisScribe::Core::ISettingsStore                                  &m_settings;        ///< 配置存储，不拥有
        GyanisScribe::Gui::ThemeManager                                     &m_theme;           ///< 主题管理器，不拥有
        GyanisScribe::Gui::IconProvider                                     &m_icons;           ///< 图标供给器，不拥有
        GyanisScribe::Platform::PlatformCapabilities                         m_capabilities;    ///< 平台能力表：热键页按它给不给登记入口
        QListWidget                                                         *m_nav   = nullptr; ///< 左侧导航，一行一页
        QStackedWidget                                                      *m_stack = nullptr; ///< 右侧页容器，序号与导航行同解
        std::unique_ptr<GyanisScribe::Plugins::FfmpegAudioDeviceEnumerator>  m_audioDevices;    ///< 录屏页那两路设备清单的来源，一次查询两格共用
        std::unique_ptr<GyanisScribe::Plugins::FfmpegVideoEncoderEnumerator> m_videoEncoders;   ///< 录屏页编码器清单的来源：逐个候选真探一遍
        GyanisScribe::Platform::IPlatformIntegration                        &m_integration;     ///< 平台动作（当前只有开机启动那一格）：能力位可以造假，动作要有真对象
        GyanisScribe::Core::HistoryStore                                    &m_historyStore;    ///< 历史清单，历史页从它读
        // 排在历史后面只为照声明次序初始化（它默认是空的，构造列表里没写它；夹在中间会让后两项的初始化次序与声明不符）
        std::unique_ptr<GyanisScribe::App::UpdateChecker> m_updateChecker;     ///< 检查更新那一条：关于页按一下才发请求，不是启动就联网
        GyanisScribe::Gui::HistoryView                   *m_history = nullptr; ///< 历史页控件，重读清单时刷新它
    };

} // namespace GyanisScribe::Main
