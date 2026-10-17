// 界面审查出图工具：离屏起真部件逐面拍 PNG，并把 sizeHint / minimumSizeHint / 实际几何打进 stdout。
// 证据的去向是仓外 GyanisScribe-Reports/ 里那份界面审查台账。
// 与 UiShotTool 并列而不合并：那一份服务 README 的十张文档图（产物要进 docs/images），
// 这一份服务审查台账（产物只进临时目录）；两份拍摄清单会各自长，共用一份配置反而互相牵制。
// 与 CrashProbe / UiShotTool 同一条理由不是产品的一部分：不进安装包、不挂产品入口。
// 跑法：UiAuditTool <输出目录>；stdout 每行是「图名: 实际尺寸 sizeHint minimumSizeHint」。

#include "App/TrayController.h"
#include "Base/Text.h"
#include "Core/Annotation/AnnotationPalette.h"
#include "Gui/Annotation/AnnotationToolbar.h"
#include "Gui/Annotation/AnnotationToolbarGeometry.h"
#include "Main/MainWindow.h"
#include "Core/History/HistoryRules.h"
#include "Core/History/HistoryStore.h"
#include "Core/History/HistoryTypes.h"
#include "Core/Ocr/OcrTypes.h"
#include "Core/Pick/RecentColors.h"
#include "Core/Record/RecordingSession.h"
#include "Gui/Capture/CaptureOverlay.h"
#include "Gui/Controls/Toast.h"
#include "Gui/Ocr/OcrResultWindow.h"
#include "Gui/Pin/PinGroupManagerWindow.h"
#include "Gui/Pin/PinManager.h"
#include "Gui/Pin/PinWindow.h"
#include "Gui/Record/RecordingHud.h"
#include "Platform/IPlatformIntegration.h"
#include "Core/Settings/SettingsKeys.h"
#include "Core/Settings/SettingsStore.h"
#include "Gui/Resources/IconProvider.h"
#include "Gui/Theme/ThemeManager.h"

#include <QApplication>
#include <QCoreApplication>
#include <QDateTime>
#include <QFont>
#include <QFontDatabase>
#include <QFormLayout>
#include <QMenu>
#include <QMouseEvent>
#include <QSystemTrayIcon>
#include <QPainter>
#include <QScrollArea>
#include <QStackedWidget>
#include <QString>
#include <QWidget>

