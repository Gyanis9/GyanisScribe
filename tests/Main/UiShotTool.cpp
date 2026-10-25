// 文档出图工具：离屏起一份真主窗口（真主题、真图标、临时配置与临时历史），逐页拍成 PNG；
// 另出三张动作图（截图遮罩、标注窗、贴图），底图是合成的一份中性桌面，不依赖真桌面的现场。
// 与 CrashProbe 同一条理由不是产品的一部分：不进安装包、不挂任何产品入口，只给文档出图。
// 跑法：UiShotTool <输出目录>；产图文件名与页面对应关系见下面 kShots 那张表。

#include "Main/MainWindow.h"

#include "Base/Types.h"
#include "Core/Annotation/AnnotationEngine.h"
#include "Core/Annotation/AnnotationTool.h"
#include "Core/Annotation/IAnnotationEngine.h"
#include "Core/Annotation/PenParameters.h"
#include "Core/History/HistoryStore.h"
#include "Platform/IPlatformIntegration.h"
#include "Core/Settings/SettingsKeys.h"
#include "Core/Settings/SettingsStore.h"
#include "Gui/Annotation/AnnotationToolbar.h"
#include "Gui/Annotation/AnnotationWindow.h"
#include "Gui/Capture/CaptureOverlay.h"
#include "Gui/Pin/PinWindow.h"
#include "Gui/Resources/IconProvider.h"
#include "Gui/Theme/ThemeManager.h"

#include <QApplication>
#include <QCoreApplication>
#include <QFont>
#include <QFontDatabase>
#include <QLinearGradient>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QPixmap>
#include <QString>
#include <QStringList>

#include <array>
#include <cstdio>
#include <filesystem>
#include <format>
#include <memory>

namespace
{
    using GyanisScribe::Main::MainWindowPage;

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

    /// 动作图的桌面底图尺寸（逻辑像素，离屏档缩放因子恒为 1）
    const QSize kDesktopSize{1280, 800};

    /// 底图上那扇示例窗口的几何：动作图里的选区、画布与贴图裁剪都对准它
    const QRect kMockupWindow{200, 80, 880, 600};

