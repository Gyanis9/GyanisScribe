#include "App/MainWindow.h"

#include "Core/Logger.h"
#include "Core/Paths.h"
#include "Ocr/OcrEngineFactory.h"
#include "Pick/ColorFormat.h"
#include "Settings/ISettingsStore.h"
#include "History/HistoryRules.h"
#include "History/HistoryView.h"
#include "Settings/SettingsKeys.h"
#include "Ui/HotkeyEdit.h"
#include "Ui/IconProvider.h"
#include "Ui/ThemeManager.h"

#include <QApplication>
#include <QComboBox>
#include <QFileDialog>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QTabWidget>
#include <QVBoxLayout>

#include <array>

#include <filesystem>
#include <format>
#include <string>

namespace
{
    using GyanisScribe::Settings::ISettingsStore;
    using GyanisScribe::Ui::IconProvider;
    using GyanisScribe::Ui::ThemeKind;
    using GyanisScribe::Ui::ThemeManager;
    using GyanisScribe::Ui::AppLanguage;

    using GyanisScribe::App::MainWindowPage;
    using GyanisScribe::App::PageChoice;
    using GyanisScribe::App::ThemeChoice;
    using GyanisScribe::App::LanguageChoice;

    /// 主窗口的全部页：序号、目标页与选项卡标签绑在同一张表上
    constexpr std::array<PageChoice, 3> kPages = {
            PageChoice{MainWindowPage::Settings, "设置"},
            PageChoice{MainWindowPage::About, "关于"},
            PageChoice{MainWindowPage::History, "历史"},
    };

    /// 下拉框的全部行：序号、档位、可见标签三者绑在这一张表上
    constexpr std::array<ThemeChoice, 3> kThemeChoices = {
            ThemeChoice{ThemeKind::Light, "浅色"},
            ThemeChoice{ThemeKind::Dark, "深色"},
            ThemeChoice{ThemeKind::System, "跟随系统"},
    };

    /// 界面语言的全部行：标签用各自语言自称，因此这一张表不进翻译目录
    constexpr std::array<LanguageChoice, 2> kLanguageChoices = {
            LanguageChoice{AppLanguage::Chinese, "中文"},
            LanguageChoice{AppLanguage::English, "English"},
    };

    /**
     * @brief 一行「可写目录」的设置控件
     * @param settings 配置存储
     * @param key 键名
     * @param title 中文名
     * @param fallback 键不存在时显示的默认目录（只作提示，不写回配置）
     * @param icons 图标供给器，浏览按钮的图标由它取
     * @return 归调用方所有的行控件
     */
    QWidget *makePathRow(ISettingsStore &settings, const std::string_view key, const QString &title, const std::filesystem::path &fallback, IconProvider &icons)
    {
        auto      *row     = new QWidget;
        auto      *layout  = new QHBoxLayout{row};
        auto      *editor  = new QLineEdit{row};
        const auto stored  = settings.stringValue(key, QString{});
        const auto display = stored.isEmpty() ? QString::fromLocal8Bit(fallback.string().c_str()) : stored;
        editor->setText(display);
        editor->setClearButtonEnabled(false);
        editor->setAccessibleName(title);
        editor->setToolTip(QStringLiteral("%1。留空表示用默认目录").arg(title));

        auto *browse = new QPushButton{QStringLiteral("浏览…"), row};
        browse->setIcon(icons.icon("Save"));
        browse->setAccessibleName(QStringLiteral("选择%1的目录").arg(title));

        layout->addWidget(editor);
        layout->addWidget(browse);

        QObject::connect(browse, &QPushButton::clicked, row,
                         [&, editor, title]
                         {
                             const auto picked = QFileDialog::getExistingDirectory(row, QStringLiteral("选择%1").arg(title), editor->text());
                             if (!picked.isEmpty())
                             {
                                 editor->setText(picked);
                             }
                         });

        // 失焦即写：设置页没有「保存」按钮，改完就看到效果是需求第五节的要求
        QObject::connect(editor, &QLineEdit::editingFinished, row,
                         [&, editor, key, title]
                         {
                             const auto text = editor->text().trimmed();
                             if (text.isEmpty())
                             {
                                 // 清空表示回到默认：这里不写空串，而是把键删到回落状态，显示也一并回落到默认目录
                                 if (auto written = settings.setValue(key, QString{}); !written)
                                 {
                                     GyanisScribe::Core::Logger::warning(std::format("{} 没能写入：{}", title.toStdString(), written.error().message));
                                 }
                                 editor->setText(QString::fromLocal8Bit(fallback.string().c_str()));
                                 return;
                             }
                             if (auto written = settings.setValue(key, text); !written)
                             {
                                 GyanisScribe::Core::Logger::warning(std::format("{} 没能写入：{}", title.toStdString(), written.error().message));
                             }
                         });
        return row;
    }
} // namespace

