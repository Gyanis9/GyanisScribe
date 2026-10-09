#include "App/MainWindow.h"

#include "Core/Logger.h"
#include "Core/Paths.h"
#include "Ocr/OcrEngineFactory.h"
#include "Settings/ISettingsStore.h"
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

    using GyanisScribe::App::ThemeChoice;

    /// 下拉框的全部行：序号、档位、可见标签三者绑在这一张表上
    constexpr std::array<ThemeChoice, 3> kThemeChoices = {
            ThemeChoice{ThemeKind::Light, "浅色"},
            ThemeChoice{ThemeKind::Dark, "深色"},
            ThemeChoice{ThemeKind::System, "跟随系统"},
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
        editor->setToolTip(QStringLiteral("{}。留空表示用默认目录").arg(title));

        auto *browse = new QPushButton{QStringLiteral("浏览…"), row};
        browse->setIcon(icons.icon("Save"));
        browse->setAccessibleName(QStringLiteral("选择{}的目录").arg(title));

        layout->addWidget(editor);
        layout->addWidget(browse);

        QObject::connect(browse, &QPushButton::clicked, row,
                         [&, editor, title]
                         {
                             const auto picked = QFileDialog::getExistingDirectory(row, QStringLiteral("选择{}").arg(title), editor->text());
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

    QString aboutText(const QString &version)
    {
        // 版本号由调用方从 QApplication::applicationVersion() 传进来：真源只有一处
        return QStringLiteral("智录 GyanisScribe %1\n\n"
                              "截图、标注、贴图、录屏与文字识别。\n"
                              "全局热键：Ctrl+Alt+A 截图，Ctrl+Alt+R 录屏（再按一次停止并保存），Ctrl+Alt+O 识别。\n"
                              "界面图标全部为本项目原创 SVG；第三方组件与其许可证见仓库 docs/ThirdPartyLicenses.md。\n")
                .arg(version);
    }

    MainWindow::MainWindow(ISettingsStore &settings, ThemeManager &theme, IconProvider &icons, QWidget *parent) :
        QWidget(parent), m_settings(settings), m_theme(theme), m_icons(icons)
    {
        setWindowTitle(QStringLiteral("智录 设置"));
        setWindowIcon(icons.icon("App"));

        auto *layout = new QVBoxLayout{this};
        m_tabs       = new QTabWidget{this};
        m_tabs->addTab(buildSettingsPage(), QStringLiteral("设置"));
        m_tabs->addTab(buildAboutPage(), QStringLiteral("关于"));
        layout->addWidget(m_tabs);
        resize(560, 460);
    }

    void MainWindow::open(const MainWindowPage page)
    {
        m_tabs->setCurrentIndex(page == MainWindowPage::Settings ? 0 : 1);
        show();
        raise();
        activateWindow();
    }

    QWidget *MainWindow::buildSettingsPage()
    {
        auto *page   = new QWidget{this};
        auto *layout = new QVBoxLayout{page};

        // 热键组：三个键各一个录制框，写进去之后由 HotkeyManager 的订阅即时重登记
        auto *hotkeyBox = new QGroupBox{QStringLiteral("快捷键"), page};
        auto *hotkeys   = new QFormLayout{hotkeyBox};
        for (const auto [key, title, defaultText]: {std::tuple{GyanisScribe::Settings::kHotkeyCapture, QStringLiteral("截图"), QStringLiteral("Ctrl+Alt+A")},
                                                    std::tuple{GyanisScribe::Settings::kHotkeyRecord, QStringLiteral("录屏"), QStringLiteral("Ctrl+Alt+R")},
                                                    std::tuple{GyanisScribe::Settings::kHotkeyOcr, QStringLiteral("文字识别"), QStringLiteral("Ctrl+Alt+O")}})
        {
            auto *edit = new GyanisScribe::Ui::HotkeyEdit{m_settings, hotkeyBox};
            edit->bindTo(key, title, defaultText);
            hotkeys->addRow(QStringLiteral("{} 的全局热键").arg(title), edit);
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