    /**
     * @brief 画一张中性内容的桌面底图
     * @return 1280x800 的合成桌面（暖灰渐变 + 一扇示例数据面板 + 任务栏）
     * @details 动作图的「屏幕现场」全靠它：内容是自己画的通用界面，
     *          不含任何现有产品的素材，也不把出图机器桌面上的真东西印进文档。
     */
    QImage makeMockupDesktop()
    {
        QImage frame{kDesktopSize, QImage::Format_ARGB32_Premultiplied};
        QPainter painter{&frame};
        painter.setRenderHint(QPainter::Antialiasing);
        painter.setRenderHint(QPainter::TextAntialiasing);

        QLinearGradient sky{0, 0, 0, static_cast<qreal>(kDesktopSize.height())};
        sky.setColorAt(0.0, QColor{0xf0, 0xed, 0xe8});
        sky.setColorAt(1.0, QColor{0xe2, 0xde, 0xd7});
        painter.fillRect(frame.rect(), sky);

        // 任务栏
        painter.setPen(Qt::NoPen);
        painter.setBrush(QColor{0xdb, 0xd7, 0xd0});
        painter.drawRect(0, 760, 1280, 40);
        painter.setBrush(QColor{0xc6, 0xc1, 0xb7});
        for (int index = 0; index < 3; ++index)
        {
            painter.drawRoundedRect(20 + index * 28, 772, 16, 16, 4, 4);
        }
        QFont font = QApplication::font();
        font.setPointSize(9);
        painter.setFont(font);
        painter.setPen(QColor{0x6b, 0x66, 0x5d});
        painter.drawText(QRect{1160, 760, 100, 40}, Qt::AlignCenter, QStringLiteral("10:24"));

        // 窗口投影与窗体
        painter.setPen(Qt::NoPen);
        painter.setBrush(QColor{30, 26, 20, 36});
        painter.drawRoundedRect(kMockupWindow.adjusted(6, 8, 6, 8), 12, 12);
        painter.setBrush(QColor{0xff, 0xff, 0xff});
        painter.drawRoundedRect(kMockupWindow, 10, 10);

        // 之后的内容都收在窗体的圆角轮廓里：直角矩形不会从圆角处露出去
        QPainterPath windowClip;
        windowClip.addRoundedRect(QRectF{kMockupWindow}, 10, 10);
        painter.save();
        painter.setClipPath(windowClip);

        // 标题栏
        painter.setPen(Qt::NoPen);
        painter.setBrush(QColor{0xf7, 0xf5, 0xf2});
        painter.drawRect(200, 80, 880, 40);
        painter.setPen(QPen{QColor{0xe6, 0xe2, 0xdb}, 1});
        painter.drawLine(200, 120, 1080, 120);
        font = QApplication::font();
        font.setPointSize(10);
        painter.setFont(font);
        painter.setPen(QColor{0x57, 0x53, 0x4b});
        painter.drawText(QRect{224, 80, 400, 40}, Qt::AlignVCenter | Qt::AlignLeft, QStringLiteral("数据面板 · 示例应用"));
        painter.setPen(Qt::NoPen);
        painter.setBrush(QColor{0xcb, 0xc6, 0xbd});
        for (int index = 0; index < 3; ++index)
        {
            painter.drawRoundedRect(1000 + index * 22, 93, 14, 14, 3, 3);
        }

        // 侧栏与导航项
        painter.setBrush(QColor{0xfb, 0xfa, 0xf8});
        painter.drawRect(200, 120, 200, 560);
        painter.setPen(QPen{QColor{0xec, 0xe8, 0xe1}, 1});
        painter.drawLine(400, 120, 400, 680);
        const QStringList navLabels{QStringLiteral("总览"), QStringLiteral("任务"), QStringLiteral("报表"), QStringLiteral("消息"), QStringLiteral("设置")};
        for (int index = 0; index < navLabels.size(); ++index)
        {
            const int itemTop = 138 + index * 44;
            if (index == 1)
            {
                painter.setPen(Qt::NoPen);
                painter.setBrush(QColor{0xef, 0xea, 0xe2});
                painter.drawRoundedRect(212, itemTop, 176, 36, 8, 8);
            }
            font = QApplication::font();
            font.setPointSize(10);
            font.setBold(index == 1);
            painter.setFont(font);
            painter.setPen(index == 1 ? QColor{0x26, 0x24, 0x1f} : QColor{0x6d, 0x67, 0x5d});
            painter.drawText(QRect{232, itemTop, 150, 36}, Qt::AlignVCenter | Qt::AlignLeft, navLabels.at(index));
        }

        // 正文：标题、三张数据卡、柱状图面板与两行段落
        font = QApplication::font();
        font.setPointSize(15);
        font.setBold(true);
        painter.setFont(font);
        painter.setPen(QColor{0x2b, 0x28, 0x23});
        painter.drawText(QRect{432, 140, 400, 30}, Qt::AlignVCenter | Qt::AlignLeft, QStringLiteral("本周概览"));
        painter.setPen(Qt::NoPen);
        painter.setBrush(QColor{0xde, 0xd9, 0xd1});
        painter.drawRoundedRect(432, 178, 260, 8, 4, 4);

        const QStringList cardValues{QStringLiteral("128"), QStringLiteral("96.4%"), QStringLiteral("37")};
        const QStringList cardDeltas{QStringLiteral("+12%"), QStringLiteral("-0.4%"), QStringLiteral("+3")};
        for (int index = 0; index < 3; ++index)
        {
            const int cardLeft = 432 + index * 216;
            painter.setBrush(QColor{0xff, 0xff, 0xff});
            painter.setPen(QPen{QColor{0xe8, 0xe4, 0xdd}, 1});
            painter.drawRoundedRect(cardLeft, 200, 196, 92, 8, 8);
            painter.setPen(Qt::NoPen);
            painter.setBrush(QColor{0xd9, 0xd4, 0xcb});
            painter.drawRoundedRect(cardLeft + 16, 216, 80, 7, 3, 3);
            font = QApplication::font();
            font.setPointSize(17);
            font.setBold(true);
            painter.setFont(font);
            painter.setPen(QColor{0x35, 0x32, 0x2c});
            painter.drawText(QRect{cardLeft + 16, 230, 164, 34}, Qt::AlignVCenter | Qt::AlignLeft, cardValues.at(index));
            font = QApplication::font();
            font.setPointSize(9);
            font.setBold(false);
            painter.setFont(font);
            painter.setPen(index == 0 ? QColor{0x4a, 0x7c, 0x59} : QColor{0x8a, 0x85, 0x7c});
            painter.drawText(QRect{cardLeft + 16, 264, 164, 18}, Qt::AlignVCenter | Qt::AlignLeft, cardDeltas.at(index));
        }

        painter.setBrush(QColor{0xfd, 0xfc, 0xfa});
        painter.setPen(QPen{QColor{0xe8, 0xe4, 0xdd}, 1});
        painter.drawRoundedRect(432, 316, 628, 240, 8, 8);
        painter.setPen(Qt::NoPen);
        painter.setBrush(QColor{0xd9, 0xd4, 0xcb});
        painter.drawRoundedRect(448, 332, 120, 7, 3, 3);
        const std::array<int, 6> barHeights{96, 140, 72, 168, 120, 88};
        for (std::size_t index = 0; index < barHeights.size(); ++index)
        {
            const int barLeft  = 480 + static_cast<int>(index) * 92;
            const int barTop   = 524 - barHeights[index];
            const bool accent  = barHeights[index] == 168;
            painter.setBrush(accent ? QColor{0x46, 0x78, 0x4f} : QColor{0xc9, 0xc3, 0xb8});
            painter.drawRoundedRect(barLeft, barTop, 44, barHeights[index], 4, 4);
        }
        painter.setPen(QPen{QColor{0xe2, 0xdd, 0xd4}, 1});
        painter.drawLine(448, 524, 1044, 524);

        painter.setPen(Qt::NoPen);
        painter.setBrush(QColor{0xe4, 0xe0, 0xd8});
        painter.drawRoundedRect(432, 580, 560, 8, 4, 4);
        painter.drawRoundedRect(432, 598, 430, 8, 4, 4);

        painter.restore();
        painter.end();
        return frame;
    }