namespace GyanisScribe::App
{
    std::span<const PageChoice> pageChoices()
    {
        return kPages;
    }

    int indexOfMainWindowPage(const MainWindowPage page)
    {
        for (std::size_t index = 0; index < kPages.size(); ++index)
        {
            if (kPages[index].page == page)
            {
                return static_cast<int>(index);
            }
        }
        return -1;
    }

    std::span<const ThemeChoice> themeChoices()
    {
        return kThemeChoices;
    }

    std::optional<ThemeKind> themeKindAt(const int index)
    {
        if (index < 0 || static_cast<std::size_t>(index) >= kThemeChoices.size())
        {
            return {};
        }
        return kThemeChoices[static_cast<std::size_t>(index)].kind;
    }

    int indexOfThemeKind(const ThemeKind themeKind)
    {
        for (std::size_t index = 0; index < kThemeChoices.size(); ++index)
        {
            if (kThemeChoices[index].kind == themeKind)
            {
                return static_cast<int>(index);
            }
        }
        return -1;
    }

    std::span<const LanguageChoice> languageChoices()
    {
        return kLanguageChoices;
    }

    std::optional<AppLanguage> appLanguageAt(const int index)
    {
        if (index < 0 || static_cast<std::size_t>(index) >= kLanguageChoices.size())
        {
            return {};
        }
        return kLanguageChoices[static_cast<std::size_t>(index)].language;
    }

    int indexOfAppLanguage(const AppLanguage language)
    {
        for (std::size_t index = 0; index < kLanguageChoices.size(); ++index)
        {
            if (kLanguageChoices[index].language == language)
            {
                return static_cast<int>(index);
            }
        }
        return -1;
    }

    QString aboutText(const QString &version)
    {
        // 版本号由调用方从 QApplication::applicationVersion() 传进来：真源只有一处
        return QStringLiteral("智录 GyanisScribe %1\n\n"
                              "截图、标注、贴图、录屏与文字识别。\n"
                              "全局热键：Ctrl+Alt+A 截图，Ctrl+Alt+R 录屏（再按一次停止并保存），Ctrl+Alt+O 识别。\n"
                              "界面图标全部为本项目原创 SVG；第三方组件与其许可证见仓库 docs/ThirdPartyLicenses.md。\n")
                .arg(version);
    }

    MainWindow::MainWindow(ISettingsStore &settings, ThemeManager &theme, IconProvider &icons, History::HistoryStore &historyStore, QWidget *parent) :
        QWidget(parent), m_settings(settings), m_theme(theme), m_icons(icons), m_historyStore(historyStore)
    {
        setWindowTitle(QStringLiteral("智录 设置"));
        setWindowIcon(icons.icon("App"));

        auto *layout = new QVBoxLayout{this};
        m_tabs       = new QTabWidget{this};
        // 页与标签都从 pageChoices() 现取：这里再抄一份中文就会出现菜单与页签不同名
        m_tabs->addTab(buildSettingsPage(), QString::fromUtf8(kPages[0].label.data(), static_cast<qsizetype>(kPages[0].label.size())));
        m_tabs->addTab(buildAboutPage(), QString::fromUtf8(kPages[1].label.data(), static_cast<qsizetype>(kPages[1].label.size())));
        m_tabs->addTab(buildHistoryPage(), QString::fromUtf8(kPages[2].label.data(), static_cast<qsizetype>(kPages[2].label.size())));
        layout->addWidget(m_tabs);
        resize(560, 460);
    }

