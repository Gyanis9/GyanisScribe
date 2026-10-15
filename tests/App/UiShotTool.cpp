// 文档出图工具：离屏起一份真主窗口（真主题、真图标、临时配置与临时历史），逐页拍成 PNG。
// 与 CrashProbe 同一条理由不是产品的一部分：不进安装包、不挂任何产品入口，只给文档出图。
// 跑法：UiShotTool <输出目录>；产图文件名与页面对应关系见下面 kShots 那张表。

#include "App/MainWindow.h"

#include "History/HistoryStore.h"
#include "Platform/IPlatformIntegration.h"
#include "Settings/SettingsKeys.h"
#include "Settings/SettingsStore.h"
#include "Ui/IconProvider.h"
#include "Ui/ThemeManager.h"

#include <QApplication>
#include <QCoreApplication>
#include <QFont>
#include <QFontDatabase>
#include <QString>

#include <array>
#include <cstdio>
#include <filesystem>
#include <format>
#include <memory>

namespace
{
    using GyanisScribe::App::MainWindowPage;

    /// 一页要拍的图：页枚举与产图文件名成对，数组顺序就是拍摄顺序
    struct ShotPage
    {
        MainWindowPage page; ///< 拍哪一页
        const char    *name; ///< 产图文件名（不带扩展名）
    };

    /// 全部要拍的主窗口页；加了新页想进文档就往这张表里补一行
    constexpr std::array<ShotPage, 10> kShots{{
            {MainWindowPage::General, "main-general"},
            {MainWindowPage::Capture, "main-capture"},
            {MainWindowPage::Annotation, "main-annotation"},
            {MainWindowPage::Pin, "main-pin"},
            {MainWindowPage::Record, "main-record"},
            {MainWindowPage::Ocr, "main-ocr"},
            {MainWindowPage::Hotkey, "main-hotkey"},
            {MainWindowPage::Theme, "main-theme"},
            {MainWindowPage::History, "main-history"},
            {MainWindowPage::About, "main-about"},
    }};
} // namespace

int main(int argc, char *argv[])
{
    // 离屏平台要排在 QApplication 之前：出图不该在用户的桌面上闪一串窗口
    qputenv("QT_QPA_PLATFORM", "offscreen");

    QApplication application(argc, argv);
    QCoreApplication::setOrganizationName(QStringLiteral("Gyanis"));
    QCoreApplication::setApplicationName(QStringLiteral("GyanisScribe-Shots"));

    // 离屏平台上 Qt 找不到系统字体，字形会全渲染成方框：把系统里那份中文字体显式装进来，
    // 并把全局字体指向它。装不上就当场失败，不出一摞方框图冒充界面图。
    const int fontId = QFontDatabase::addApplicationFont(QStringLiteral("C:/Windows/Fonts/msyh.ttc"));
    if (fontId == -1)
    {
        std::fprintf(stderr, "failed to register a UI font: text would render as tofu\n");
        return 1;
    }
    QApplication::setFont(QFont(QFontDatabase::applicationFontFamilies(fontId).front(), 9));

    const auto      outputDirectory = argc > 1 ? std::filesystem::path(argv[1]) : std::filesystem::current_path() / "shots";
    std::error_code ignored;
    std::filesystem::create_directories(outputDirectory, ignored);

    // 配置与历史都落在临时目录：出图不该碰到这台机器上真实的那两份
    const auto dataDirectory = std::filesystem::temp_directory_path() / "GyanisScribe-Shots";
    std::filesystem::remove_all(dataDirectory, ignored);
    std::filesystem::create_directories(dataDirectory, ignored);

    GyanisScribe::Settings::SettingsStore settings(dataDirectory / "settings.ini");
    // 目录几格给一条中性占位，别把出图这台机器的真实路径（用户名、临时目录）印进文档图里
    const auto placeholdersSeeded =
            settings.setValue(GyanisScribe::Settings::kPathScreenshot, QStringLiteral("C:/Users/Example/Pictures/GyanisScribe")) &&
            settings.setValue(GyanisScribe::Settings::kPathRecording, QStringLiteral("C:/Users/Example/Videos/GyanisScribe")) &&
            settings.setValue(GyanisScribe::Settings::kOcrTessDataDirectory, QStringLiteral("C:/Users/Example/AppData/Roaming/Gyanis/GyanisScribe/tessdata"));
    if (!placeholdersSeeded)
    {
        std::fprintf(stderr, "failed to seed placeholder paths\n");
        return 1;
    }

    GyanisScribe::Ui::IconProvider      icons;
    GyanisScribe::Ui::ThemeManager      theme(settings);
    GyanisScribe::History::HistoryStore history(dataDirectory / "history.json");
    const auto                          integration = GyanisScribe::Platform::createPlatformIntegration();
    // 样式表要落一次：真实程序里这一步在 AppController 构造里做，这里没有控制器
    theme.applyFromSettings();

    GyanisScribe::App::MainWindow window(settings, theme, icons, history, integration->capabilities(), *integration);
    window.resize(1000, 680);

    for (const auto &shot: kShots)
    {
        window.open(shot.page);
        // 让布局与第一帧画完之后再拍：抓的是控件自己渲染的那一份，与屏幕上看到的是同一套画法
        QCoreApplication::processEvents();

        const auto pixmap   = window.grab();
        const auto filePath = outputDirectory / std::format("{}.png", shot.name);
        if (!pixmap.save(QString::fromUtf8(filePath.string().c_str())))
        {
            std::fprintf(stderr, "save failed: %s\n", filePath.string().c_str());
            return 1;
        }
        std::printf("%s\n", filePath.string().c_str());
    }

    return 0;
}