    /**
     * @brief 给标注引擎落一组示例标注
     * @param engine 已载入底图的引擎
     * @return 全部落定返回 true；任何一笔被引擎拒绝返回 false
     * @details 六种工具各来一份（矩形、箭头、序号、文字、高亮），坐标按底图上那扇示例窗口内的版面摆；
     *          收在「选择」档并清掉选中——成图该是刚画完的样子，不是一身手柄。
     */
    bool drawSampleAnnotations(GyanisScribe::Core::IAnnotationEngine &engine)
    {
        using GyanisScribe::Core::PenParameters;
        using GyanisScribe::Core::ToolKind;

        const PenParameters red{.color = QColor{222, 56, 46}, .lineWidth = 3, .fontSize = 18};
        const auto drawStroke = [&engine](const ToolKind tool, const PenParameters &pen, const QPoint &from, const QPoint &to)
        {
            if (!engine.setActiveTool(tool).has_value() || !engine.setPenParameters(pen).has_value())
            {
                return false;
            }
            engine.beginStroke(from);
            engine.moveStroke(to);
            return engine.endStroke().has_value();
        };
        const auto drawNumber = [&engine, &red](const QPoint &at)
        {
            if (!engine.setActiveTool(ToolKind::Number).has_value() || !engine.setPenParameters(red).has_value())
            {
                return false;
            }
            engine.beginStroke(at);
            return engine.endStroke().has_value();
        };

        if (!drawStroke(ToolKind::Rectangle, red, {224, 112}, {436, 220}))
        {
            return false;
        }
        if (!drawStroke(ToolKind::Arrow, red, {150, 164}, {226, 230}))
        {
            return false;
        }
        if (!drawNumber({216, 104}) || !drawNumber({240, 244}))
        {
            return false;
        }
        if (!engine.setActiveTool(ToolKind::Text).has_value() || !engine.setPenParameters(red).has_value())
        {
            return false;
        }
        engine.beginStroke({470, 64});
        if (!engine.setPendingText(QStringLiteral("数据每天 06:00 刷新")).has_value() || !engine.endStroke().has_value())
        {
            return false;
        }
        const PenParameters yellow{.color = QColor{255, 206, 64}, .lineWidth = 5, .fontSize = 18};
        if (!drawStroke(ToolKind::Highlight, yellow, {236, 504}, {760, 504}))
        {
            return false;
        }
        engine.clearSelection();
        return engine.setActiveTool(ToolKind::Select).has_value();
    }
} // namespace