#include <array>
#include <cstdio>
#include <filesystem>
#include <format>
#include <fstream>
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
        const auto          step = 360 / static_cast<int>(GyanisScribe::Core::kPaletteCapacity);
        for (int index = 0; index < static_cast<int>(GyanisScribe::Core::kPaletteCapacity); ++index)
        {
            palette.push_back(QColor::fromHsv((index * step) % 360, 200, 220));
        }
        return palette;
    }

    /// 6 格最近使用色：与色板区分开的一组深色
    std::vector<QColor> fullRecentColors()
    {
        std::vector<QColor> recent;
        for (int index = 0; index < static_cast<int>(GyanisScribe::Core::kRecentColorCapacity); ++index)
        {
            recent.push_back(QColor::fromHsv(210 - index * 12, 160 + index * 8, 120 + index * 16));
        }
        return recent;
    }

    /// 一张「像屏幕」的假帧：深底 + 三块面板 + 两行字，遮罩读数与放大镜看得到内容
    QImage syntheticScreen(const QSize &size)
    {
        QImage   image{size, QImage::Format_ARGB32_Premultiplied};
        QPainter painter{&image};
        painter.fillRect(image.rect(), QColor(32, 38, 46));
        painter.fillRect(QRect{48, 72, 360, 200}, QColor(52, 60, 70));
        painter.fillRect(QRect{456, 300, 480, 260}, QColor(70, 80, 92));
        painter.fillRect(QRect{640, 96, 300, 120}, QColor(45, 52, 61));
        painter.setPen(QColor(226, 232, 240));
        painter.drawText(QRect{48, 72, 360, 200}, Qt::AlignCenter, QStringLiteral("preview · 窗口区"));
        painter.drawText(QRect{456, 300, 480, 260}, Qt::AlignCenter, QStringLiteral("panel 420×260"));
        return image;
    }

    /// Toast 的气泡是顶层窗（无障碍名「提示」）：显示之后按名字找到它再抓图
    QWidget *toastBubble()
    {
        for (auto *widget: QApplication::topLevelWidgets())
        {
            if (widget->accessibleName() == GyanisScribe::Base::uiText("提示"))
            {
                return widget;
            }
        }
        return nullptr;
    }

    /**
     * @brief 浮窗族逐面出图：遮罩（默认/放大镜）、贴图窗（默认/悬停）、OCR 结果窗、录屏控制条（四态）、Toast（三类）
     * @param prefix 文件名前缀（主题档接在名字前，三趟互不覆盖）
     * @details 构造与生产同路：真 settings / 真图标 / 先按档 applyFromSettings 再建控件；离屏 show + processEvents 后 grab。
     *          每趟自成一批、控件随作用域销毁——跨档复用一批控件会把上一档的样式残留带进来。
     */
    void shootFloatingFaces(GyanisScribe::Core::SettingsStore &settings, GyanisScribe::Gui::IconProvider &icons, const GyanisScribe::Gui::ThemeManager &theme,
                            GyanisScribe::Platform::IPlatformIntegration &integration, const std::filesystem::path &outputDirectory, const std::string &prefix)
    {
        const QRect screen{0, 0, 1280, 720};

        // 截屏遮罩：默认态 + 放大镜态（放大镜要收到第一次鼠标移动才现身）+ 拖出选区态（2px 框与八角抓点的形状看得清）
        {
            GyanisScribe::Gui::CaptureOverlay overlay{syntheticScreen(screen.size()), screen, 1.0, settings};
            overlay.show();
            shoot(overlay, outputDirectory, (prefix + "overlay-default").c_str());
            QMouseEvent moved{QEvent::MouseMove, QPointF{640, 360}, QPointF{640, 360}, Qt::NoButton, Qt::NoButton, Qt::KeyboardModifiers{}};
            QApplication::sendEvent(&overlay, &moved);
            shoot(overlay, outputDirectory, (prefix + "overlay-magnifier").c_str());

            const QPointF pressPoint{200, 150};
            const QPointF dragPoint{900, 560};
            QMouseEvent  pressed{QEvent::MouseButtonPress, pressPoint, pressPoint, Qt::LeftButton, Qt::LeftButton, Qt::KeyboardModifiers{}};
            QApplication::sendEvent(&overlay, &pressed);
            QMouseEvent dragged{QEvent::MouseMove, dragPoint, dragPoint, Qt::NoButton, Qt::LeftButton, Qt::KeyboardModifiers{}};
            QApplication::sendEvent(&overlay, &dragged);
            QMouseEvent released{QEvent::MouseButtonRelease, dragPoint, dragPoint, Qt::LeftButton, Qt::NoButton, Qt::KeyboardModifiers{}};
            QApplication::sendEvent(&overlay, &released);
            shoot(overlay, outputDirectory, (prefix + "overlay-selection").c_str());
        }

        // 贴图窗：默认 + 悬停（悬停出边框）
        {
            GyanisScribe::Gui::PinWindow pin{syntheticScreen({480, 320}), settings, integration};
            pin.show();
            shoot(pin, outputDirectory, (prefix + "pin-window").c_str());
            QEvent entered{QEvent::Enter};
            QApplication::sendEvent(&pin, &entered);
            shoot(pin, outputDirectory, (prefix + "pin-window-hover").c_str());
        }

        // 贴图组管理窗
        {
            GyanisScribe::Gui::PinManager              pins{settings, integration};
            GyanisScribe::Gui::PinGroupManagerWindow   groups{pins};
            groups.show();
            shoot(groups, outputDirectory, (prefix + "pin-group-manager").c_str());
        }

        // 工具栏摆放三景（复合图）：正常选区（下方）/ 选区贴底（翻上方）/ 极小贴底（两侧都紧）
        {
            const auto shootPlacement = [&](const QRect &selection, const char *name)
            {
                const auto placed = GyanisScribe::Gui::placeToolbar(screen, QSize{1356, 51}, selection);
                if (!placed)
                {
                    std::fprintf(stderr, "placement failed: %s\n", placed.error().message.c_str());
                    return;
                }
                GyanisScribe::Gui::CaptureOverlay overlay{syntheticScreen(screen.size()), screen, 1.0, settings};
                overlay.show();
                QMouseEvent moved{QEvent::MouseMove, QPointF{640, 360}, QPointF{640, 360}, Qt::NoButton, Qt::NoButton, Qt::KeyboardModifiers{}};
                QApplication::sendEvent(&overlay, &moved);
                QCoreApplication::processEvents();
                QImage composite = overlay.grab().toImage();
                {
                    GyanisScribe::Gui::AnnotationToolbar toolbar{settings, icons, nullptr};
                    toolbar.show();
                    QCoreApplication::processEvents();
                    QPainter painter{&composite};
                    painter.setPen(QPen{QColor(35, 109, 233), 2});
                    painter.drawRect(selection);
                    painter.drawPixmap(placed->frame.topLeft(), toolbar.grab());
                }
                composite.save(QString::fromUtf8((outputDirectory / std::format("{}{}.png", prefix, name)).string().c_str()));
                std::printf("%s%s: frame=%d,%d %dx%d side=%d（选区 %d,%d %dx%d）\n", prefix.c_str(), name, placed->frame.x(), placed->frame.y(), placed->frame.width(),
                            placed->frame.height(), static_cast<int>(placed->side), selection.x(), selection.y(), selection.width(), selection.height());
            };
            // 拖拽没真实鼠标可重放：用合成选区 + 真摆放函数，蓝框把选区画在复合图上当参照。
            // 三景各打一条分支：正常放得下（下方）/ 贴底（翻上方）/ 上下都放不下（挑空隙大的一侧贴边）
            shootPlacement(QRect{200, 120, 900, 400}, "placement-below");
            shootPlacement(QRect{200, 560, 900, 130}, "placement-above");
            shootPlacement(QRect{200, 20, 900, 660}, "placement-cramped");
        }

        // OCR 结果窗：两行带词框的结果 + 平均置信度
        {
            GyanisScribe::Core::OcrResult result;
            result.language          = QStringLiteral("zh-Hans");
            result.averageConfidence = 0.93;
            const auto addLine = [&result](const QString &text, const QRect &box)
            {
                GyanisScribe::Core::OcrLine line;
                line.boundingBox = box;
                line.words.push_back({text, box});
                result.lines.push_back(line);
            };
            addLine(QStringLiteral("已保存到截图目录"), QRect{24, 28, 360, 34});
            addLine(QStringLiteral("Saved to the screenshot folder"), QRect{24, 74, 420, 30});
            GyanisScribe::Gui::OcrResultWindow window{syntheticScreen({720, 480}), result, icons, theme};
            window.show();
            shoot(window, outputDirectory, (prefix + "ocr-result-window").c_str());
        }

        // 录屏控制条：倒计时 / 在录（9 秒与 605 秒两档看秒数抖不抖宽）/ 暂停
        {
            GyanisScribe::Gui::RecordingHud hud{icons, screen};
            hud.show();
            hud.applyState(GyanisScribe::Core::RecordingState::Countdown, 3, 0);
            shoot(hud, outputDirectory, (prefix + "hud-countdown").c_str());
            hud.applyState(GyanisScribe::Core::RecordingState::Recording, 0, 9);
            shoot(hud, outputDirectory, (prefix + "hud-recording-9s").c_str());
            hud.applyState(GyanisScribe::Core::RecordingState::Recording, 0, 605);
            shoot(hud, outputDirectory, (prefix + "hud-recording-605s").c_str());
            hud.applyState(GyanisScribe::Core::RecordingState::Paused, 0, 605);
            shoot(hud, outputDirectory, (prefix + "hud-paused").c_str());
        }

        // Toast：三类各拍一张气泡本体
        {
            GyanisScribe::Gui::Toast toast{icons, theme};
            const auto              shootToast = [&](const GyanisScribe::Gui::ToastKind kind, const char *name, const QString &text)
            {
                toast.show(text, kind);
                if (auto *bubble = toastBubble(); bubble != nullptr)
                {
                    shoot(*bubble, outputDirectory, (prefix + "toast-" + name).c_str());
                }
            };
            shootToast(GyanisScribe::Gui::ToastKind::Information, "information", QStringLiteral("正在识别这一张…"));
            shootToast(GyanisScribe::Gui::ToastKind::Success, "success", QStringLiteral("已保存到截图目录"));
            shootToast(GyanisScribe::Gui::ToastKind::Danger, "danger", QStringLiteral("截图失败：屏幕已不在"));
        }
    }
} // namespace

