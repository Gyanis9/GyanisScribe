// 界面审查出图工具：离屏起真部件逐面拍 PNG，并把 sizeHint / minimumSizeHint / 实际几何打进 stdout。
// 证据的去向是仓外 GyanisScribe-Reports/ 里那份界面审查台账。
// 与 UiShotTool 并列而不合并：那一份服务 README 的十张文档图（产物要进 docs/images），
// 这一份服务审查台账（产物只进临时目录）；两份拍摄清单会各自长，共用一份配置反而互相牵制。
// 与 CrashProbe / UiShotTool 同一条理由不是产品的一部分：不进安装包、不挂产品入口。
// 跑法：UiAuditTool <输出目录>；stdout 每行是「图名: 实际尺寸 sizeHint minimumSizeHint」。

#include "Annotation/AnnotationPalette.h"
#include "Annotation/AnnotationToolbar.h"
#include "Annotation/AnnotationToolbarGeometry.h"
#include "App/MainWindow.h"
#include "History/HistoryStore.h"
#include "Pick/RecentColors.h"
#include "Platform/IPlatformIntegration.h"
#include "Settings/SettingsKeys.h"
#include "Settings/SettingsStore.h"
#include "Gui/Resources/IconProvider.h"
#include "Gui/Theme/ThemeManager.h"

#include <QApplication>
#include <QCoreApplication>
#include <QFont>
#include <QFontDatabase>
#include <QScrollArea>
#include <QStackedWidget>
#include <QString>
#include <QWidget>

#include <array>
#include <cstdio>
#include <filesystem>
#include <format>
#include <memory>
#include <utility>
#include <vector>

namespace
{
    /**
     * @brief 抓一张图并把尺寸读数打进 stdout
     * @param widget 已经 show 过的控件（隐藏的窗口上布局不给子控件算位置）
     * @param outputDirectory 出图目录
     * @param name 图名（也是读数行里的标签）
     */
    void shoot(QWidget &widget, const std::filesystem::path &outputDirectory, const char *name)
    {
        // 换肤/换档之后样式是整片重抛的：多泵几轮事件并强制重绘一次，
        // 免得抓到半新半旧的帧（实测只泵一次会拍到底部一条旧底的带子）
        for (int round = 0; round < 3; ++round)
        {
            QCoreApplication::processEvents();
        }
        widget.repaint();
        QCoreApplication::processEvents();
        const auto pixmap   = widget.grab();
        const auto filePath = outputDirectory / std::format("{}.png", name);
        pixmap.save(QString::fromUtf8(filePath.string().c_str()));
        const auto hint    = widget.sizeHint();
        const auto minimum = widget.minimumSizeHint();
        std::printf("%s: actual=%dx%d sizeHint=%dx%d minimumSizeHint=%dx%d\n", name, widget.width(), widget.height(), hint.width(), hint.height(), minimum.width(),
                    minimum.height());
    }

    /// 16 格满配色板：按色相环均匀取，每一格彼此分得开，量宽度时不受同色观感影响
    std::vector<QColor> fullPalette()
    {
        std::vector<QColor> palette;
        const auto          step = 360 / static_cast<int>(GyanisScribe::Annotation::kPaletteCapacity);
        for (int index = 0; index < static_cast<int>(GyanisScribe::Annotation::kPaletteCapacity); ++index)
        {
            palette.push_back(QColor::fromHsv((index * step) % 360, 200, 220));
        }
        return palette;
    }

    /// 6 格最近使用色：与色板区分开的一组深色
    std::vector<QColor> fullRecentColors()
    {
        std::vector<QColor> recent;
        for (int index = 0; index < static_cast<int>(GyanisScribe::Pick::kRecentColorCapacity); ++index)
        {
            recent.push_back(QColor::fromHsv(210 - index * 12, 160 + index * 8, 120 + index * 16));
        }
        return recent;
    }
} // namespace