    void MainWindow::open(const MainWindowPage page)
    {
        const auto index = indexOfMainWindowPage(page);
        if (index < 0)
        {
            // 页不在表里（比如新页忘了登记）就别切：切到第 0 页会把用户的操作读成「没反应」
            Core::Logger::warning(std::format("主窗口没有这一页（枚举值 {}），已忽略这次跳转", static_cast<int>(page)));
            return;
        }
        m_tabs->setCurrentIndex(index);
        show();
        raise();
        activateWindow();
    }

    QWidget *MainWindow::buildHistoryPage()
    {
        auto *page  = new QWidget{this};
        auto *inner = new QVBoxLayout{page};
        m_history   = new History::HistoryView{m_historyStore, History::policyFromSettings(m_settings), m_icons, page};
        inner->addWidget(m_history);

        // 三个出口原样转给控制器：主窗口不知道贴图与识别各自要做什么
        QObject::connect(m_history, &History::HistoryView::pinRequested, this, &MainWindow::historyPinRequested);
        QObject::connect(m_history, &History::HistoryView::ocrRequested, this, &MainWindow::historyOcrRequested);
        QObject::connect(m_history, &History::HistoryView::removeRequested, this, &MainWindow::historyRemoveRequested);
        QObject::connect(m_history, &History::HistoryView::messageForUser, this, &MainWindow::historyMessage);
        return page;
    }

    void MainWindow::refreshHistory()
    {
        if (m_history != nullptr)
        {
            m_history->refresh();
        }
    }