int main(int argc, char *argv[])
{
    // 离屏平台要排在 QApplication 之前：出图不该在用户的桌面上闪一串窗口
    qputenv("QT_QPA_PLATFORM", "offscreen");

    QApplication application(argc, argv);
    QCoreApplication::setOrganizationName(QStringLiteral("Gyanis"));
    QCoreApplication::setApplicationName(QStringLiteral("GyanisScribe-Shots"));
    // 关于页正文的版本号读 applicationVersion：与真程序同一条真源（工程版本宏），不写第二份字面量
    QCoreApplication::setApplicationVersion(QStringLiteral(PROJECT_VERSION));

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

    GyanisScribe::Core::SettingsStore settings(dataDirectory / "settings.ini");
    // 目录几格给一条中性占位，别把出图这台机器的真实路径（用户名、临时目录）印进文档图里
    const auto placeholdersSeeded = settings.setValue(GyanisScribe::Core::kPathScreenshot, QStringLiteral("C:/Users/Example/Pictures/GyanisScribe")) &&
                                    settings.setValue(GyanisScribe::Core::kPathRecording, QStringLiteral("C:/Users/Example/Videos/GyanisScribe")) &&
                                    settings.setValue(GyanisScribe::Core::kOcrTessDataDirectory, QStringLiteral("C:/Users/Example/AppData/Roaming/Gyanis/GyanisScribe/tessdata"));
    if (!placeholdersSeeded)
    {
        std::fprintf(stderr, "failed to seed placeholder paths\n");
        return 1;
    }

    GyanisScribe::Gui::IconProvider  icons;
    GyanisScribe::Gui::ThemeManager  theme(settings);
    GyanisScribe::Core::HistoryStore history(dataDirectory / "history.json");
    const auto                       integration = GyanisScribe::Platform::createPlatformIntegration();
    // 样式表要落一次：真实程序里这一步在 AppController 构造里做，这里没有控制器
    theme.applyFromSettings();

    GyanisScribe::Main::MainWindow window(settings, theme, icons, history, integration->capabilities(), *integration);
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

    // 动作图的产线：与主窗口那批共用同一份设置、图标与平台集成
    const auto saveShot = [&outputDirectory](const QPixmap &pixmap, const char *name)
    {
        const auto filePath = outputDirectory / std::format("{}.png", name);
        if (!pixmap.save(QString::fromUtf8(filePath.string().c_str())))
        {
            std::fprintf(stderr, "save failed: %s\n", filePath.string().c_str());
            return false;
        }
        std::printf("%s\n", filePath.string().c_str());
        return true;
    };
    const auto mockup = makeMockupDesktop();

    // 截图遮罩：拖到一半的现场（选区、三分网格与放大镜读数都在），事件直接投给控件，不碰真鼠标
    {
        GyanisScribe::Gui::CaptureOverlay overlay{mockup, QRect{QPoint{0, 0}, kDesktopSize}, 1.0, settings};
        const auto sendMouse = [&overlay](const QEvent::Type type, const QPoint &local, const Qt::MouseButton button, const Qt::MouseButtons buttons)
        {
            QMouseEvent event{type, QPointF{local}, QPointF{local}, button, buttons, Qt::KeyboardModifiers{}};
            QApplication::sendEvent(&overlay, &event);
        };
        sendMouse(QEvent::MouseMove, {640, 400}, Qt::NoButton, Qt::NoButton);
        sendMouse(QEvent::MouseButtonPress, {350, 210}, Qt::LeftButton, Qt::LeftButton);
        sendMouse(QEvent::MouseMove, {846, 470}, Qt::NoButton, Qt::LeftButton);
        QCoreApplication::processEvents();
        if (!saveShot(overlay.grab(), "action-capture"))
        {
            return 1;
        }
    }

    // 标注窗：走一遍真开局（startAnnotation），再落示例标注；工具栏是独立浮窗，与窗口分别 render 进桌面底图
    {
        GyanisScribe::Core::AnnotationEngine    engine;
        GyanisScribe::Gui::AnnotationWindow     annotationWindow{engine, settings, std::make_unique<GyanisScribe::Gui::AnnotationToolbar>(settings, icons)};
        GyanisScribe::Base::CapturedImage       captured;
        captured.image                          = mockup.copy(kMockupWindow);
        captured.logicalSize                    = kMockupWindow.size();
        captured.devicePixelRatio               = 1.0;
        captured.screenName                     = QStringLiteral("UiShot");
        captured.logicalRegion                  = kMockupWindow;
        annotationWindow.startAnnotation(captured, QRect{0, 0, kDesktopSize.width(), 760}, kMockupWindow);
        if (!drawSampleAnnotations(engine))
        {
            std::fprintf(stderr, "sample annotations rejected by the engine\n");
            return 1;
        }
        QCoreApplication::processEvents();
        QPixmap shot = QPixmap::fromImage(mockup);
        QPainter painter{&shot};
        annotationWindow.render(&painter, annotationWindow.pos());
        annotationWindow.toolbar().render(&painter, annotationWindow.toolbar().pos());
        painter.end();
        if (!saveShot(shot, "action-annotation"))
        {
            return 1;
        }
    }

    // 贴图：两张裁自底图的贴图摆回桌面上，合成出「钉在屏幕最上层」的样子
    {
        GyanisScribe::Gui::PinWindow cardPin{mockup.copy(432, 200, 196, 92), settings, *integration};
        GyanisScribe::Gui::PinWindow chartPin{mockup.copy(432, 316, 470, 240), settings, *integration};
        cardPin.move(120, 520);
        chartPin.move(742, 430);
        QCoreApplication::processEvents();
        QPixmap shot = QPixmap::fromImage(mockup);
        QPainter painter{&shot};
        cardPin.render(&painter, cardPin.pos());
        chartPin.render(&painter, chartPin.pos());
        painter.end();
        if (!saveShot(shot, "action-pin"))
        {
            return 1;
        }
    }

    return 0;
}