int main(int argc, char *argv[])
{
    // 离屏平台要排在 QApplication 之前：出图不该在用户的桌面上闪一串窗口
    qputenv("QT_QPA_PLATFORM", "offscreen");

    QApplication application(argc, argv);
    QCoreApplication::setOrganizationName(QStringLiteral("Gyanis"));
    QCoreApplication::setApplicationName(QStringLiteral("GyanisScribe-Audit"));

    // 与 UiShotTool 同一份理由：离屏平台上 Qt 找不到系统字体，中文会渲染成方框。
    // 装不上就当场失败，不出一摞方框图冒充界面图
    const int fontId = QFontDatabase::addApplicationFont(QStringLiteral("C:/Windows/Fonts/msyh.ttc"));
    if (fontId == -1)
    {
        std::fprintf(stderr, "failed to register a UI font: text would render as tofu\n");
        return 1;
    }
    QApplication::setFont(QFont(QFontDatabase::applicationFontFamilies(fontId).front(), 9));

    const auto      outputDirectory = argc > 1 ? std::filesystem::path(argv[1]) : std::filesystem::current_path() / "audit-shots";
    std::error_code ignored;
    std::filesystem::create_directories(outputDirectory, ignored);

    // 配置与历史都落在临时目录：审查出图不该碰到这台机器上真实的那两份
    const auto dataDirectory = std::filesystem::temp_directory_path() / "GyanisScribe-Audit";
    std::filesystem::remove_all(dataDirectory, ignored);
    std::filesystem::create_directories(dataDirectory, ignored);

    GyanisScribe::Settings::SettingsStore settings(dataDirectory / "settings.ini");
    GyanisScribe::Gui::IconProvider        icons;
    GyanisScribe::Gui::ThemeManager        theme(settings);
    // 样式表要落一次：真实程序里这一步在 AppController 构造里做，这里没有控制器
    theme.applyFromSettings();

    // 面 1：标注工具栏默认态（设置里没存过色板，= 全新安装那一份）
    {
        GyanisScribe::Annotation::AnnotationToolbar toolbar{settings, icons, nullptr};
        toolbar.show();
        shoot(toolbar, outputDirectory, "annotation-toolbar-default");

        // 与摆放判据对表：常见屏宽下 frame 宽会小于工具栏最小宽的那些，右端按钮落在屏外。
        // 用纯函数算而不是 setGeometry：Qt 会把小于最小尺寸的几何静默保回最小宽，看不出被切
        const auto hint    = toolbar.sizeHint();
        const auto minimum = toolbar.minimumSizeHint();
        for (const int screenWidth: {2560, 2048, 1920, 1536, 1280})
        {
            const QRect available{0, 0, screenWidth, 1040};
            const auto  placed = GyanisScribe::Annotation::placeToolbar(available, hint, QRect{200, 200, 900, 600});
            if (!placed)
            {
                std::printf("place @%d: failed (%s)\n", screenWidth, placed.error().message.c_str());
                continue;
            }
            std::printf("place @%d: frame w=%d; toolbar min w=%d -> %s\n", screenWidth, placed->frame.width(), minimum.width(),
                        placed->frame.width() < minimum.width() ? "RIGHT SIDE CLIPPED" : "fits");
        }

        // 每种工具收放可见段之后的宽度：台账 B 条「收敛后最宽是哪一档」的读数与对照图
        for (std::size_t index = 0; index < GyanisScribe::Annotation::kToolTable.size(); ++index)
        {
            toolbar.setActiveTool(std::get<0>(GyanisScribe::Annotation::kToolTable[index]));
            QCoreApplication::processEvents();
            shoot(toolbar, outputDirectory, std::format("annotation-toolbar-{}", std::get<1>(GyanisScribe::Annotation::kToolTable[index])).c_str());
        }

        // 文字工具的两个子档：描边开着多两枚（描边色/描边粗细）、底板开着多一枚（底板色）
        toolbar.setActiveTool(GyanisScribe::Annotation::ToolKind::Text);
        toolbar.setTextOutline(true);
        shoot(toolbar, outputDirectory, "annotation-toolbar-text-outline");
        toolbar.setTextBackground(true);
        shoot(toolbar, outputDirectory, "annotation-toolbar-text-outline-background");
    }

    // 面 2：标注工具栏满配态——16 格色板 + 6 格最近色，这是它能长到的最宽形状
    {
        const auto paletteSeeded = settings.setValue(GyanisScribe::Settings::kAnnotationPalette, GyanisScribe::Annotation::paletteToTokens(fullPalette()));
        const auto recentSeeded  = settings.setValue(GyanisScribe::Settings::kAnnotationRecentColors, GyanisScribe::Pick::serializeRecentColors(fullRecentColors()));
        if (!paletteSeeded || !recentSeeded)
        {
            std::fprintf(stderr, "failed to seed the full palette\n");
            return 1;
        }
        GyanisScribe::Annotation::AnnotationToolbar toolbar{settings, icons, nullptr};
        toolbar.show();
        shoot(toolbar, outputDirectory, "annotation-toolbar-full-palette");
    }

    // 面 3：主窗口初始尺寸档（MainWindow 构造里 resize(680, 560)，实际被最小宽顶住，读数行会报真实值）
    {
        GyanisScribe::History::HistoryStore history(dataDirectory / "history.json");
        const auto                          integration = GyanisScribe::Platform::createPlatformIntegration();
        GyanisScribe::App::MainWindow       window(settings, theme, icons, history, integration->capabilities(), *integration);
        window.resize(680, 560);
        for (const auto page: {GyanisScribe::App::MainWindowPage::General, GyanisScribe::App::MainWindowPage::Capture})
        {
            window.open(page);
            shoot(window, outputDirectory, page == GyanisScribe::App::MainWindowPage::General ? "main-general-default" : "main-capture-default");
        }
        // 快捷键页整页：那一页比窗口高，分组标题行在折叠之下——抓滚动区里那张卡片（内容全高）才看得到
        window.open(GyanisScribe::App::MainWindowPage::Hotkey);
        if (auto *stack = window.findChild<QStackedWidget *>(); stack != nullptr)
        {
            if (auto *scroll = qobject_cast<QScrollArea *>(stack->currentWidget()); scroll != nullptr && scroll->widget() != nullptr)
            {
                shoot(*scroll->widget(), outputDirectory, "page-hotkey-full");
            }
        }

        // 面 4：深色与高对比度档的对应图（换肤观感核对；浅色已在前面出过）
        for (const auto &[mode, name]: std::array<std::pair<const char *, const char *>, 2>{{{"Dark", "dark"}, {"HighContrast", "hc"}}})
        {
            if (const auto written = settings.setValue(GyanisScribe::Settings::kThemeMode, QString::fromUtf8(mode)); !written)
            {
                std::fprintf(stderr, "failed to switch the theme\n");
                return 1;
            }
            theme.applyFromSettings();
            window.open(GyanisScribe::App::MainWindowPage::General);
            shoot(window, outputDirectory, std::format("theme-{}-general", name).c_str());
            GyanisScribe::Annotation::AnnotationToolbar toolbar{settings, icons, nullptr};
            toolbar.show();
            shoot(toolbar, outputDirectory, std::format("theme-{}-toolbar", name).c_str());
        }
    }

    return 0;
}