    QWidget *MainWindow::buildSettingsPage()
    {
        auto *page   = new QWidget{this};
        auto *layout = new QVBoxLayout{page};

        // 热键组：四个动作各一个录制框，写进去之后由 HotkeyManager 的订阅即时重登记
        auto *hotkeyBox = new QGroupBox{QStringLiteral("快捷键"), page};
        auto *hotkeys   = new QFormLayout{hotkeyBox};
        for (const auto [key, title, defaultText]: {std::tuple{GyanisScribe::Settings::kHotkeyCapture, QStringLiteral("截图"), QStringLiteral("Ctrl+Alt+A")},
                                                    std::tuple{GyanisScribe::Settings::kHotkeyRecord, QStringLiteral("录屏"), QStringLiteral("Ctrl+Alt+R")},
                                                    std::tuple{GyanisScribe::Settings::kHotkeyOcr, QStringLiteral("文字识别"), QStringLiteral("Ctrl+Alt+O")},
                                                    std::tuple{GyanisScribe::Settings::kHotkeyPick, QStringLiteral("屏幕取色"), QStringLiteral("Ctrl+Alt+P")}})
        {
            auto *edit = new GyanisScribe::Ui::HotkeyEdit{m_settings, hotkeyBox};
            edit->bindTo(key, title, defaultText);
            hotkeys->addRow(QStringLiteral("%1 的全局热键").arg(title), edit);
        }
        layout->addWidget(hotkeyBox);

        // 目录组：三处路径都给出默认值作提示，留空即回落
        auto *pathBox = new QGroupBox{QStringLiteral("保存位置"), page};
        auto *paths   = new QFormLayout{pathBox};
        paths->addRow(QStringLiteral("截图与标注成品目录"),
                      makePathRow(m_settings, GyanisScribe::Settings::kPathScreenshot, QStringLiteral("截图目录"), Core::Paths::defaultScreenshotDirectory(), m_icons));
        paths->addRow(QStringLiteral("录屏成品目录"),
                      makePathRow(m_settings, GyanisScribe::Settings::kPathRecording, QStringLiteral("录屏目录"), Core::Paths::defaultRecordingDirectory(), m_icons));
        paths->addRow(QStringLiteral("识别语言包目录"), makePathRow(m_settings, GyanisScribe::Settings::kOcrTessDataDirectory, QStringLiteral("语言包目录"),
                                                                    Core::Paths::applicationDataDirectory() / "tessdata", m_icons));
        layout->addWidget(pathBox);

        // 主题组：三档即时生效，不需要重启
        auto *appearanceBox = new QGroupBox{QStringLiteral("主题"), page};
        auto *appearance    = new QFormLayout{appearanceBox};
        auto *themeCombo    = new QComboBox{appearanceBox};
        // 标签来自 themeChoices()：控件里不再抄第二份中文，序号—标签—档位的对应关系才只有一个真源
        for (const auto &choice: themeChoices())
        {
            themeCombo->addItem(QString::fromUtf8(choice.label.data(), static_cast<qsizetype>(choice.label.size())));
        }
        themeCombo->setAccessibleName(QStringLiteral("主题"));
        themeCombo->setCurrentIndex(indexOfThemeKind(m_theme.requestedKind()));
        QObject::connect(themeCombo, &QComboBox::currentIndexChanged, appearanceBox,
                         [this, themeCombo](int index)
                         {
                             if (const auto kind = themeKindAt(index); kind)
                             {
                                 if (auto applied = m_theme.setTheme(*kind); !applied)
                                 {
                                     GyanisScribe::Core::Logger::warning(std::format("主题没能切换：{}", applied.error().message));
                                 }
                             }
                         });
        appearance->addRow(QStringLiteral("配色方案"), themeCombo);

        // 界面语言：档位与自称都在 languageChoices() 上，配置里存的是 token 而不是中文。
        // 语言包在进程启动时就装好了，这里写了值也只影响下一次启动，所以把「要重启」挂在提示里
        auto *uiLanguage = new QComboBox{appearanceBox};
        for (const auto &choice: languageChoices())
        {
            uiLanguage->addItem(QString::fromUtf8(choice.label.data(), static_cast<qsizetype>(choice.label.size())));
        }
        uiLanguage->setAccessibleName(QStringLiteral("界面语言"));
        uiLanguage->setToolTip(QStringLiteral("切换语言要重启智录才生效"));
        const auto storedUiLanguage = m_settings.stringValue(GyanisScribe::Settings::kUiLanguage,
                                                             QString::fromUtf8(std::string{GyanisScribe::Ui::appLanguageText(GyanisScribe::Ui::AppLanguage::Chinese)}));
        if (const auto parsed = GyanisScribe::Ui::parseAppLanguage(storedUiLanguage.toStdString()); parsed)
        {
            uiLanguage->setCurrentIndex(indexOfAppLanguage(*parsed));
        } else
        {
            // 配置里的值读不出时下拉框显示中文，但原因要落进日志：只把界面悄悄拨回默认档，用户会以为是自己选错的
            Core::Logger::warning(std::format("设置页读不出界面语言：{}，下拉框按中文显示", parsed.error().message));
            uiLanguage->setCurrentIndex(indexOfAppLanguage(AppLanguage::Chinese));
        }
        QObject::connect(uiLanguage, &QComboBox::currentIndexChanged, appearanceBox,
                         [this, uiLanguage](int index)
                         {
                             const auto choice = appLanguageAt(index);
                             if (!choice)
                             {
                                 return;
                             }
                             const auto token = QString::fromUtf8(std::string{GyanisScribe::Ui::appLanguageText(*choice)});
                             if (auto written = m_settings.setValue(GyanisScribe::Settings::kUiLanguage, token); !written)
                             {
                                 Core::Logger::warning(std::format("界面语言没能保存：{}（当前选的是 {}）", written.error().message, uiLanguage->currentText().toStdString()));
                             }
                         });
        appearance->addRow(QStringLiteral("界面语言"), uiLanguage);
        layout->addWidget(appearanceBox);

        // 识别组：语言下拉的取值来自后端词表，不在这里另列一份
        auto *ocrBox   = new QGroupBox{QStringLiteral("文字识别"), page};
        auto *ocr      = new QFormLayout{ocrBox};
        auto *language = new QComboBox{ocrBox};
        language->addItems(GyanisScribe::Ocr::supportedOcrLanguageTags());
        language->setAccessibleName(QStringLiteral("识别语言"));
        const auto storedLanguage = m_settings.stringValue(GyanisScribe::Settings::kOcrLanguage, QString::fromUtf8(GyanisScribe::Ocr::kDefaultOcrLanguage));
        if (const int index = language->findText(storedLanguage); index >= 0)
        {
            language->setCurrentIndex(index);
        }
        QObject::connect(language, &QComboBox::currentTextChanged, ocrBox,
                         [this, language](const QString &text)
                         {
                             if (auto written = m_settings.setValue(GyanisScribe::Settings::kOcrLanguage, text); !written)
                             {
                                 GyanisScribe::Core::Logger::warning(
                                         std::format("识别语言没能保存：{}（当前选的是 {}）", written.error().message, language->currentText().toStdString()));
                             }
                         });
        ocr->addRow(QStringLiteral("识别语言"), language);
        layout->addWidget(ocrBox);

        // 取色组：写法下拉的项名与序号都来自 Pick 那张表，配置里存的是标记而不是中文
        auto *pickBox    = new QGroupBox{QStringLiteral("屏幕取色"), page};
        auto *pick       = new QFormLayout{pickBox};
        auto *pickFormat = new QComboBox{pickBox};
        pickFormat->addItems(GyanisScribe::Pick::colorFormatLabels());
        pickFormat->setAccessibleName(QStringLiteral("颜色写法"));
        const auto storedFormat = GyanisScribe::Pick::parseColorFormat(m_settings.stringValue(GyanisScribe::Settings::kPickFormat, QStringLiteral("Hex")).toStdString());
        pickFormat->setCurrentIndex(GyanisScribe::Pick::indexOfColorFormat(storedFormat ? *storedFormat : GyanisScribe::Pick::ColorFormat::Hex));
        QObject::connect(pickFormat, &QComboBox::currentIndexChanged, pickBox,
                         [this, pickFormat](int index)
                         {
                             const auto format = GyanisScribe::Pick::colorFormatAt(index);
                             if (!format)
                             {
                                 return;
                             }
                             const auto token = QString::fromUtf8(GyanisScribe::Pick::colorFormatToken(*format).data(),
                                                                  static_cast<qsizetype>(GyanisScribe::Pick::colorFormatToken(*format).size()));
                             if (auto written = m_settings.setValue(GyanisScribe::Settings::kPickFormat, token); !written)
                             {
                                 GyanisScribe::Core::Logger::warning(std::format("颜色写法没能保存：{}", written.error().message));
                             }
                         });
        pick->addRow(QStringLiteral("复制用的写法"), pickFormat);
        layout->addWidget(pickBox);

        layout->addStretch();
        return page;
    }

    QWidget *MainWindow::buildAboutPage()
    {
        auto *page   = new QWidget{this};
        auto *layout = new QVBoxLayout{page};

        auto *logo = new QLabel{page};
        logo->setPixmap(m_icons.pixmap("AppIcon", GyanisScribe::Ui::IconSize::ExtraLarge));
        logo->setAccessibleName(QStringLiteral("智录图标"));

        auto *text = new QLabel{aboutText(QApplication::applicationVersion()), page};
        text->setWordWrap(true);
        text->setTextInteractionFlags(Qt::TextSelectableByMouse);

        layout->addWidget(logo);
        layout->addWidget(text);
        layout->addStretch();
        return page;
    }

} // namespace GyanisScribe::App