int main(int argc, char *argv[])
{
    // 离屏平台要排在 QApplication 之前：出图不该在用户的桌面上闪一串窗口。
    // 托盘菜单那一面是唯一的例外：离屏没有系统托盘，只能显式要它时换真平台
    const bool liveTray = qEnvironmentVariableIsSet("GYANISSCRIBE_UI_AUDIT_LIVE_TRAY");
    if (!liveTray)
    {
        qputenv("QT_QPA_PLATFORM", "offscreen");
    }

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

    GyanisScribe::Core::SettingsStore settings(dataDirectory / "settings.ini");
    GyanisScribe::Gui::IconProvider   icons;
    GyanisScribe::Gui::ThemeManager   theme(settings);
    // 样式表要落一次：真实程序里这一步在 AppController 构造里做，这里没有控制器
    theme.applyFromSettings();

    // 面 0（只在显式要真托盘时跑）：托盘右键菜单的真渲染。菜单本体是 QMenu，真平台下按真图标、
    // 真主题、真字体排出来；取 contextMenu() 直接抓，不经过系统托盘的鼠标动作
    if (liveTray)
    {
        auto integration = GyanisScribe::Platform::createPlatformIntegration();
        GyanisScribe::App::TrayController tray{icons, theme, settings, *integration};
        if (!tray.install())
        {
            std::fprintf(stderr, "live tray requested but this shell has no system tray\n");
            return 1;
        }
        auto *icon = tray.findChild<QSystemTrayIcon *>();
        auto *menu = icon != nullptr ? icon->contextMenu() : nullptr;
        if (menu == nullptr)
        {
            std::fprintf(stderr, "live tray requested but the context menu was not built\n");
            return 1;
        }
        menu->ensurePolished();
        menu->adjustSize();
        const auto filePath = outputDirectory / "tray-menu-live.png";
        if (!menu->grab().save(QString::fromStdWString(filePath.wstring())))
        {
            std::fprintf(stderr, "failed to save the tray menu image\n");
            return 1;
        }
        std::printf("tray-menu: %dx%d\n", menu->width(), menu->height());
        for (auto *action: menu->actions())
        {
            std::printf("tray-menu row: %s | icon=%d | enabled=%d | h=%d\n", action->text().toStdString().c_str(), action->isSeparator() ? 0 : !action->icon().isNull(),
                        static_cast<int>(action->isEnabled()), menu->actionGeometry(action).height());
        }
        return 0;
    }

    // 面 1：标注工具栏默认态（设置里没存过色板，= 全新安装那一份）
    {
        GyanisScribe::Gui::AnnotationToolbar toolbar{settings, icons, nullptr};
        toolbar.show();
        shoot(toolbar, outputDirectory, "annotation-toolbar-default");

        // 与摆放判据对表：常见屏宽下 frame 宽会小于工具栏最小宽的那些，右端按钮落在屏外。
        // 用纯函数算而不是 setGeometry：Qt 会把小于最小尺寸的几何静默保回最小宽，看不出被切
        const auto hint    = toolbar.sizeHint();
        const auto minimum = toolbar.minimumSizeHint();
        for (const int screenWidth: {2560, 2048, 1920, 1536, 1280})
        {
            const QRect available{0, 0, screenWidth, 1040};
            const auto  placed = GyanisScribe::Gui::placeToolbar(available, hint, QRect{200, 200, 900, 600});
            if (!placed)
            {
                std::printf("place @%d: failed (%s)\n", screenWidth, placed.error().message.c_str());
                continue;
            }
            std::printf("place @%d: frame w=%d; toolbar min w=%d -> %s\n", screenWidth, placed->frame.width(), minimum.width(),
                        placed->frame.width() < minimum.width() ? "RIGHT SIDE CLIPPED" : "fits");
        }

        // 每种工具收放可见段之后的宽度：台账 B 条「收敛后最宽是哪一档」的读数与对照图
        for (std::size_t index = 0; index < GyanisScribe::Gui::kToolTable.size(); ++index)
        {
            toolbar.setActiveTool(std::get<0>(GyanisScribe::Gui::kToolTable[index]));
            QCoreApplication::processEvents();
            shoot(toolbar, outputDirectory, std::format("annotation-toolbar-{}", std::get<1>(GyanisScribe::Gui::kToolTable[index])).c_str());
        }

        // 文字工具的两个子档：描边开着多两枚（描边色/描边粗细）、底板开着多一枚（底板色）
        toolbar.setActiveTool(GyanisScribe::Core::ToolKind::Text);
        toolbar.setTextOutline(true);
        shoot(toolbar, outputDirectory, "annotation-toolbar-text-outline");
        toolbar.setTextBackground(true);
        shoot(toolbar, outputDirectory, "annotation-toolbar-text-outline-background");
    }

    // 面 2：标注工具栏满配态——16 格色板 + 6 格最近色，这是它能长到的最宽形状
    {
        const auto paletteSeeded = settings.setValue(GyanisScribe::Core::kAnnotationPalette, GyanisScribe::Core::paletteToTokens(fullPalette()));
        const auto recentSeeded  = settings.setValue(GyanisScribe::Core::kAnnotationRecentColors, GyanisScribe::Core::serializeRecentColors(fullRecentColors()));
        if (!paletteSeeded || !recentSeeded)
        {
            std::fprintf(stderr, "failed to seed the full palette\n");
            return 1;
        }
        GyanisScribe::Gui::AnnotationToolbar toolbar{settings, icons, nullptr};
        toolbar.show();
        shoot(toolbar, outputDirectory, "annotation-toolbar-full-palette");
    }

    // 面 3：主窗口初始尺寸档（MainWindow 构造里 resize(680, 560)，实际被最小宽顶住，读数行会报真实值）
    {
        GyanisScribe::Core::HistoryStore history(dataDirectory / "history.json");
        const auto                       integration = GyanisScribe::Platform::createPlatformIntegration();
        GyanisScribe::Main::MainWindow   window(settings, theme, icons, history, integration->capabilities(), *integration);
        window.resize(680, 560);
        for (const auto page: {GyanisScribe::Main::MainWindowPage::General, GyanisScribe::Main::MainWindowPage::Capture})
        {
            window.open(page);
            shoot(window, outputDirectory, page == GyanisScribe::Main::MainWindowPage::General ? "main-general-default" : "main-capture-default");
        }
        // 快捷键页整页：那一页比窗口高，分组标题行在折叠之下——抓滚动区里那张卡片（内容全高）才看得到
        window.open(GyanisScribe::Main::MainWindowPage::Hotkey);
        if (auto *stack = window.findChild<QStackedWidget *>(); stack != nullptr)
        {
            if (auto *scroll = qobject_cast<QScrollArea *>(stack->currentWidget()); scroll != nullptr && scroll->widget() != nullptr)
            {
                shoot(*scroll->widget(), outputDirectory, "page-hotkey-full");
            }
        }

        // 历史页两态：空态（还没记过任何一条）与有内容态（两条成品已落盘并记账）
        window.open(GyanisScribe::Main::MainWindowPage::History);
        shoot(window, outputDirectory, "history-empty");
        {
            const auto shotFile = dataDirectory / "history-shot.png";
            const auto clipFile = dataDirectory / "history-clip.mp4";
            syntheticScreen({320, 200}).save(QString::fromUtf8(shotFile.string().c_str()));
            {
                std::ofstream dummy{clipFile, std::ios::binary};
                dummy << "not a real video";
            }
            const auto policy = GyanisScribe::Core::policyFromSettings(settings);
            const auto now    = QDateTime::currentDateTime();
            static_cast<void>(history.append({now.addSecs(-3600), GyanisScribe::Core::HistoryKind::Screenshot, shotFile, QSize{2560, 1440}}, policy, now));
            static_cast<void>(history.append({now.addSecs(-120), GyanisScribe::Core::HistoryKind::Recording, clipFile, QSize{1920, 1080}}, policy, now));
            window.refreshHistory();
            shoot(window, outputDirectory, "history-filled");
        }

        // 「更多」菜单：离屏尽力拍——拍不出可信的图就明说，不当成图（菜单实机可补拍）
        if (auto *moreMenu = window.findChild<QMenu *>(); moreMenu != nullptr)
        {
            moreMenu->ensurePolished();
            moreMenu->adjustSize();
            const auto menuShot = moreMenu->grab();
            if (!menuShot.isNull() && menuShot.width() > 0 && menuShot.height() > 0)
            {
                menuShot.save(QString::fromUtf8((outputDirectory / "menu-more.png").string().c_str()));
                std::printf("menu-more: actual=%dx%d（离屏直接 grab，未经 popup；观感结论以实机为准）\n", menuShot.width(), menuShot.height());
            } else
            {
                std::printf("menu-more: 离屏拍不出（未验，留给实机走查）\n");
            }
        }

        // 设置页滚动条出现态（UI-04 两态对照）：把窗口压矮到内容装不下
        window.resize(680, 420);
        window.open(GyanisScribe::Main::MainWindowPage::General);
        shoot(window, outputDirectory, "main-general-scrolled");

        // 文档档 1000×680 的对照（跨页量尺与动作栏判据的宽窗侧）
        window.resize(1000, 680);
        window.open(GyanisScribe::Main::MainWindowPage::General);
        shoot(window, outputDirectory, "main-general-wide");
        window.open(GyanisScribe::Main::MainWindowPage::Capture);
        shoot(window, outputDirectory, "main-capture-wide");

        // 面 6：表单行列几何（跨页对表：标签右缘 / 字段左缘 / 缝 / 行高，坐标一律映射到窗口）
        {
            for (const auto &shot: std::array<std::pair<GyanisScribe::Main::MainWindowPage, const char *>, 8>{
                         {{GyanisScribe::Main::MainWindowPage::General, "general"},
                          {GyanisScribe::Main::MainWindowPage::Capture, "capture"},
                          {GyanisScribe::Main::MainWindowPage::Annotation, "annotation"},
                          {GyanisScribe::Main::MainWindowPage::Pin, "pin"},
                          {GyanisScribe::Main::MainWindowPage::Record, "record"},
                          {GyanisScribe::Main::MainWindowPage::Ocr, "ocr"},
                          {GyanisScribe::Main::MainWindowPage::Hotkey, "hotkey"},
                          {GyanisScribe::Main::MainWindowPage::Theme, "theme"}}})
            {
                window.open(shot.first);
                QCoreApplication::processEvents();
                auto *stack = window.findChild<QStackedWidget *>();
                if (stack == nullptr)
                {
                    continue;
                }
                auto *scroll = qobject_cast<QScrollArea *>(stack->currentWidget());
                QWidget *page = scroll != nullptr && scroll->widget() != nullptr ? scroll->widget() : stack->currentWidget();
                for (auto *form: page->findChildren<QFormLayout *>())
                {
                    for (int row = 0; row < form->rowCount(); ++row)
                    {
                        auto *labelItem = form->itemAt(row, QFormLayout::LabelRole);
                        auto *fieldItem = form->itemAt(row, QFormLayout::FieldRole);
                        if (labelItem == nullptr || fieldItem == nullptr || labelItem->widget() == nullptr || fieldItem->widget() == nullptr)
                        {
                            continue;
                        }
                        auto *label = labelItem->widget();
                        auto *field = fieldItem->widget();
                        const auto labelRight = label->mapTo(&window, QPoint{label->width(), 0}).x();
                        const auto fieldLeft  = field->mapTo(&window, QPoint{0, 0}).x();
                        std::printf("form[%s] row=%d labelRight=%d fieldLeft=%d gap=%d rowH=%d\n", shot.second, row, labelRight, fieldLeft, fieldLeft - labelRight, field->height());
                    }
                }
            }
        }

        // 面 4：深色与高对比度档的对应图（换肤观感核对；浅色已在前面出过）
        for (const auto &[mode, name]: std::array<std::pair<const char *, const char *>, 2>{{{"Dark", "dark"}, {"HighContrast", "hc"}}})
        {
            if (const auto written = settings.setValue(GyanisScribe::Core::kThemeMode, QString::fromUtf8(mode)); !written)
            {
                std::fprintf(stderr, "failed to switch the theme\n");
                return 1;
            }
            theme.applyFromSettings();
            window.open(GyanisScribe::Main::MainWindowPage::General);
            shoot(window, outputDirectory, std::format("theme-{}-general", name).c_str());
            GyanisScribe::Gui::AnnotationToolbar toolbar{settings, icons, nullptr};
            toolbar.show();
            shoot(toolbar, outputDirectory, std::format("theme-{}-toolbar", name).c_str());
        }

        // 面 5：浮窗族三档主题全过（P4 矩阵；浅色那一趟同时供 P2 判定用）
        for (const auto &[mode, name]: std::array<std::pair<const char *, const char *>, 3>{{{"Light", "light"}, {"Dark", "dark"}, {"HighContrast", "hc"}}})
        {
            if (const auto written = settings.setValue(GyanisScribe::Core::kThemeMode, QString::fromUtf8(mode)); !written)
            {
                std::fprintf(stderr, "failed to switch the theme\n");
                return 1;
            }
            theme.applyFromSettings();
            shootFloatingFaces(settings, icons, theme, *integration, outputDirectory, std::format("theme-{}-", name));
        }
    }

    return 0;
}
