#include "Gui/Annotation/AnnotationView.h"

#include "Core/Capture/SourceInfo.h"
#include "Core/Image/ImageFit.h"
#include "Core/Settings/SettingsKeys.h"
#include "Gui/Annotation/AnnotationToolbarGeometry.h"
#include "Gui/Annotation/AnnotationWindow.h"

#include "Base/Text.h"
#include "Core/Annotation/AnnotationEngine.h"
#include "Gui/Annotation/AnnotationToolbar.h"
#include "Core/Annotation/IAnnotationEngine.h"
#include "Core/Annotation/AnnotationPalette.h"
#include "Core/Annotation/PenParameters.h"

#include "Core/Settings/SettingsStore.h"
#include "Gui/Controls/AppShortcuts.h"
#include "Gui/Resources/IconProvider.h"
#include "Gui/Theme/DesignTokens.h"
#include "Gui/Theme/ThemeKind.h"
#include "Gui/Theme/ThemeManager.h"

#include <gtest/gtest.h>

#include <QAbstractButton>
#include <QApplication>
#include <QCoreApplication>
#include <QDateTime>
#include <QGuiApplication>
#include <QElapsedTimer>
#include <QFontMetricsF>
#include <QImage>
#include <QKeyEvent>
#include <QLabel>
#include <QMenu>
#include <QMouseEvent>
#include <QPainter>
#include <QRegularExpression>
#include <QScreen>
#include <QSignalSpy>
#include <QTextCursor>
#include <QTextDocument>
#include <QTextEdit>
#include <QToolButton>
#include <QPoint>
#include <QRect>
#include <QSize>
#include <QWidget>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <vector>
#include <filesystem>
#include <format>
#include <memory>
#include <numbers>
#include <optional>
#include <tuple>
#include <utility>

using GyanisScribe::Base::CapturedImage;
using GyanisScribe::Base::ErrorCode;
using GyanisScribe::Core::CaptureSourceInfo;
using GyanisScribe::Core::displayRectFor;
using GyanisScribe::Core::PenParameters;
using GyanisScribe::Core::SettingsStore;
using GyanisScribe::Core::ToolKind;
using GyanisScribe::Gui::AnnotationToolbar;
using GyanisScribe::Gui::AnnotationWindow;
using GyanisScribe::Gui::IconProvider;
using GyanisScribe::Gui::ShortcutAction;
using GyanisScribe::Gui::shortcutActionFor;
using GyanisScribe::Gui::toCanvasPoint;
using GyanisScribe::Gui::toolShortcutFor;
using GyanisScribe::Gui::toWidgetPoint;

namespace
{
    std::unique_ptr<QApplication> application;

    /// 把一条按键事件投给目标控件：判据不依赖「窗口恰好是当前焦点」
    void pressKeyOn(QWidget &target, const Qt::Key key, const Qt::KeyboardModifiers modifiers = Qt::NoModifier)
    {
        QKeyEvent event{QEvent::KeyPress, key, modifiers};
        QApplication::sendEvent(&target, &event);
    }

    /// 造一帧 n 倍比的画布：物理 400x200、逻辑按 size 给，标注坐标全程用逻辑值
    CapturedImage makeCanvas(const QSize &logicalSize, qreal devicePixelRatio = 2.0)
    {
        CapturedImage captured;
        captured.image = QImage{QSize{static_cast<int>(logicalSize.width() * devicePixelRatio), static_cast<int>(logicalSize.height() * devicePixelRatio)}, QImage::Format_ARGB32};
        captured.image.fill(QColor{180, 180, 180});
        captured.logicalSize      = logicalSize;
        captured.devicePixelRatio = devicePixelRatio;
        captured.screenName       = QStringLiteral("TestScreen");
        return captured;
    }

    /// 取信息面板上某一项的文本：标签按对象名认，面板没建或找不到那一项时返回空串
    QString panelLabelText(const QWidget &panel, const char *objectName)
    {
        const auto *label = panel.findChild<QLabel *>(QString::fromLatin1(objectName));
        return label != nullptr ? label->text() : QString{};
    }

    class AnnotationWindowFixture : public ::testing::Test
    {
    protected:
        static void SetUpTestSuite()
        {
            // argv 与 argc 的缓冲要有静态存储期：Qt 只存指针不拷贝，
            // 而 QCoreApplication::applicationDirPath() 这类调用是在装置函数返回之后才读它们。
            static int   argumentCount  = 1;
            static char  argumentZero[] = "TestAnnotationWindow";
            static char *arguments[]    = {argumentZero, nullptr};
            application                 = std::make_unique<QApplication>(argumentCount, arguments);
        }

        static void TearDownTestSuite()
        {
            application.reset();
        }

        void SetUp() override
        {
            // 每条用例一份 ini：并行跑时两个进程写同一个文件会互相盖掉设置
            const auto caseName = ::testing::UnitTest::GetInstance()->current_test_info()->name();
            m_iniPath           = std::filesystem::temp_directory_path() / std::format("GyanisScribe-AnnotationWindow-{}.ini", caseName);
            std::error_code ignored;
            std::filesystem::remove(m_iniPath, ignored);

            m_engine = std::make_unique<GyanisScribe::Core::AnnotationEngine>();
            m_engine->loadCanvas(makeCanvas({200, 100}));
            // 这两步都要验成功：SetUp 里悄悄失败会让后面的用例在「工具不是矩形」的状态上跑，红了也看不出原因
            ASSERT_TRUE(m_engine->setActiveTool(ToolKind::Rectangle).has_value());
            ASSERT_TRUE(m_engine->setPenParameters(PenParameters{.color = QColor{255, 0, 0}, .lineWidth = 2, .fontSize = 16, .fillStyle = 0}).has_value());
        }

        void TearDown() override
        {
            m_engine.reset();
        }

        [[nodiscard]] GyanisScribe::Core::IAnnotationEngine &engine() const
        {
            return *m_engine;
        }

        /// 本次用例的设置存储：信息面板开关这类要在开窗之前落到 ini 上
        [[nodiscard]] SettingsStore &settings()
        {
            if (m_settings == nullptr)
            {
                m_settings = std::make_unique<SettingsStore>(m_iniPath);
            }
            return *m_settings;
        }

        /// 开一扇真的标注窗：工具栏要的设置与图标就近给，窗口与引擎同生共死
        [[nodiscard]] std::unique_ptr<AnnotationWindow> makeWindow()
        {
            if (m_icons == nullptr)
            {
                m_icons = std::make_unique<IconProvider>();
            }
            if (m_settings == nullptr)
            {
                m_settings = std::make_unique<SettingsStore>(m_iniPath);
            }
            return std::make_unique<AnnotationWindow>(engine(), *m_settings, std::make_unique<AnnotationToolbar>(*m_settings, *m_icons));
        }

        /// 画一条并选中，返回选中项的包围盒
        [[nodiscard]] std::optional<QRect> drawAndSelect(const QPoint &from, const QPoint &to)
        {
            engine().beginStroke(from);
            engine().moveStroke(to);
            const auto settled = engine().endStroke();
            EXPECT_TRUE(settled.has_value()) << settled.error().message;
            if (!settled)
            {
                return {};
            }
            EXPECT_TRUE(engine().select(*settled).has_value());
            return engine().selectionBounds();
        }

    private:
        std::unique_ptr<GyanisScribe::Core::AnnotationEngine> m_engine;   ///< 被测引擎，提供选中项包围盒
        std::filesystem::path                                 m_iniPath;  ///< 本次用例专用的设置文件
        std::unique_ptr<IconProvider>                         m_icons;    ///< 工具栏要的图标供给器
        std::unique_ptr<SettingsStore>                        m_settings; ///< 工具栏要的设置store
    };

} // namespace

/**
 * @brief 钉住两套坐标互指同一个点
 * @details 显示区本身的判据已上收到 tests/Core/Image/TestImageFit.cpp，这里只测点位往返。
 * @details 只补一套换算就是静默错位：正向、反向各算一遍并断它们落在同一个点上；
 *          同时钉住「画布外夹到边缘」而不是报错——拖出边缘的意思是画到边界。
 */
TEST(ViewGeometry, CanvasAndWidgetMappingsPointAtTheSamePlace)
{
    const QSize canvas{200, 100};
    const QRect display = displayRectFor({400, 400}, canvas);

    for (const auto &point: {QPoint{0, 0}, QPoint{199, 99}, QPoint{64, 37}, QPoint{120, 80}})
    {
        const auto widgetPoint = toWidgetPoint(point, display, canvas);
        const auto back        = toCanvasPoint(widgetPoint, display, canvas);
        EXPECT_EQ(back, point) << point.x() << "," << point.y();
    }

    // 画布外的两个方向都要夹住，不能给出负数或越界坐标
    EXPECT_EQ(toCanvasPoint({display.left() - 50, display.top() - 20}, display, canvas), QPoint(0, 0));
    EXPECT_EQ(toCanvasPoint({display.right() + 50, display.bottom() + 20}, display, canvas), QPoint(199, 99));

    // 显示区为空时换算给 (0,0) 而不是崩掉：没有画布就没有坐标可言
    EXPECT_EQ(toCanvasPoint({10, 10}, QRect{}, canvas), QPoint(0, 0));
}

/**
 * @brief 钉住快捷键表的每一项
 * @details 按键含义改了用户肌肉记忆就改了，所以逐条钉；不在表内的组合必须回 None，
 *          否则输入文字时按个 s 就会去存文件。
 */
TEST(ViewShortcuts, ShortcutTableMapsExactlyTheDocumentedKeys)
{
    EXPECT_EQ(shortcutActionFor(Qt::Key_Escape, Qt::NoModifier), ShortcutAction::Close);
    EXPECT_EQ(shortcutActionFor(Qt::Key_Return, Qt::NoModifier), ShortcutAction::Copy);
    EXPECT_EQ(shortcutActionFor(Qt::Key_Enter, Qt::NoModifier), ShortcutAction::Copy);
    EXPECT_EQ(shortcutActionFor(Qt::Key_Z, Qt::ControlModifier), ShortcutAction::Undo);
    EXPECT_EQ(shortcutActionFor(Qt::Key_Z, Qt::ControlModifier | Qt::ShiftModifier), ShortcutAction::Redo);
    EXPECT_EQ(shortcutActionFor(Qt::Key_Y, Qt::ControlModifier), ShortcutAction::Redo);
    EXPECT_EQ(shortcutActionFor(Qt::Key_S, Qt::ControlModifier), ShortcutAction::Save);
    EXPECT_EQ(shortcutActionFor(Qt::Key_D, Qt::ControlModifier), ShortcutAction::Pin);
    EXPECT_EQ(shortcutActionFor(Qt::Key_O, Qt::ControlModifier), ShortcutAction::Ocr);
    EXPECT_EQ(shortcutActionFor(Qt::Key_Delete, Qt::NoModifier), ShortcutAction::DeleteSelection);
    EXPECT_EQ(shortcutActionFor(Qt::Key_Backspace, Qt::NoModifier), ShortcutAction::DeleteSelection);
    EXPECT_EQ(shortcutActionFor(Qt::Key_Left, Qt::NoModifier), ShortcutAction::NudgeSelection);
    EXPECT_EQ(shortcutActionFor(Qt::Key_Down, Qt::ShiftModifier), ShortcutAction::NudgeSelection);
    EXPECT_EQ(shortcutActionFor(Qt::Key_F5, Qt::NoModifier), ShortcutAction::Refresh);

    // 这些组合不归窗口管：文字输入里按 s/z、没有 Ctrl 的 Esc 之外的键都要交回 Qt
    EXPECT_EQ(shortcutActionFor(Qt::Key_S, Qt::NoModifier), ShortcutAction::None);
    EXPECT_EQ(shortcutActionFor(Qt::Key_Z, Qt::NoModifier), ShortcutAction::None);
    EXPECT_EQ(shortcutActionFor(Qt::Key_A, Qt::ControlModifier), ShortcutAction::None);
    EXPECT_EQ(shortcutActionFor(Qt::Key_Escape, Qt::ControlModifier), ShortcutAction::None);
    EXPECT_EQ(shortcutActionFor(Qt::Key_Delete, Qt::ControlModifier), ShortcutAction::None);
    // 带 Ctrl 的方向键留给 Qt 自己用（换焦点那一类），截走就少一条既有行为
    EXPECT_EQ(shortcutActionFor(Qt::Key_Right, Qt::ControlModifier), ShortcutAction::None);
    EXPECT_EQ(shortcutActionFor(Qt::Key_F5, Qt::ControlModifier), ShortcutAction::None);
}

/**
 * @brief 钉住图形操作与层叠那几枚键，以及它们的拒面
 * @details Ctrl+C/V 管图形副本（没选中时复制整图的回退由窗口分派时判，不进这张表）；
 *          Ctrl+Shift+↑/↓ 改一层、Home/End 到两端。Ctrl+↑/↓ 不带 Shift 仍交回 Qt——
 *          行内文字框用它按词移光标，截走就少一条既有行为。
 */
TEST(ViewShortcuts, ShapeAndLayerShortcutsMapExactlyTheDocumentedKeys)
{
    EXPECT_EQ(shortcutActionFor(Qt::Key_C, Qt::ControlModifier), ShortcutAction::CopyShape);
    EXPECT_EQ(shortcutActionFor(Qt::Key_V, Qt::ControlModifier), ShortcutAction::PasteShape);
    EXPECT_EQ(shortcutActionFor(Qt::Key_P, Qt::ControlModifier), ShortcutAction::Print);
    EXPECT_EQ(shortcutActionFor(Qt::Key_Up, Qt::ControlModifier | Qt::ShiftModifier), ShortcutAction::RaiseSelection);
    EXPECT_EQ(shortcutActionFor(Qt::Key_Down, Qt::ControlModifier | Qt::ShiftModifier), ShortcutAction::LowerSelection);
    EXPECT_EQ(shortcutActionFor(Qt::Key_Home, Qt::ControlModifier | Qt::ShiftModifier), ShortcutAction::SendToBack);
    EXPECT_EQ(shortcutActionFor(Qt::Key_End, Qt::ControlModifier | Qt::ShiftModifier), ShortcutAction::BringToFront);

    EXPECT_EQ(shortcutActionFor(Qt::Key_C, Qt::ControlModifier | Qt::ShiftModifier), ShortcutAction::None);
    EXPECT_EQ(shortcutActionFor(Qt::Key_V, Qt::ControlModifier | Qt::ShiftModifier), ShortcutAction::None);
    EXPECT_EQ(shortcutActionFor(Qt::Key_Home, Qt::ControlModifier), ShortcutAction::None) << "Ctrl+Home 不带 Shift 不该改层序";
    EXPECT_EQ(shortcutActionFor(Qt::Key_End, Qt::ControlModifier), ShortcutAction::None);
    EXPECT_EQ(shortcutActionFor(Qt::Key_Up, Qt::ControlModifier), ShortcutAction::None) << "Ctrl+↑ 留给行内文字框按词移光标";
    EXPECT_EQ(shortcutActionFor(Qt::Key_Down, Qt::ControlModifier), ShortcutAction::None);
    EXPECT_EQ(shortcutActionFor(Qt::Key_C, Qt::NoModifier), ShortcutAction::None) << "裸 c 不归这张表：它没绑任何动作";
    EXPECT_EQ(shortcutActionFor(Qt::Key_P, Qt::NoModifier), ShortcutAction::None) << "裸 P 是画笔工具的字母键，打印只认 Ctrl+P";
}

/**
 * @brief 钉住字母键位与清册同解，且只认按下就相等的那一颗
 * @details 键位住在 Gui/Controls/AppShortcuts.h 那份清册里（默认那颗就是登记表的字母），界面提示与按键分派读的是同一处：
 *          十三档逐个钉住，再钉两位拒绝面——没绑工具的字母不许换工具，带修饰键的组合也不许
 *          （Ctrl+Z 里的 z 若当成字母键位，撤销就变成换工具）。
 */
TEST(ViewShortcuts, ToolShortcutTableMapsEveryRegisteredTool)
{
    // 一份独占的空配置：这条判的是「没人改过键」那一档，所以清册要现读默认的十三颗
    const auto      iniPath = std::filesystem::temp_directory_path() / std::format("GyanisScribe-ToolKeys-{}.ini", ::testing::UnitTest::GetInstance()->current_test_info()->name());
    std::error_code ignored;
    std::filesystem::remove(iniPath, ignored);
    const SettingsStore settings{iniPath};

    EXPECT_EQ(toolShortcutFor(Qt::Key_R, Qt::NoModifier, settings), ToolKind::Rectangle);
    EXPECT_EQ(toolShortcutFor(Qt::Key_E, Qt::NoModifier, settings), ToolKind::Ellipse);
    EXPECT_EQ(toolShortcutFor(Qt::Key_A, Qt::NoModifier, settings), ToolKind::Arrow);
    EXPECT_EQ(toolShortcutFor(Qt::Key_P, Qt::NoModifier, settings), ToolKind::Pen);
    EXPECT_EQ(toolShortcutFor(Qt::Key_H, Qt::NoModifier, settings), ToolKind::Highlight);
    EXPECT_EQ(toolShortcutFor(Qt::Key_M, Qt::NoModifier, settings), ToolKind::Mosaic);
    EXPECT_EQ(toolShortcutFor(Qt::Key_T, Qt::NoModifier, settings), ToolKind::Text);
    EXPECT_EQ(toolShortcutFor(Qt::Key_N, Qt::NoModifier, settings), ToolKind::Number);
    EXPECT_EQ(toolShortcutFor(Qt::Key_L, Qt::NoModifier, settings), ToolKind::Line);
    EXPECT_EQ(toolShortcutFor(Qt::Key_B, Qt::NoModifier, settings), ToolKind::Blur);
    EXPECT_EQ(toolShortcutFor(Qt::Key_X, Qt::NoModifier, settings), ToolKind::Eraser);
    EXPECT_EQ(toolShortcutFor(Qt::Key_G, Qt::NoModifier, settings), ToolKind::Polyline);
    EXPECT_EQ(toolShortcutFor(Qt::Key_F, Qt::NoModifier, settings), ToolKind::Magnifier);

    EXPECT_FALSE(toolShortcutFor(Qt::Key_S, Qt::NoModifier, settings).has_value()) << "s 没绑任何工具，不该把当前工具换掉";
    EXPECT_FALSE(toolShortcutFor(Qt::Key_Z, Qt::NoModifier, settings).has_value()) << "z 只归 Ctrl+Z 那一档用";
    EXPECT_FALSE(toolShortcutFor(Qt::Key_T, Qt::ControlModifier, settings).has_value()) << "带修饰键的字母要交回原组合，不能当成换工具";
    EXPECT_FALSE(toolShortcutFor(Qt::Key_Escape, Qt::NoModifier, settings).has_value()) << "非字母键不该落进这张表";
}

/**
 * @brief 钉住「用户在设置页换过一颗键」之后分派当场跟着换
 * @details 这条钉的是「键位只住在清册一处」：画笔换成 K 之后 K 换画笔、P 不再换画笔，
 *          两处只要有一处还读默认值，按钮提示与按下的结果就分家。
 *          顺带钉两位边界：换成带修饰键的组合也算（那一颗组合从此归这一档），
 *          填歪的那一串落回默认那颗而不是替用户猜一颗键。
 */
TEST(ViewShortcuts, ReboundToolKeyFollowsTheRegistry)
{
    const auto      iniPath = std::filesystem::temp_directory_path() / std::format("GyanisScribe-ToolKeys-{}.ini", ::testing::UnitTest::GetInstance()->current_test_info()->name());
    std::error_code ignored;
    std::filesystem::remove(iniPath, ignored);
    SettingsStore settings{iniPath};

    ASSERT_TRUE(settings.setValue(GyanisScribe::Gui::appShortcutKey("Annotation/Pen").toStdString(), QStringLiteral("K")).has_value()) << "写入失败";
    EXPECT_EQ(toolShortcutFor(Qt::Key_K, Qt::NoModifier, settings), ToolKind::Pen);
    EXPECT_FALSE(toolShortcutFor(Qt::Key_P, Qt::NoModifier, settings).has_value()) << "换走之后，旧那颗键不该还认这一档";

    ASSERT_TRUE(settings.setValue(GyanisScribe::Gui::appShortcutKey("Annotation/Text").toStdString(), QStringLiteral("Ctrl+K")).has_value()) << "写入失败";
    EXPECT_EQ(toolShortcutFor(Qt::Key_K, Qt::ControlModifier, settings), ToolKind::Text) << "换成组合键也算：整颗组合相等就命中";
    EXPECT_FALSE(toolShortcutFor(Qt::Key_T, Qt::NoModifier, settings).has_value()) << "文字换走之后 T 不再切工具";

    ASSERT_TRUE(settings.setValue(GyanisScribe::Gui::appShortcutKey("Annotation/Arrow").toStdString(), QStringLiteral("Ctrl+")).has_value()) << "写入失败";
    EXPECT_EQ(toolShortcutFor(Qt::Key_A, Qt::NoModifier, settings), ToolKind::Arrow) << "填歪的那一档要落回默认键";
}

/**
 * @brief 钉住按字母换工具时，引擎与按钮态一起跟上
 * @details 引擎是真源：只改按钮不改引擎，按 T 之后画出来的还是矩形；只改引擎不改按钮，
 *          屏幕上亮着的是另一档。两处必须同一批到位。
 */
TEST_F(AnnotationWindowFixture, LetterKeySwitchesToolInEngineAndToolbarTogether)
{
    auto window = makeWindow();

    // 登记表里文字排第 8 枚（下标 7）：选择、矩形、椭圆、箭头、画笔、高亮、马赛克、文字
    pressKeyOn(*window, Qt::Key_T);
    EXPECT_EQ(engine().activeTool(), ToolKind::Text) << "按 T 没换成文字工具";
    EXPECT_TRUE(window->toolbar().toolButtonAt(7)->isChecked()) << "引擎换了工具，按钮还亮在原来那一档";
    EXPECT_FALSE(window->toolbar().toolButtonAt(0)->isChecked()) << "两枚工具按钮同时亮着，用户看不出自己在用哪一档";

    // Ctrl+Z 走的还是撤销那一条：没有可撤的一步也不该顺手把当前工具改掉
    pressKeyOn(*window, Qt::Key_Z, Qt::ControlModifier);
    EXPECT_EQ(engine().activeTool(), ToolKind::Text) << "Ctrl+Z 把当前工具改走了";
}

/**
 * @brief 钉住工具按钮的提示里带着那枚字母
 * @details 键位只在登记表写一处，界面提示必须跟着它走：漏了提示等于功能存在但没人找得到。
 *          无障碍名要保持工具本名——读屏报「矩形 (R)」会把键位念成名字的一部分。
 */
TEST_F(AnnotationWindowFixture, ToolButtonTooltipsCarryTheirShortcutLetter)
{
    auto window = makeWindow();

    EXPECT_EQ(window->toolbar().toolButtonAt(0)->toolTip(), QString::fromUtf8("选择 (V)"));
    EXPECT_EQ(window->toolbar().toolButtonAt(1)->toolTip(), QString::fromUtf8("矩形 (R)"));
    EXPECT_EQ(window->toolbar().toolButtonAt(7)->toolTip(), QString::fromUtf8("文字 (T)"));
    EXPECT_EQ(window->toolbar().toolButtonAt(0)->accessibleName(), QString::fromUtf8("选择"));
}

/**
 * @brief 钉住选中项的包围盒随选中走
 * @details 界面画选框只问引擎要矩形，所以这条是「界面拿得到正确位置」的判据：
 *          左上角落在起笔处，右下角按线宽余量覆盖到收笔处。
 */
TEST_F(AnnotationWindowFixture, SelectionBoundsFollowTheSelectedShape)
{
    engine().beginStroke({10, 10});
    engine().moveStroke({60, 40});
    const auto settled = engine().endStroke();
    ASSERT_TRUE(settled.has_value()) << settled.error().message;
    // 引擎收笔时不会自己选中（返回值与「当前选中」是两件事），选中由界面决定
    ASSERT_TRUE(engine().selectionBounds() == std::nullopt) << "没选中的时候也要返回矩形，界面就会画出个凭空的选框";
    ASSERT_TRUE(engine().select(*settled).has_value());

    const auto bounds = engine().selectionBounds();
    ASSERT_TRUE(bounds.has_value());
    EXPECT_EQ(bounds->topLeft(), QPoint(10, 10));
    EXPECT_GE(bounds->bottomRight().x(), 60);
    EXPECT_GE(bounds->bottomRight().y(), 40);
    EXPECT_LE(bounds->width(), 60 + 8) << "外扩过头说明包围盒算法与线宽脱了钩";
}

/**
 * @brief 钉住视图的鼠标三拍确实走完了画一条图形
 * @details 合成事件按视图坐标发：这条同时验坐标换算、起笔/行进/收笔的顺序、
 *          以及「刚画完的那条被选中」——三段里任何一段接错，用户看到的就是画不出或拖不动。
 */
TEST_F(AnnotationWindowFixture, ViewDragsOutARectangleAndSelectsIt)
{
    GyanisScribe::Gui::AnnotationView view{engine()};
    view.resize(400, 200);
    // 画布逻辑 200x100 放进 400x200 的视图：比例 2.0、无留白，控件坐标就是逻辑坐标乘 2
    const auto press   = QPoint{20, 20};
    const auto release = QPoint{120, 100};

    for (const auto &[type, position]: {std::pair{QEvent::MouseButtonPress, press}, std::pair{QEvent::MouseMove, QPoint{70, 60}}, std::pair{QEvent::MouseButtonRelease, release}})
    {
        QMouseEvent event{type, QPointF{position}, QPointF{position}, Qt::LeftButton, Qt::LeftButton, Qt::NoModifier};
        QApplication::sendEvent(&view, &event);
    }

    EXPECT_TRUE(engine().selectionBounds().has_value()) << "拖完一条矩形却没有选中项，坐标换算或三拍顺序至少有一段没接对";
    EXPECT_TRUE(engine().canUndo()) << "这一笔没进撤销栈";
    const auto bounds = engine().selectionBounds();
    // 控件 (20,20) → 逻辑 (10,10)，(120,100) → 逻辑 (60,50)
    EXPECT_EQ(bounds->topLeft(), QPoint(10, 10));
    EXPECT_GE(bounds->bottomRight().y(), 50);
}

/**
 * @brief 钉住没选中与取消选中时包围盒为空
 * @details 空包围盒要真返回空：界面把 nullopt 当成 0x0 矩形画，就会在左上角多出一个假选框
 */
TEST_F(AnnotationWindowFixture, SelectionBoundsIsEmptyWithoutSelection)
{
    EXPECT_FALSE(engine().selectionBounds().has_value());

    engine().beginStroke({5, 5});
    engine().moveStroke({45, 35});
    ASSERT_TRUE(engine().endStroke().has_value());
    EXPECT_FALSE(engine().selectionBounds().has_value());
}

/**
 * @brief 钉住「分派表认得方向键」之后确实接到了引擎：一格 1 像素，Shift 那档 10 像素
 * @details 只测映射证不了接线：窗口没把这一格交给引擎时，键表照样全绿而界面上按了没反应。
 *          这条用一扇真的窗口投事件，再回头问引擎要选中项的包围盒。
 */
TEST_F(AnnotationWindowFixture, ArrowKeysNudgeTheSelectedShape)
{
    auto       window = makeWindow();
    const auto before = drawAndSelect({10, 10}, {60, 40});
    ASSERT_TRUE(before.has_value());

    pressKeyOn(*window, Qt::Key_Right);
    const auto oneStep = engine().selectionBounds();
    ASSERT_TRUE(oneStep.has_value());
    EXPECT_EQ(oneStep->topLeft(), before->topLeft() + QPoint(1, 0)) << "一次方向键没挪一格";

    pressKeyOn(*window, Qt::Key_Down, Qt::ShiftModifier);
    const auto coarse = engine().selectionBounds();
    ASSERT_TRUE(coarse.has_value());
    EXPECT_EQ(coarse->topLeft(), oneStep->topLeft() + QPoint(0, 10)) << "Shift 那一档该走大步，仍是一格都不许少";

    // 带 Ctrl 的方向键不归微调管：交回 Qt 之后这里的包围盒必须一动不动
    pressKeyOn(*window, Qt::Key_Left, Qt::ControlModifier);
    EXPECT_EQ(engine().selectionBounds(), coarse) << "Ctrl+方向键被截去做了微调";
}

/**
 * @brief 拒绝面：没有选中项时方向键不产出位移，也不往撤销栈里塞空动作
 * @details 引擎这条路径本来就要拒绝（文案写明「请先点中要移动的那一条」），
 *          但栈里多出一条空动作会让下一次 Ctrl+Z 撤掉一个看不见的东西。
 */
TEST_F(AnnotationWindowFixture, ArrowKeysWithoutASelectionMoveNothing)
{
    auto window = makeWindow();

    pressKeyOn(*window, Qt::Key_Right);
    pressKeyOn(*window, Qt::Key_Down, Qt::ShiftModifier);

    EXPECT_FALSE(engine().selectionBounds().has_value()) << "没选中项却被挪出了一个包围盒";
    EXPECT_FALSE(engine().canUndo()) << "这两次按键各往撤销栈塞了一条空动作";
}

/**
 * @brief 钉住「双击一条已放下的文字」真的原地打开来改
 * @details 走的是真事件：双击 → 输入框带着原文出现 → 改内容 → Ctrl+回车提交。
 *          判据落在引擎那一份内容上，而不是输入框的显示——界面把框填对了但没提交，
 *          用户看到的仍然是旧文字。
 */
TEST_F(AnnotationWindowFixture, DoubleClickReEditsPlacedText)
{
    GyanisScribe::Gui::AnnotationView view{engine()};
    view.resize(400, 200);
    // 画布逻辑 200×100 放进 400×200 的视图：比例 2.0，控件坐标 = 逻辑坐标乘 2
    ASSERT_TRUE(engine().setActiveTool(GyanisScribe::Core::ToolKind::Text).has_value());
    engine().beginStroke(QPoint(20, 20));
    ASSERT_TRUE(engine().setPendingText(QStringLiteral("AB")).has_value());
    const auto placed = engine().endStroke();
    ASSERT_TRUE(placed.has_value()) << placed.error().message;
    ASSERT_TRUE(engine().select(*placed).has_value());

    const auto bounds = engine().selectionBounds();
    ASSERT_TRUE(bounds.has_value());
    const QPoint widgetCenter{(bounds->center().x() + 1) * 2, (bounds->center().y() + 1) * 2};

    QMouseEvent doubleClick{QEvent::MouseButtonDblClick, QPointF{widgetCenter}, QPointF{widgetCenter}, Qt::LeftButton, Qt::NoButton, Qt::NoModifier};
    QApplication::sendEvent(&view, &doubleClick);

    auto *editor = view.findChild<QTextEdit *>();
    ASSERT_NE(editor, nullptr) << "双击一条文字没有打开输入框";
    EXPECT_EQ(editor->toPlainText(), QStringLiteral("AB")) << "输入框没带着原文打开，用户得重打一遍";

    editor->setPlainText(QStringLiteral("CD"));
    pressKeyOn(*editor, Qt::Key_Return, Qt::ControlModifier);

    EXPECT_EQ(engine().textOfSelection().value_or(QString{}), QStringLiteral("CD")) << "Ctrl+回车提交没把内容写回那条图形";
    ASSERT_TRUE(engine().undo().has_value());
    EXPECT_EQ(engine().textOfSelection().value_or(QString{}), QStringLiteral("AB")) << "改文字没进撤销栈，Ctrl+Z 回不到改之前";
}

/**
 * @brief 钉住多行输入的分工：回车在框里换行、Ctrl+回车才提交，且提交前引擎里一个字都没动
 * @details 单行时代回车即提交（QLineEdit 的 editingFinished）；多行之后这个分工反过来。
 *          两次判据一次钉住两边——回车后框里多了一行而图形上还是原文（回车若还当提交，
 *          半截内容当场就落定了），Ctrl+回车才把两行一起写回去。
 */
TEST_F(AnnotationWindowFixture, EnterInsertsALineAndControlEnterCommits)
{
    GyanisScribe::Gui::AnnotationView view{engine()};
    view.resize(400, 200);
    ASSERT_TRUE(engine().setActiveTool(GyanisScribe::Core::ToolKind::Text).has_value());
    engine().beginStroke(QPoint(20, 20));
    ASSERT_TRUE(engine().setPendingText(QStringLiteral("AB")).has_value());
    const auto placed = engine().endStroke();
    ASSERT_TRUE(placed.has_value()) << placed.error().message;
    ASSERT_TRUE(engine().select(*placed).has_value());

    const auto bounds = engine().selectionBounds();
    ASSERT_TRUE(bounds.has_value());
    const QPoint widgetCenter{(bounds->center().x() + 1) * 2, (bounds->center().y() + 1) * 2};

    QMouseEvent doubleClick{QEvent::MouseButtonDblClick, QPointF{widgetCenter}, QPointF{widgetCenter}, Qt::LeftButton, Qt::NoButton, Qt::NoModifier};
    QApplication::sendEvent(&view, &doubleClick);

    auto *editor = view.findChild<QTextEdit *>();
    ASSERT_NE(editor, nullptr) << "双击一条文字没有打开输入框";
    EXPECT_FALSE(editor->textCursor().hasSelection()) << "重编打开时全选了：多行之下按个回车就把原文整段替掉";

    pressKeyOn(*editor, Qt::Key_Return);
    EXPECT_EQ(editor->toPlainText(), QStringLiteral("AB\n")) << "回车没在框里换行";
    EXPECT_EQ(engine().textOfSelection().value_or(QString{}), QStringLiteral("AB")) << "回车把半截内容提交了：多行编辑里它只负责换行";

    editor->moveCursor(QTextCursor::End);
    editor->insertPlainText(QStringLiteral("CD"));

    pressKeyOn(*editor, Qt::Key_Return, Qt::ControlModifier);
    EXPECT_EQ(engine().textOfSelection().value_or(QString{}), QStringLiteral("AB\nCD")) << "Ctrl+回车没把两行都写回那条图形";
    QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
    EXPECT_EQ(view.findChild<QTextEdit *>(), nullptr) << "提交之后输入框还挂在视图上";
}

/**
 * @brief 钉住提交时对空行的取舍：尾巴上的空行削掉、中间的空行是内容要留着
 * @details 尾随空行多半是「敲完回车就提交」带出来的，留着会在图形里多出一行空白
 *          （底板与选中框都跟着多一格）；而不挨着末尾的空行是用户分出来的段，
 *          删掉就是把排版改了。
 */
TEST_F(AnnotationWindowFixture, TrailingBlankLinesAreTrimmedButInnerOnesKept)
{
    GyanisScribe::Gui::AnnotationView view{engine()};
    view.resize(400, 200);
    ASSERT_TRUE(engine().setActiveTool(GyanisScribe::Core::ToolKind::Text).has_value());
    engine().beginStroke(QPoint(20, 20));
    ASSERT_TRUE(engine().setPendingText(QStringLiteral("AB")).has_value());
    const auto placed = engine().endStroke();
    ASSERT_TRUE(placed.has_value()) << placed.error().message;
    ASSERT_TRUE(engine().select(*placed).has_value());

    const auto openEditor = [&view, this]
    {
        // 上一轮收场是 deleteLater：先把欠下的销毁跑掉，免得 findChild 捡到还没死的那一只——
        // 那样后面敲的字全进了僵尸，引擎那几条断言就成了恒真的假绿
        QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);

        const auto bounds = engine().selectionBounds();
        if (!bounds.has_value())
        {
            return static_cast<QTextEdit *>(nullptr);
        }
        const QPoint widgetCenter{(bounds->center().x() + 1) * 2, (bounds->center().y() + 1) * 2};
        QMouseEvent  doubleClick{QEvent::MouseButtonDblClick, QPointF{widgetCenter}, QPointF{widgetCenter}, Qt::LeftButton, Qt::NoButton, Qt::NoModifier};
        QApplication::sendEvent(&view, &doubleClick);
        EXPECT_EQ(view.findChildren<QTextEdit *>().size(), 1) << "视图里同时活着的输入框不止一只，下面的判据可能打在僵尸身上";
        return view.findChild<QTextEdit *>();
    };

    auto *editor = openEditor();
    ASSERT_NE(editor, nullptr) << "双击一条文字没有打开输入框";
    editor->setPlainText(QStringLiteral("A\n\nB"));
    pressKeyOn(*editor, Qt::Key_Return, Qt::ControlModifier);
    EXPECT_EQ(engine().textOfSelection().value_or(QString{}), QStringLiteral("A\n\nB")) << "中间的空行被削掉了，用户分的段没了";

    editor = openEditor();
    ASSERT_NE(editor, nullptr) << "第二次双击没有重开输入框";
    editor->setPlainText(QStringLiteral("A\n\nB\n\n"));
    pressKeyOn(*editor, Qt::Key_Return, Qt::ControlModifier);
    EXPECT_EQ(engine().textOfSelection().value_or(QString{}), QStringLiteral("A\n\nB")) << "尾巴上的空行落了地，图形里多出一行空白";
}

/**
 * @brief 钉住新画一条时的 Esc：输入框收起、画布上不留半个图形、撤销栈里不落东西
 * @details 文字是点一下落的，起笔之后由输入框决定这条算不算数；取消路径上引擎会收一次
 *          空笔（空文本会被判退化而丢弃），这一条就是那个「丢弃」的回归判据。
 */
TEST_F(AnnotationWindowFixture, EscCancelsTheEditorWithoutLeavingAShape)
{
    GyanisScribe::Gui::AnnotationView view{engine()};
    view.resize(400, 200);
    ASSERT_TRUE(engine().setActiveTool(GyanisScribe::Core::ToolKind::Text).has_value());

    // 画布逻辑 200×100 放进 400×200 的视图：控件 (40, 40) 落在空白处的画布 (20, 20)
    QMouseEvent press{QEvent::MouseButtonPress, QPointF{40, 40}, QPointF{40, 40}, Qt::LeftButton, Qt::LeftButton, Qt::NoModifier};
    QApplication::sendEvent(&view, &press);

    auto *editor = view.findChild<QTextEdit *>();
    ASSERT_NE(editor, nullptr) << "文字工具下点一下画布没有打开输入框";
    editor->setPlainText(QStringLiteral("AB"));
    pressKeyOn(*editor, Qt::Key_Escape);

    QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
    EXPECT_EQ(view.findChild<QTextEdit *>(), nullptr) << "Esc 之后输入框还挂在视图上";
    EXPECT_FALSE(engine().canUndo()) << "取消掉的那条进了撤销栈（或半截内容落了地）";
}
/**
 * @brief 钉住改号键只在序号工具在用的时候认
 * @details 别的工具在用的时候 `+` 该交回 Qt：它是键盘上的普通字符，不该偷偷改某个工具的档。
 */
TEST_F(AnnotationWindowFixture, NumberKeysAdjustTheNextNumberOnlyForThatTool)
{
    auto window = makeWindow();

    // 换到矩形：`+` 不该动序号
    pressKeyOn(*window, Qt::Key_R);
    const auto before = engine().nextNumber();
    pressKeyOn(*window, Qt::Key_Plus);
    EXPECT_EQ(engine().nextNumber(), before) << "别的工具在用的时候 `+` 改了序号";

    pressKeyOn(*window, Qt::Key_N);
    pressKeyOn(*window, Qt::Key_Plus);
    EXPECT_EQ(engine().nextNumber(), before + 1) << "序号工具在用，`+` 没把下一个号加一";
    pressKeyOn(*window, Qt::Key_Minus);
    EXPECT_EQ(engine().nextNumber(), before) << "`-` 没把号退回来";

    pressKeyOn(*window, Qt::Key_Plus);
    pressKeyOn(*window, Qt::Key_0);
    EXPECT_EQ(engine().nextNumber(), 1) << "`0` 该回到自动口径：一条序号都没有时下一个号是 1";
}

/**
 * @brief 钉住双向箭头开关从工具栏一路走到引擎：点一下按钮，选中的单头箭头当场变双向，撤销回单头
 * @details 窗口把工具栏的五项参数汇成画笔包再下发，这条验的正是「双向」那一项在这条链上：
 *          漏了它按钮会按下去，选中的那条却不动。取样区手算：窗口建起时下发的是工具栏初值
 *          （线宽 4），画布 200x100、倍比 2，起点端上侧的物理 (54..64, 126..128) 一带
 *          在单头箭头下必然是底色（杆身只到 y=124），双向后起点端三角盖到这里。
 */
TEST_F(AnnotationWindowFixture, DoubleArrowToggleFromToolbarReachesTheEngine)
{
    auto window = makeWindow();

    ASSERT_TRUE(engine().setActiveTool(ToolKind::Arrow).has_value());
    engine().beginStroke(QPoint(20, 60));
    engine().moveStroke(QPoint(180, 60));
    const auto arrow = engine().endStroke();
    ASSERT_TRUE(arrow.has_value()) << arrow.error().message;
    ASSERT_TRUE(engine().select(*arrow).has_value());

    const auto countInkAtStartHead = [this]
    {
        const QImage drawn = engine().exportImage();
        int          ink   = 0;
        for (int x = 54; x < 64; ++x)
        {
            for (int y = 126; y < 128; ++y)
            {
                if (drawn.pixelColor(x, y) != QColor(180, 180, 180))
                {
                    ++ink;
                }
            }
        }
        return ink;
    };
    EXPECT_EQ(countInkAtStartHead(), 0) << "单头箭头在起点端的翼区不该有墨";

    QToolButton *doubleButton = nullptr;
    for (auto *candidate: window->toolbar().findChildren<QToolButton *>())
    {
        if (candidate->accessibleName() == QString::fromUtf8("双向箭头"))
        {
            doubleButton = candidate;
            break;
        }
    }
    ASSERT_NE(doubleButton, nullptr) << "工具栏上找不到双向箭头按钮";
    doubleButton->click();

    EXPECT_GT(countInkAtStartHead(), 0) << "按钮点下去了，选中的那条没有跟着变双向：窗口到引擎这条链没接上";
    ASSERT_TRUE(engine().undo().has_value());
    EXPECT_EQ(countInkAtStartHead(), 0) << "撤销没把双向退回单头，改开关没进撤销栈";
}

/**
 * @brief 钉住切工具从窗口一路不改选中图形：工具栏换色只是下一笔用什么
 * @details 箭头与矩形各记一格色，切到箭头时工具栏会沿用箭头那格；旧实现把它发成
 *          colorChanged，桥按用户改样式把选中的矩形刷成新色并压一步撤销。
 *          判据落在像素与撤销步两处：撤销一次该把矩形整条收走，而不是先退颜色。
 */
TEST_F(AnnotationWindowFixture, SwitchingToolFromToolbarKeepsTheSelectedShapeStyle)
{
    auto window = makeWindow();

    const auto toolButton = [&window](const QString &label) -> QToolButton *
    {
        for (auto *candidate: window->toolbar().findChildren<QToolButton *>())
        {
            if (candidate->accessibleName() == label)
            {
                return candidate;
            }
        }
        return nullptr;
    };
    const auto pickColor = [&window](const QString &label)
    {
        auto *menu = window->toolbar().findChild<QMenu *>(QStringLiteral("annotationColorMenu"));
        EXPECT_NE(menu, nullptr) << "工具栏上没有颜色菜单";
        if (menu == nullptr)
        {
            return;
        }
        QMetaObject::invokeMethod(menu, "aboutToShow", Qt::DirectConnection);
        for (auto *swatch: menu->findChildren<QToolButton *>())
        {
            if (swatch->accessibleName() == label)
            {
                swatch->click();
                return;
            }
        }
        ADD_FAILURE() << "颜色菜单里没有「" << label.toStdString() << "」这一格";
    };

    // 箭头记蓝、矩形记红：此刻没有选中项，换色只改默认值
    auto *arrowButton = toolButton(QString::fromUtf8("箭头"));
    ASSERT_NE(arrowButton, nullptr) << "工具栏上找不到箭头按钮";
    arrowButton->click();
    pickColor(QString::fromUtf8("颜色 5"));
    auto *rectangleButton = toolButton(QString::fromUtf8("矩形"));
    ASSERT_NE(rectangleButton, nullptr) << "工具栏上找不到矩形按钮";
    rectangleButton->click();
    pickColor(QString::fromUtf8("颜色 1"));

    engine().beginStroke(QPoint(20, 20));
    engine().moveStroke(QPoint(80, 60));
    const auto box = engine().endStroke();
    ASSERT_TRUE(box.has_value()) << box.error().message;
    ASSERT_TRUE(engine().select(*box).has_value());
    // 画布倍比 2：逻辑 (20,40) 落在物理 (40,80)，与既有像素用例同一个探点
    ASSERT_EQ(engine().exportImage().pixelColor(40, 80), QColor(232, 62, 62)) << "红矩形没画上，后面的对照没有基准";

    // 切到箭头：工具栏沿用箭头那格蓝，但这不是用户对选中矩形的改色
    arrowButton->click();
    EXPECT_EQ(engine().activeTool(), ToolKind::Arrow) << "工具没切过去，这条判据的前提没成立";
    EXPECT_EQ(engine().exportImage().pixelColor(40, 80), QColor(232, 62, 62)) << "切工具把选中矩形刷成了新工具的颜色";

    // 切过来的工具得真按箭头那格参数落笔：按钮显示的是蓝，画出来也得是蓝。
    // 画在矩形下方：逻辑 (20,80)-(180,80)，杆身中点在物理 (200,160)，不与矩形重叠
    engine().beginStroke(QPoint(20, 80));
    engine().moveStroke(QPoint(180, 80));
    const auto arrow = engine().endStroke();
    ASSERT_TRUE(arrow.has_value()) << arrow.error().message;
    EXPECT_EQ(engine().exportImage().pixelColor(200, 160), GyanisScribe::Core::kAnnotationPaletteColors[4])
            << "切工具只换了按钮显示，箭头仍按矩形那格红落笔：桥没把工具栏参数写回默认值";

    // 先撤掉刚画的箭头，再撤一步把矩形整条收走：撤销栈里该只有两次插入，没有改样式那一步
    ASSERT_TRUE(engine().undo().has_value());
    EXPECT_EQ(engine().exportImage().pixelColor(200, 160), QColor(180, 180, 180)) << "这一撤没把刚画的箭头收走";
    ASSERT_TRUE(engine().undo().has_value());
    EXPECT_EQ(engine().exportImage().pixelColor(40, 80), QColor(180, 180, 180)) << "切工具多压了一步改样式，这一撤退的是颜色";
}

/**
 * @brief 钉住高亮加宽按钮从窗口接到引擎：按一下（中→宽）选中的那条跟着加宽
 * @details 画布 400×200、逻辑 200×100：高亮画在逻辑 y=70（物理 140），线宽 8。
 *          窄档（2×）带宽 16 物理格，探点 (320,110) 在带外；宽档（8×）带宽 64，探点进带内。
 *          探点在灰色底上与墨色的差别用「不等于底色」数格子——与双向箭头那条同一套判据。
 */
TEST_F(AnnotationWindowFixture, HighlightWidthCycleFromToolbarReachesTheEngine)
{
    auto window = makeWindow();

    ASSERT_TRUE(engine().setActiveTool(ToolKind::Highlight).has_value());
    ASSERT_TRUE(
            engine().setPenParameters(PenParameters{.color = QColor(255, 255, 0), .lineWidth = 8, .fontSize = 16, .fillStyle = 0, .highlightAlpha = 110, .highlightWidthFactor = 2})
                    .has_value());
    engine().beginStroke(QPoint(20, 70));
    engine().moveStroke(QPoint(180, 70));
    const auto highlight = engine().endStroke();
    ASSERT_TRUE(highlight.has_value()) << highlight.error().message;
    ASSERT_TRUE(engine().select(*highlight).has_value());

    const auto countInkAtProbe = [this]
    {
        const QImage drawn = engine().exportImage();
        int          ink   = 0;
        for (int x = 316; x < 325; ++x)
        {
            for (int y = 106; y < 115; ++y)
            {
                if (drawn.pixelColor(x, y) != QColor(180, 180, 180))
                {
                    ++ink;
                }
            }
        }
        return ink;
    };
    EXPECT_EQ(countInkAtProbe(), 0) << "窄档在探点处不该有墨，后面的对照就没有基准";

    QToolButton *widthButton = nullptr;
    for (auto *candidate: window->toolbar().findChildren<QToolButton *>())
    {
        if (candidate->accessibleName().startsWith(QString::fromUtf8("高亮加宽")))
        {
            widthButton = candidate;
            break;
        }
    }
    ASSERT_NE(widthButton, nullptr) << "工具栏上找不到高亮加宽按钮";
    // 工具栏的默认档是「中」（4×）：点一下到「宽」（8×），探点进带内。
    // 注意别按「窄 → 中 → 宽」来点——从「中」出发两下会绕回「窄」，探点又出带，读数会假装链路没接上
    widthButton->click();

    EXPECT_GT(countInkAtProbe(), 0) << "按钮点下去了，选中的那条没有跟着加宽：窗口到引擎这条链没接上";
}

/**
 * @brief 钉住椭圆形状开关从窗口接到引擎：点开之后新画的高亮两头才有圆帽
 * @details 工具栏默认线宽 4、加宽 4 倍：笔宽 16、圆帽半径 8。端点外 6 逻辑像素处
 *          （物理 252 那一列）圆帽下该有墨、平头下没有；再往外的 268 列圆帽也不该到。
 *          两笔一前一后画在同一条底图上，长出帽沿的那一笔只能是开关按下去的功劳。
 */
TEST_F(AnnotationWindowFixture, EllipticalToggleFromToolbarReachesTheEngine)
{
    auto window = makeWindow();

    ASSERT_TRUE(engine().setActiveTool(ToolKind::Highlight).has_value());
    engine().beginStroke(QPoint(60, 50));
    engine().moveStroke(QPoint(120, 50));
    ASSERT_TRUE(engine().endStroke().has_value());
    EXPECT_EQ(engine().exportImage().pixelColor(252, 100), QColor(180, 180, 180)) << "平头档在探点处就有墨，后面的对照没有基准";

    QToolButton *ellipticalButton = nullptr;
    for (auto *candidate: window->toolbar().findChildren<QToolButton *>())
    {
        if (candidate->accessibleName().startsWith(QString::fromUtf8("椭圆形状")))
        {
            ellipticalButton = candidate;
            break;
        }
    }
    ASSERT_NE(ellipticalButton, nullptr) << "工具栏上找不到椭圆形状按钮";
    ellipticalButton->click();

    engine().beginStroke(QPoint(60, 70));
    engine().moveStroke(QPoint(120, 70));
    ASSERT_TRUE(engine().endStroke().has_value());

    const auto drawn = engine().exportImage();
    EXPECT_NE(drawn.pixelColor(252, 140), QColor(180, 180, 180)) << "开关点下去了，新画的笔迹两头没有圆帽：窗口到引擎这条链没接上";
    EXPECT_EQ(drawn.pixelColor(268, 140), QColor(180, 180, 180)) << "帽沿伸过了自己的半径：圆帽画成了拉长的方块";
}

/**
 * @brief 钉住描边色按钮从窗口接到引擎：点成「白」档之后新画的文字描边才是白的
 * @details 工具栏默认字色是调色板第一格红 (232,62,62)，亮度 147 → 自动档的反差色是黑；
 *          所以判别取「白档」：桥接不上时新字会拿到自动档的黑描边，亮格一个都不会有。
 */
TEST_F(AnnotationWindowFixture, TextStrokeColorFromToolbarReachesTheEngine)
{
    auto window = makeWindow();

    const auto brightIn = [this](const QRect &box)
    {
        const QImage drawn  = engine().exportImage();
        int          bright = 0;
        for (int y = box.top(); y <= box.bottom(); ++y)
        {
            for (int x = box.left(); x <= box.right(); ++x)
            {
                const auto pixel = drawn.pixelColor(x, y);
                if (pixel.red() > 200 && pixel.green() > 200 && pixel.blue() > 200)
                {
                    ++bright;
                }
            }
        }
        return bright;
    };

    ASSERT_TRUE(engine().setActiveTool(ToolKind::Text).has_value());
    engine().beginStroke(QPoint(20, 60));
    ASSERT_TRUE(engine().setPendingText(QStringLiteral("H")).has_value());
    ASSERT_TRUE(engine().endStroke().has_value());
    EXPECT_EQ(brightIn(QRect(20, 10, 280, 180)), 0) << "不打描边时窗口里出了亮格，后面的对照没有基准";

    QToolButton *outlineButton     = nullptr;
    QToolButton *strokeColorButton = nullptr;
    for (auto *candidate: window->toolbar().findChildren<QToolButton *>())
    {
        if (candidate->accessibleName() == QString::fromUtf8("描边"))
        {
            outlineButton = candidate;
        }
        if (candidate->accessibleName() == QString::fromUtf8("描边色"))
        {
            strokeColorButton = candidate;
        }
    }
    ASSERT_NE(outlineButton, nullptr) << "工具栏上找不到描边按钮";
    ASSERT_NE(strokeColorButton, nullptr) << "工具栏上找不到描边色按钮";
    outlineButton->click();
    strokeColorButton->click(); // 自动 → 白

    engine().beginStroke(QPoint(160, 60));
    ASSERT_TRUE(engine().setPendingText(QStringLiteral("H")).has_value());
    ASSERT_TRUE(engine().endStroke().has_value());
    EXPECT_GT(brightIn(QRect(300, 10, 100, 180)), 0) << "按钮点下去了，新画的文字描边不是白的：窗口到引擎这条链没接上";
}

/**
 * @brief 钉住计数组从窗口接到引擎：点过按钮之后落的牌从第 2 组的 1 重新起数
 * @details 先用第 1 组落一条（下一个号变 2），再点「计数组」：引擎的组号与下一个号
 *          同时该换到第 2 组那一套——两处都钉住，桥断在哪一半都跑不掉。
 */
TEST_F(AnnotationWindowFixture, NumberGroupFromToolbarReachesTheEngine)
{
    auto window = makeWindow();

    ASSERT_TRUE(engine().setActiveTool(ToolKind::Number).has_value());
    engine().beginStroke(QPoint(30, 30));
    ASSERT_TRUE(engine().endStroke().has_value());
    EXPECT_EQ(engine().nextNumber(), 2) << "第 1 组落了一条，下一个号该是 2";

    QToolButton *groupButton = nullptr;
    for (auto *candidate: window->toolbar().findChildren<QToolButton *>())
    {
        if (candidate->accessibleName() == QString::fromUtf8("计数组"))
        {
            groupButton = candidate;
            break;
        }
    }
    ASSERT_NE(groupButton, nullptr) << "工具栏上找不到计数组按钮";
    groupButton->click();

    EXPECT_EQ(engine().numberGroup(), 2) << "按钮点下去了，引擎的计数组没跟着切：窗口到引擎这条链没接上";
    EXPECT_EQ(engine().nextNumber(), 1) << "切到第 2 组之后下一个号该回到 1";
}

/**
 * @brief 钉住刷新截图的窗口两半：F5 报出「哪块屏、哪块区域」，refreshCapture 换底图保标注
 * @details 窗口自己不碰抓屏来源：F5 只把这一轮的开窗现场（屏名 + 区域）报出去，宿主抓回来
 *          再经 refreshCapture 换上去。两半分别钉：报错了地方宿主就抓错屏，换的时候清了图形
 *          用户就丢标注。
 */
TEST_F(AnnotationWindowFixture, RefreshShortcutReportsTheRegionAndKeepsTheAnnotations)
{
    auto window = makeWindow();

    // 摆一轮带来源信息的开窗现场：刷新要知道重抓哪块地方
    auto captured          = makeCanvas({200, 100});
    captured.screenName    = QStringLiteral("FakeScreen-9");
    captured.logicalRegion = QRect(40, 50, 200, 100);
    window->startAnnotation(captured, QRect(0, 0, 1920, 1080), QRect(40, 50, 200, 100));

    QString seenScreen;
    QRect   seenRegion;
    QObject::connect(window.get(), &AnnotationWindow::refreshRequested,
                     [&](const QString &screenName, const QRect &region)
                     {
                         seenScreen = screenName;
                         seenRegion = region;
                     });

    ASSERT_TRUE(engine().setActiveTool(ToolKind::Rectangle).has_value());
    engine().beginStroke(QPoint(20, 20));
    engine().moveStroke(QPoint(80, 60));
    ASSERT_TRUE(engine().endStroke().has_value());

    pressKeyOn(*window, Qt::Key_F5);
    EXPECT_EQ(seenScreen, QStringLiteral("FakeScreen-9")) << "F5 没把屏幕名报出来，宿主不知道抓哪块屏";
    EXPECT_EQ(seenRegion, QRect(40, 50, 200, 100)) << "F5 没把区域原样报出来，重抓会抓到别处";

    // 宿主把新帧送回来：底图换了、画的那条还在（物理探测：逻辑坐标乘 2）
    auto fresh = makeCanvas({200, 100});
    fresh.image.fill(QColor(0, 0, 255));
    fresh.screenName    = captured.screenName;
    fresh.logicalRegion = captured.logicalRegion;
    window->refreshCapture(fresh);

    const auto drawn = engine().exportImage();
    EXPECT_EQ(drawn.pixelColor(300, 160), QColor(0, 0, 255)) << "新底图没换上";
    // 字色取的是工具栏调色板第一格（桥构造时就把整套参数压给了引擎），不是引擎自带的纯红
    EXPECT_EQ(drawn.pixelColor(40, 80), QColor(232, 62, 62)) << "刷新把画好的标注抹掉了：该走 refreshCanvas 而不是 loadCanvas";
}

/**
 * @brief 钉住线型按钮从窗口接到引擎：点一下（实线→虚线），选中的那条断口就出来了
 * @details 画布逻辑 200×100 放进 400×200 的视图：一条逻辑 (20,50)-(180,50) 的横线；
 *          判据量与引擎级那条同一套——同一段里「有墨的列数」，改档之后变小。
 */
TEST_F(AnnotationWindowFixture, LineStyleCycleFromToolbarReachesTheEngine)
{
    auto window = makeWindow();

    ASSERT_TRUE(engine().setActiveTool(ToolKind::Line).has_value());
    ASSERT_TRUE(engine().setPenParameters(PenParameters{.color = QColor(255, 0, 0), .lineWidth = 4, .fontSize = 16, .fillStyle = 0}).has_value());
    engine().beginStroke(QPoint(20, 50));
    engine().moveStroke(QPoint(180, 50));
    const auto line = engine().endStroke();
    ASSERT_TRUE(line.has_value()) << line.error().message;
    ASSERT_TRUE(engine().select(*line).has_value());

    const auto inkedColumns = [this]
    {
        const QImage drawn = engine().exportImage();
        int          ink   = 0;
        for (int x = 40; x <= 360; ++x)
        {
            for (int dy = -8; dy <= 8; ++dy)
            {
                if (drawn.pixelColor(x, 100 + dy) != QColor(180, 180, 180))
                {
                    ++ink;
                    break;
                }
            }
        }
        return ink;
    };

    const int solid = inkedColumns();
    ASSERT_GT(solid, 250) << "实线没铺满，后面的对照没有基准";

    QToolButton *styleButton = nullptr;
    for (auto *candidate: window->toolbar().findChildren<QToolButton *>())
    {
        if (candidate->accessibleName() == QString::fromUtf8("线型"))
        {
            styleButton = candidate;
            break;
        }
    }
    ASSERT_NE(styleButton, nullptr) << "工具栏上找不到线型按钮";
    styleButton->click(); // 实线 → 虚线

    EXPECT_LT(inkedColumns(), solid) << "按钮点下去了，选中的那条线还是满的：窗口到引擎这条链没接上";
}

/**
 * @brief 钉住箭头款式与大小两枚按钮从窗口接到引擎：点一下，选中的那条跟着变
 * @details 画布逻辑 200×100 放进 400×200 的视图（2 倍），箭头尖在逻辑 (140, 50) → 物理 (280, 100)。
 *          探点全按工具栏自己的读数算几何——按按钮时桥会把工具栏整套参数（含线宽）下发给选中项，
 *          所以起点线宽就取工具栏那一档，点击前后只差款式/大小这一个变量。
 *          款式探点取三角的重心（离尖 2/3 纵深处）：实心有芯、空心透白、开口是伸到尖上的杆（有墨）。
 *          大小探点取「中档头够不着、大档头够得着」的一列（两档纵深的中间）：只有杆 vs 切到头的芯。
 */
TEST_F(AnnotationWindowFixture, ArrowHeadButtonsFromToolbarReachTheEngine)
{
    auto window = makeWindow();

    ASSERT_TRUE(engine().setActiveTool(ToolKind::Arrow).has_value());
    ASSERT_TRUE(engine().setPenParameters(PenParameters{.color = QColor(255, 0, 0), .lineWidth = window->toolbar().currentWidth(), .fontSize = 16, .fillStyle = 0}).has_value());
    engine().beginStroke(QPoint(20, 50));
    engine().moveStroke(QPoint(140, 50));
    const auto arrow = engine().endStroke();
    ASSERT_TRUE(arrow.has_value()) << arrow.error().message;
    ASSERT_TRUE(engine().select(*arrow).has_value());

    // 张角 60°（两翼各与杆成 30°）、头长 = 大小档 × 线宽：都按工具栏读数现算，物理像素 = 逻辑 × 2
    constexpr double kHalfAngle  = 30.0 * std::numbers::pi / 180.0;
    const qreal      scale       = window->toolbar().currentArrowHeadScale();
    const qreal      headLength  = scale * static_cast<qreal>(window->toolbar().currentWidth()) * 2.0;
    const qreal      headDepth   = headLength * std::cos(kHalfAngle);
    const int        tipX        = 280;
    const int        interiorX   = tipX - static_cast<int>(std::lround(headDepth * 2.0 / 3.0));
    const int        outsideMidX = tipX - static_cast<int>(std::lround(headDepth * (1.0 + GyanisScribe::Core::kArrowHeadScales.back() / scale) / 2.0));

    const auto innerInk = [this, interiorX]
    {
        const QImage drawn = engine().exportImage();
        int          ink   = 0;
        for (int x = interiorX - 1; x <= interiorX + 1; ++x)
        {
            for (int y = 99; y <= 101; ++y)
            {
                if (drawn.pixelColor(x, y) != QColor(180, 180, 180))
                {
                    ++ink;
                }
            }
        }
        return ink;
    };
    ASSERT_GT(innerInk(), 0) << "实心档在头的重心处没有芯，后面的对照没有基准（探点几何与线宽档对不上了）";

    const auto buttonLabelled = [&window](const QString &prefix) -> QToolButton *
    {
        for (auto *candidate: window->toolbar().findChildren<QToolButton *>())
        {
            if (candidate->accessibleName().startsWith(prefix))
            {
                return candidate;
            }
        }
        return nullptr;
    };

    auto *styleButton = buttonLabelled(QString::fromUtf8("箭头样式"));
    ASSERT_NE(styleButton, nullptr) << "工具栏上找不到箭头样式按钮";
    styleButton->click(); // 实心 → 空心
    EXPECT_EQ(innerInk(), 0) << "点成空心之后头里还有芯：款式按钮到引擎这条链没接上";
    styleButton->click(); // 空心 → 开口
    EXPECT_GT(innerInk(), 0) << "再点到开口，重心处又没有墨：开口档的杆没伸到尖上（或按钮没接上）";
    styleButton->click(); // 开口 → 实心：把款式转回实心，下面的大小对照才只差一个变量
    ASSERT_GT(innerInk(), 0) << "三档转回来不是实心";

    const auto inkRowsInColumn = [this](const int x)
    {
        const QImage drawn = engine().exportImage();
        int          rows  = 0;
        for (int y = 60; y <= 140; ++y)
        {
            if (drawn.pixelColor(x, y) != QColor(180, 180, 180))
            {
                ++rows;
            }
        }
        return rows;
    };
    const int shaftRows = inkRowsInColumn(outsideMidX);
    ASSERT_GT(shaftRows, 0) << "中档在那一列连杆都没切到，后面的对照没有基准";

    auto *scaleButton = buttonLabelled(QString::fromUtf8("箭头大小"));
    ASSERT_NE(scaleButton, nullptr) << "工具栏上找不到箭头大小按钮";
    scaleButton->click(); // 中 → 大
    EXPECT_GT(inkRowsInColumn(outsideMidX), shaftRows * 2) << "点成大档之后那一列还是只有杆：大小按钮到引擎这条链没接上";
}

/**
 * @brief 钉住从视图拖缩放手柄：一趟手势把选中项拉大，抬笔后一次撤销退回
 * @details 手柄判定要压过普通命中：按点就落在右边线上，不先问手柄的话这一按会被当成「拖图形」。
 *          视图 400x200、画布逻辑 200x100：控件坐标 = 逻辑坐标乘 2。
 *          画的矩形是 QRect(10,10,51,41)，转成外接框后右中手柄在逻辑 (61,30.5)、控件 (122,61)。
 */
TEST_F(AnnotationWindowFixture, ViewHandleDragResizesTheSelectedShape)
{
    GyanisScribe::Gui::AnnotationView view{engine()};
    view.resize(400, 200);

    engine().beginStroke({10, 10});
    engine().moveStroke({60, 50});
    const auto settled = engine().endStroke();
    ASSERT_TRUE(settled.has_value()) << settled.error().message;
    ASSERT_TRUE(engine().select(*settled).has_value());

    for (const auto &[type, position]:
         {std::pair{QEvent::MouseButtonPress, QPoint{122, 61}}, std::pair{QEvent::MouseMove, QPoint{162, 61}}, std::pair{QEvent::MouseButtonRelease, QPoint{162, 61}}})
    {
        QMouseEvent event{type, QPointF{position}, QPointF{position}, Qt::LeftButton, Qt::LeftButton, Qt::NoModifier};
        QApplication::sendEvent(&view, &event);
    }

    const auto grown = engine().selectionBounds();
    ASSERT_TRUE(grown.has_value());
    EXPECT_NEAR(grown->right(), 80, 2.5) << "手柄没把右边拖到逻辑 81 一带（控件 162 的一半）";
    // 图形的左边界原位还在（物理坐标：逻辑 10 乘 2，正落在描边中线上）：这趟是缩放，不是整条挪走
    EXPECT_EQ(engine().exportImage().pixelColor(20, 60), QColor(255, 0, 0)) << "左边界不在了：这不像缩放";

    ASSERT_TRUE(engine().undo().has_value());
    const auto back = engine().selectionBounds();
    ASSERT_TRUE(back.has_value());
    EXPECT_NEAR(back->right(), 60, 1.5) << "一次撤销没回到拖手柄之前";
}

/**
 * @brief 钉住修饰键从鼠标事件一路走到几何：按住 Shift 拖角手柄＝等比例缩放
 * @details 画的是 51×51 的正方形，右下角拖到逻辑 (81,70)：两轴比值 71/51 与 60/51，
 *          等比例档取大的那一轴定倍率，另一轴跟乘到 71。不带这一档时高会停在 60——
 *          断言「高追上了宽」才能证明 Shift 真进了几何，而不是把普通缩放当成了等比例。
 */
TEST_F(AnnotationWindowFixture, ViewKeepsTheAspectRatioWhenShiftIsHeld)
{
    GyanisScribe::Gui::AnnotationView view{engine()};
    view.resize(400, 200);

    engine().beginStroke({10, 10});
    engine().moveStroke({60, 60});
    const auto settled = engine().endStroke();
    ASSERT_TRUE(settled.has_value()) << settled.error().message;
    ASSERT_TRUE(engine().select(*settled).has_value());

    // 右下角手柄在逻辑 (61,61)：视图 400x200、画布逻辑 200x100，控件坐标是逻辑坐标的两倍
    for (const auto &[type, position]:
         {std::pair{QEvent::MouseButtonPress, QPoint{122, 122}}, std::pair{QEvent::MouseMove, QPoint{162, 140}}, std::pair{QEvent::MouseButtonRelease, QPoint{162, 140}}})
    {
        QMouseEvent event{type, QPointF{position}, QPointF{position}, Qt::LeftButton, Qt::LeftButton, Qt::ShiftModifier};
        QApplication::sendEvent(&view, &event);
    }

    const auto grown = engine().selectionBounds();
    ASSERT_TRUE(grown.has_value());
    EXPECT_NEAR(grown->width(), 71.0, 2.5) << "宽没按变化大的那一轴倍率走";
    EXPECT_NEAR(grown->height(), grown->width(), 2.0) << "Shift 拖完长宽比没保住（修饰键没走到几何）";
    EXPECT_NEAR(grown->left(), 10.0, 2.5) << "等比例缩放该锚住左与上两条边";
    EXPECT_NEAR(grown->top(), 10.0, 2.5);
}

/**
 * @brief 钉住 Ctrl/Shift 点选多选与「多选后拖任意一条挪整组」
 * @details 点选走合成事件：先裸点选中 A，再 Ctrl 点 B 并进选中集；
 *          多选状态下按住任意一条拖动，整组一起挪，一步撤销整组回位。
 */
TEST_F(AnnotationWindowFixture, ViewMultiSelectsWithModifiersAndDragsTheWholeGroup)
{
    GyanisScribe::Gui::AnnotationView view{engine()};
    view.resize(400, 200);

    // 两条矩形：A 逻辑 (10,10)-(40,40)，B 逻辑 (100,10)-(130,40)
    engine().beginStroke({10, 10});
    engine().moveStroke({40, 40});
    ASSERT_TRUE(engine().endStroke().has_value());
    engine().beginStroke({100, 10});
    engine().moveStroke({130, 40});
    ASSERT_TRUE(engine().endStroke().has_value());

    const auto click = [&view](const QPoint &widgetPoint, const Qt::KeyboardModifiers modifiers)
    {
        for (const auto type: {QEvent::MouseButtonPress, QEvent::MouseButtonRelease})
        {
            QMouseEvent event{type, QPointF{widgetPoint}, QPointF{widgetPoint}, Qt::LeftButton, Qt::LeftButton, modifiers};
            QApplication::sendEvent(&view, &event);
        }
    };

    click(QPoint{50, 50}, Qt::NoModifier); // A 中心：逻辑 (25,25)
    EXPECT_EQ(engine().selections().size(), 1U) << "裸点没有单选上那一条";
    click(QPoint{230, 50}, Qt::ControlModifier); // B 中心：逻辑 (115,25)
    EXPECT_EQ(engine().selections().size(), 2U) << "Ctrl 点选没把第二条并进选中集";
    click(QPoint{230, 50}, Qt::ShiftModifier);
    EXPECT_EQ(engine().selections().size(), 2U) << "Shift 加选已选中的一条造出了重复项";

    // 拖 A（已在选中集里）：整组一起挪逻辑 (10,10)
    for (const auto &[type, position]:
         {std::pair{QEvent::MouseButtonPress, QPoint{50, 50}}, std::pair{QEvent::MouseMove, QPoint{70, 70}}, std::pair{QEvent::MouseButtonRelease, QPoint{70, 70}}})
    {
        QMouseEvent event{type, QPointF{position}, QPointF{position}, Qt::LeftButton, Qt::LeftButton, Qt::NoModifier};
        QApplication::sendEvent(&view, &event);
    }
    EXPECT_EQ(engine().selections().size(), 2U) << "拖动把多选塌缩成了单选";

    const auto moved = engine().selectionBounds();
    ASSERT_TRUE(moved.has_value());
    EXPECT_EQ(moved->topLeft(), QPoint(20, 20)) << "整组没跟着一起挪";
    EXPECT_EQ(moved->bottomRight(), QPoint(140, 50)) << "只挪了被按住的那一条";

    ASSERT_TRUE(engine().undo().has_value());
    const auto back = engine().selectionBounds();
    ASSERT_TRUE(back.has_value());
    EXPECT_EQ(back->topLeft(), QPoint(10, 10)) << "一次撤销没把整组挪回去";
}

/**
 * @brief 钉住点空白：先清掉选中再起笔，选框不会留在「刚才那条」上
 */
TEST_F(AnnotationWindowFixture, ViewClickingBlankClearsTheSelection)
{
    GyanisScribe::Gui::AnnotationView view{engine()};
    view.resize(400, 200);

    const auto settled = drawAndSelect({10, 10}, {60, 40});
    ASSERT_TRUE(settled.has_value());
    ASSERT_EQ(engine().selections().size(), 1U);

    for (const auto &[type, position]: {std::pair{QEvent::MouseButtonPress, QPoint{380, 180}}, std::pair{QEvent::MouseButtonRelease, QPoint{380, 180}}})
    {
        QMouseEvent event{type, QPointF{position}, QPointF{position}, Qt::LeftButton, Qt::LeftButton, Qt::NoModifier};
        QApplication::sendEvent(&view, &event);
    }

    EXPECT_TRUE(engine().selections().empty()) << "点空白没清掉选中，选框会留在刚才那条上";
    // 点一下没拖动：矩形退化不收，撤销栈里不该多出条目——撤一次该轮到原来那条的插入
    ASSERT_TRUE(engine().undo().has_value());
    EXPECT_FALSE(engine().selectionAt(QPoint(10, 25)).has_value()) << "撤销栈里多出了空动作，这一撤没撤到原来那条";
}

/**
 * @brief 钉住从视图抓旋转手柄：拖到正右方，图形绕中心转 90°
 * @details 旋转柄按上边中点再往上一个固定偏移摆；起点在正上方（-90° 方向）、
 *          终点在正右方，增量正好 +90°。判据取手柄框转角与像素两处。
 */
TEST_F(AnnotationWindowFixture, ViewRotateHandleTurnsTheSelectedShape)
{
    GyanisScribe::Gui::AnnotationView view{engine()};
    view.resize(400, 200);

    // 逻辑 (40,40)-(120,60) 的矩形；控件坐标 = 逻辑乘 2
    engine().beginStroke({40, 40});
    engine().moveStroke({120, 60});
    const auto settled = engine().endStroke();
    ASSERT_TRUE(settled.has_value()) << settled.error().message;
    ASSERT_TRUE(engine().select(*settled).has_value());

    // 上边中点逻辑 (80.5,40)（QRect 含右边界，框心落在 80.5）→ 控件 (161,80)；
    // 旋转柄再往上 24 控件像素 → (161,56)。终点取正右方：增量正好 +90°
    for (const auto &[type, position]:
         {std::pair{QEvent::MouseButtonPress, QPoint{161, 56}}, std::pair{QEvent::MouseMove, QPoint{261, 101}}, std::pair{QEvent::MouseButtonRelease, QPoint{261, 101}}})
    {
        QMouseEvent event{type, QPointF{position}, QPointF{position}, Qt::LeftButton, Qt::LeftButton, Qt::NoModifier};
        QApplication::sendEvent(&view, &event);
    }

    const auto frame = engine().selectionFrame();
    ASSERT_TRUE(frame.has_value());
    EXPECT_NEAR((*frame)[0].x(), 91.0, 1.5) << "转 90° 后左上角该转到 (91,10) 一带";
    EXPECT_NEAR((*frame)[0].y(), 10.0, 1.5);
    // 物理坐标：转前上边线在逻辑 y=40（物理 80）；转完该处是内里，上边线立到逻辑 x=91（物理 182）
    const auto turned = engine().exportImage();
    EXPECT_EQ(turned.pixelColor(160, 80), QColor(180, 180, 180)) << "转完之后原上边线那里该是底色";
    EXPECT_EQ(turned.pixelColor(182, 100), QColor(255, 0, 0)) << "原来的上边线转到右侧：物理 x=182 一带该有墨";

    ASSERT_TRUE(engine().undo().has_value());
    EXPECT_EQ(engine().exportImage().pixelColor(160, 80), QColor(255, 0, 0)) << "撤销没把旋转退回去";
}

/**
 * @brief 钉住 Ctrl+C/Ctrl+V 的窗口分派：有选中复制图形，没选中退回复制整图
 */
TEST_F(AnnotationWindowFixture, CopyPasteShortcutsCopyTheShapeAndFallBackToImageCopy)
{
    auto window      = makeWindow();
    auto imageCopies = 0;
    QObject::connect(window.get(), &AnnotationWindow::copyRequested, [&imageCopies](const QImage &) { ++imageCopies; });

    // 没选中：Ctrl+C 走老习惯（复制整张成品），不收进图形剪贴板
    pressKeyOn(*window, Qt::Key_C, Qt::ControlModifier);
    EXPECT_EQ(imageCopies, 1) << "没选中时 Ctrl+C 没退回复制整图";
    EXPECT_TRUE(engine().selections().empty());

    // 画一条并选中：Ctrl+C 复制图形、Ctrl+V 粘出带偏移的副本，界面不收场
    ASSERT_TRUE(drawAndSelect({10, 10}, {60, 40}).has_value());
    pressKeyOn(*window, Qt::Key_C, Qt::ControlModifier);
    pressKeyOn(*window, Qt::Key_V, Qt::ControlModifier);
    EXPECT_EQ(imageCopies, 1) << "有选中时 Ctrl+C 把整张图也交出去了";
    ASSERT_EQ(engine().selections().size(), 1U) << "粘出来的副本该成为新的选中集";
    const auto bounds = engine().selectionBounds();
    ASSERT_TRUE(bounds.has_value());
    EXPECT_EQ(bounds->topLeft(), QPoint(18, 18)) << "副本没带一格偏移";
}

/**
 * @brief 钉住 Ctrl+P 打印分派：交出去的是烘好的成品，而且窗口不收场
 * @details 打印与别的出口不同这一点要钉住：对话框取消、打完还想接着改，现场都得留着。
 *          不钉的话下一轮「统一收口」会顺手把它并进收场那一组。
 */
TEST_F(AnnotationWindowFixture, PrintShortcutEmitsTheBakedExportAndKeepsTheWindow)
{
    auto window = makeWindow();
    // 走一遍真开局：窗口与工具栏都要先在屏上，后面的「没收场」断言才有意义
    window->startAnnotation(makeCanvas({200, 100}), QRect{0, 0, 1920, 1080}, QRect{0, 0, 200, 100});
    ASSERT_TRUE(window->isVisible());

    auto   printCalls = 0;
    QImage printed;
    QObject::connect(window.get(), &AnnotationWindow::printRequested,
                     [&](const QImage &image)
                     {
                         ++printCalls;
                         printed = image;
                     });

    ASSERT_TRUE(drawAndSelect({10, 10}, {60, 40}).has_value());
    pressKeyOn(*window, Qt::Key_P, Qt::ControlModifier);

    EXPECT_EQ(printCalls, 1) << "Ctrl+P 没走到打印那一路";
    ASSERT_FALSE(printed.isNull());
    EXPECT_EQ(printed.size(), QSize(400, 200)) << "打印交出去的不是画布的物理尺寸";
    // 交出去的是烘好图形的成品：矩形上边框那一段在物理图上不会是底色的灰
    EXPECT_NE(printed.pixelColor(60, 20), QColor(180, 180, 180)) << "打印的是底图，不是画上图形的成品";
    EXPECT_TRUE(window->isVisible()) << "打印把窗口收了：取消对话框之后编辑现场就没了";
    EXPECT_TRUE(window->toolbar().isVisible()) << "打印把工具栏也收了";
}

/**
 * @brief 钉住视图按下与抬起之间是一趟拖动，两趟各占一步撤销
 * @details 引擎按手势号分界，视图得在按下与抬起时各标一次；漏标的话两趟拖动共用
 *          同一个手势号，位移命令按目标列表并成一步，用户按一次 Ctrl+Z 会退回两趟之前。
 */
TEST_F(AnnotationWindowFixture, TwoSeparateViewDragsUndoIndependently)
{
    GyanisScribe::Gui::AnnotationView view{engine()};
    view.resize(400, 200);
    // 画布逻辑 200x100 放进 400x200 的视图：控件坐标是逻辑坐标的两倍
    engine().beginStroke({10, 10});
    engine().moveStroke({60, 40});
    const auto settled = engine().endStroke();
    ASSERT_TRUE(settled.has_value()) << settled.error().message;
    ASSERT_TRUE(engine().select(*settled).has_value());

    const auto drag = [&view](const QPoint &from, const QPoint &to)
    {
        for (const auto &[type, position]: {std::pair{QEvent::MouseButtonPress, from}, std::pair{QEvent::MouseMove, to}, std::pair{QEvent::MouseButtonRelease, to}})
        {
            QMouseEvent event{type, QPointF{position}, QPointF{position}, Qt::LeftButton, Qt::LeftButton, Qt::NoModifier};
            QApplication::sendEvent(&view, &event);
        }
    };

    // 两趟各挪逻辑 (10,10)：控件坐标是逻辑坐标的两倍
    drag(QPoint{60, 60}, QPoint{80, 80});
    drag(QPoint{100, 80}, QPoint{120, 100});

    const auto moved = engine().selectionBounds();
    ASSERT_TRUE(moved.has_value());
    EXPECT_EQ(moved->topLeft(), QPoint(30, 30)) << "两趟拖动没各挪一格";

    ASSERT_TRUE(engine().undo().has_value());
    const auto afterOneUndo = engine().selectionBounds();
    ASSERT_TRUE(afterOneUndo.has_value()) << "撤销拖动不该把选中项一起作废";
    EXPECT_EQ(afterOneUndo->topLeft(), QPoint(20, 20)) << "一次撤销把两趟独立拖动都退了：视图没在按下/抬起时标手势边界";
}

/**
 * @brief 钉住松手之后的方向键微调另算一步撤销
 * @details 视图在抬起时补一次手势边界，松手之后的 moveSelectionBy()（方向键微调）才会另起一步；
 *          漏掉它时微调与刚结束的拖动同号，位移命令并成一步，一次撤销把拖动与微调都退回去。
 */
TEST_F(AnnotationWindowFixture, DragThenNudgeUndoIndependently)
{
    GyanisScribe::Gui::AnnotationView view{engine()};
    view.resize(400, 200);
    // 画布逻辑 200x100 放进 400x200 的视图：控件坐标是逻辑坐标的两倍
    engine().beginStroke({10, 10});
    engine().moveStroke({60, 40});
    const auto settled = engine().endStroke();
    ASSERT_TRUE(settled.has_value()) << settled.error().message;
    ASSERT_TRUE(engine().select(*settled).has_value());

    // 拖一趟：控件 (70,50) 按下、(90,70) 松手，逻辑位移 (10,10)
    for (const auto &[type, position]: {std::pair{QEvent::MouseButtonPress, QPoint{70, 50}}, std::pair{QEvent::MouseMove, QPoint{90, 70}},
                                       std::pair{QEvent::MouseButtonRelease, QPoint{90, 70}}})
    {
        QMouseEvent event{type, QPointF{position}, QPointF{position}, Qt::LeftButton, Qt::LeftButton, Qt::NoModifier};
        QApplication::sendEvent(&view, &event);
    }
    const auto dragged = engine().selectionBounds();
    ASSERT_TRUE(dragged.has_value());
    EXPECT_EQ(dragged->topLeft(), QPoint(20, 20)) << "这一趟拖动没把图形挪到逻辑 (20,20)";

    // 松手之后按方向键：直接走引擎的位移出口，逻辑再向右挪 5
    ASSERT_TRUE(engine().moveSelectionBy(QPoint(5, 0)).has_value());
    const auto nudged = engine().selectionBounds();
    ASSERT_TRUE(nudged.has_value());
    EXPECT_EQ(nudged->topLeft(), QPoint(25, 20)) << "微调没落到图形上";

    ASSERT_TRUE(engine().undo().has_value());
    const auto afterOneUndo = engine().selectionBounds();
    ASSERT_TRUE(afterOneUndo.has_value()) << "撤销微调不该把选中项一起作废";
    EXPECT_EQ(afterOneUndo->topLeft(), QPoint(20, 20)) << "一次撤销把拖动与微调都退了：松手时没标手势边界";
}

/**
 * @brief 钉住工具栏最右那枚 X：一点就把窗口与工具栏一起收起，与 Esc 同一条路
 * @details 工具栏是独立顶层窗，只藏窗口会把它留在桌面上；这条判据两个 visible 都断。
 */
TEST_F(AnnotationWindowFixture, ToolbarCloseButtonHidesBothWindows)
{
    auto window = makeWindow();
    window->startAnnotation(makeCanvas({200, 100}), QRect{0, 0, 1920, 1080}, QRect{0, 0, 200, 100});
    ASSERT_TRUE(window->isVisible());
    ASSERT_TRUE(window->toolbar().isVisible());

    QToolButton *closeButton = nullptr;
    for (auto *candidate: window->toolbar().findChildren<QToolButton *>())
    {
        if (candidate->accessibleName() == QString::fromUtf8("关闭标注"))
        {
            closeButton = candidate;
        }
    }
    ASSERT_NE(closeButton, nullptr) << "工具栏上找不到关闭按钮";
    closeButton->click();

    EXPECT_FALSE(window->isVisible()) << "点了 X 标注窗还留在屏上";
    EXPECT_FALSE(window->toolbar().isVisible()) << "点了 X 工具栏还浮在桌面上";
}

/**
 * @brief 钉住关闭信号：Esc 收起这一轮标注时，宿主能收到「标注窗已关」
 * @details 回溯流程靠这一声恢复抓帧；少发一次，回溯缓冲就永远停在 CaptureFlow。
 *          判据只认一次按下的结果，窗口与工具栏的可见态一起断。
 */
TEST_F(AnnotationWindowFixture, EscapeEmitsClosedOnce)
{
    auto window = makeWindow();
    window->startAnnotation(makeCanvas({200, 100}), QRect{0, 0, 1920, 1080}, QRect{0, 0, 200, 100});
    ASSERT_TRUE(window->isVisible());
    ASSERT_TRUE(window->toolbar().isVisible());

    QSignalSpy closedSpy(window.get(), &AnnotationWindow::closed);
    ASSERT_TRUE(closedSpy.isValid());

    pressKeyOn(*window, Qt::Key_Escape);

    EXPECT_FALSE(window->isVisible()) << "按了 Esc 标注窗还留在屏上";
    EXPECT_FALSE(window->toolbar().isVisible()) << "按了 Esc 工具栏还浮在桌面上";
    EXPECT_EQ(closedSpy.count(), 1) << "一次关闭发了 " << closedSpy.count() << " 次 closed()";
}

/**
 * @brief 钉住四个交付出口也报「标注窗已关」，且与 Esc 不会重复报
 * @details 回车复制、Ctrl+S、Ctrl+D、Ctrl+O 在 deliver 里收窗口交成品，与 Esc/X 是同一个终点；
 *          少发这一声，回溯缓冲会停在 CaptureFlow。每档各开一轮新窗口，判交付一次、窗口收起、
 *          closed 一次；交付之后再按 Esc，计数不许再涨。
 */
TEST_F(AnnotationWindowFixture, DeliveryExitsEmitClosedOnce)
{
    using DeliverySignal = void (AnnotationWindow::*)(const QImage &);
    const std::vector<std::tuple<Qt::Key, Qt::KeyboardModifiers, DeliverySignal>> cases{
        {Qt::Key_Return, Qt::NoModifier, &AnnotationWindow::copyRequested},
        {Qt::Key_S, Qt::ControlModifier, &AnnotationWindow::saveRequested},
        {Qt::Key_D, Qt::ControlModifier, &AnnotationWindow::pinRequested},
        {Qt::Key_O, Qt::ControlModifier, &AnnotationWindow::ocrRequested},
    };

    for (const auto &[key, modifiers, signal]: cases)
    {
        auto window = makeWindow();
        window->startAnnotation(makeCanvas({200, 100}), QRect{0, 0, 1920, 1080}, QRect{0, 0, 200, 100});
        ASSERT_TRUE(window->isVisible());
        ASSERT_TRUE(window->toolbar().isVisible());

        int deliveries = 0;
        QObject::connect(window.get(), signal, window.get(), [&deliveries](const QImage &) { ++deliveries; });
        QSignalSpy closedSpy(window.get(), &AnnotationWindow::closed);
        ASSERT_TRUE(closedSpy.isValid());

        pressKeyOn(*window, key, modifiers);

        EXPECT_EQ(deliveries, 1) << "按键 " << static_cast<int>(key) << " 没走到交付出口";
        EXPECT_FALSE(window->isVisible()) << "交付之后标注窗还留在屏上";
        EXPECT_FALSE(window->toolbar().isVisible()) << "交付之后工具栏还浮在桌面上";
        EXPECT_EQ(closedSpy.count(), 1) << "交付出口发了 " << closedSpy.count() << " 次 closed()";

        // 窗口虽已藏起，事件仍能送达：Esc 走同一条收场路时必须被防重挡下
        pressKeyOn(*window, Qt::Key_Escape);
        EXPECT_EQ(closedSpy.count(), 1) << "交付之后再关一次又发了一回 closed()";
    }
}

/**
 * @brief 钉住「识别二维码」按钮：交出去的是烘好的成品，而且窗口不收场（与打印同一口径）
 * @details 与识别文字不同：扫码不另开结果窗，认没认出来都该留在标注窗里——多半只是想知道
 *          「这里头是什么」，顺手关掉现场是最烦的一种收场。
 */
TEST_F(AnnotationWindowFixture, ScanButtonEmitsTheBakedExportAndKeepsTheWindow)
{
    auto window = makeWindow();
    window->startAnnotation(makeCanvas({200, 100}), QRect{0, 0, 1920, 1080}, QRect{0, 0, 200, 100});
    ASSERT_TRUE(window->isVisible());

    auto   scanCalls = 0;
    QImage scanned;
    QObject::connect(window.get(), &AnnotationWindow::scanRequested,
                     [&](const QImage &image)
                     {
                         ++scanCalls;
                         scanned = image;
                     });

    ASSERT_TRUE(drawAndSelect({10, 10}, {60, 40}).has_value());
    ASSERT_TRUE(window->toolbar().isVisible());

    QToolButton *scanButton = nullptr;
    for (auto *candidate: window->toolbar().findChildren<QToolButton *>())
    {
        if (candidate->accessibleName() == QString::fromUtf8("识别二维码"))
        {
            scanButton = candidate;
            break;
        }
    }
    ASSERT_NE(scanButton, nullptr) << "工具栏上找不到「识别二维码」按钮";
    scanButton->click();

    EXPECT_EQ(scanCalls, 1) << "点了按钮没走到扫码那一路";
    ASSERT_FALSE(scanned.isNull());
    EXPECT_EQ(scanned.size(), QSize(400, 200)) << "交出去的不是画布的物理尺寸";
    EXPECT_NE(scanned.pixelColor(60, 20), QColor(180, 180, 180)) << "交出去的是底图，不是画上图形的成品";
    EXPECT_TRUE(window->isVisible()) << "扫码把窗口收了：认没认出来都该留着编辑现场";
    EXPECT_TRUE(window->toolbar().isVisible());
}

/**
 * @brief 钉住「隐私打码」按钮：交出去的是画布原图，而且窗口不收场
 * @details 隐私扫描认的是底图上的文字与码框，不是烘上图形的成品；入口与扫码同一口径——
 *          检查结果回到宿主再弹审核，编辑现场必须留着。
 */
TEST_F(AnnotationWindowFixture, PrivacyButtonEmitsTheCanvasImageAndKeepsTheWindow)
{
    auto window = makeWindow();
    window->startAnnotation(makeCanvas({200, 100}), QRect{0, 0, 1920, 1080}, QRect{0, 0, 200, 100});
    ASSERT_TRUE(window->isVisible());

    auto   privacyCalls = 0;
    QImage requested;
    QObject::connect(window.get(), &AnnotationWindow::privacyRequested,
                     [&](const QImage &image)
                     {
                         ++privacyCalls;
                         requested = image;
                     });

    ASSERT_TRUE(drawAndSelect({10, 10}, {60, 40}).has_value());
    ASSERT_TRUE(window->toolbar().isVisible());

    QToolButton *privacyButton = nullptr;
    for (auto *candidate: window->toolbar().findChildren<QToolButton *>())
    {
        if (candidate->accessibleName() == QString::fromUtf8("隐私打码"))
        {
            privacyButton = candidate;
            break;
        }
    }
    ASSERT_NE(privacyButton, nullptr) << "工具栏上找不到「隐私打码」按钮";
    privacyButton->click();

    EXPECT_EQ(privacyCalls, 1) << "点了按钮没走到隐私打码那一路";
    ASSERT_FALSE(requested.isNull());
    EXPECT_EQ(requested.size(), QSize(400, 200)) << "交出去的不是画布的物理尺寸";
    EXPECT_EQ(requested.cacheKey(), window->canvas().image.cacheKey()) << "交出去的应当是画布原图，而不是烘上图形的成品";
    EXPECT_TRUE(window->isVisible()) << "隐私打码把窗口收了：检查结果还没回来，编辑现场得留着";
    EXPECT_TRUE(window->toolbar().isVisible());
}

/**
 * @brief 钉住轮次守卫：开窗置真，刷新底图仍在同一轮，收场之后才置假
 * @details 关窗后引擎与画布还留着上一轮内容，异步结果回来时只能靠这一句分辨
 *          「用户还在看这一轮」还是「窗已经关了」。重抓只是换底图，不算收场。
 */
TEST_F(AnnotationWindowFixture, RoundStaysActiveUntilTheWindowCloses)
{
    auto window = makeWindow();
    EXPECT_FALSE(window->isRoundActive()) << "还没开窗就报这一轮在编辑";

    window->startAnnotation(makeCanvas({200, 100}), QRect{0, 0, 1920, 1080}, QRect{0, 0, 200, 100});
    EXPECT_TRUE(window->isRoundActive()) << "开窗之后没报这一轮在编辑";

    window->refreshCapture(makeCanvas({200, 100}));
    EXPECT_TRUE(window->isRoundActive()) << "刷新底图把这一轮当成收场了";

    pressKeyOn(*window, Qt::Key_Escape);
    EXPECT_FALSE(window->isRoundActive()) << "Esc 收起之后还报这一轮在编辑";

    window->startAnnotation(makeCanvas({200, 100}), QRect{0, 0, 1920, 1080}, QRect{0, 0, 200, 100});
    EXPECT_TRUE(window->isRoundActive()) << "新一轮开窗没有把轮次守卫重新立起来";
}

/**
 * @brief 钉住系统关窗（Alt+F4、任务栏关闭）也走收场：closed 只发一次，轮次守卫落下
 * @details 只 hide 不报收场时，迟到的隐私识别结果仍以为这一轮在编辑，会开审核并往看不见的画布上落笔。
 */
TEST_F(AnnotationWindowFixture, CloseEventEndsTheRoundOnce)
{
    auto window = makeWindow();
    QSignalSpy closed{window.get(), &AnnotationWindow::closed};
    ASSERT_TRUE(closed.isValid());

    window->startAnnotation(makeCanvas({200, 100}), QRect{0, 0, 1920, 1080}, QRect{0, 0, 200, 100});
    ASSERT_TRUE(window->isVisible());
    ASSERT_TRUE(window->isRoundActive());

    window->close();
    EXPECT_FALSE(window->isVisible()) << "系统关窗之后窗口还留着";
    EXPECT_FALSE(window->isRoundActive()) << "系统关窗之后这一轮还报「在编辑」";
    EXPECT_EQ(closed.count(), 1) << "系统关窗没有走 closed() 收场，或一轮报了几次";

    window->close();
    EXPECT_EQ(closed.count(), 1) << "重复关窗又报了一次 closed()";
}

/**
 * @brief 钉住 Ctrl+Shift+↑/↓ 与 Home/End 从窗口接到引擎
 * @details 两条实心矩形盖同一块地，中心像素就是层叠读数；四条键各按一次核对读数。
 */
TEST_F(AnnotationWindowFixture, LayerShortcutsReachTheEngine)
{
    auto window = makeWindow();

    ASSERT_TRUE(engine().setPenParameters(PenParameters{.color = QColor{255, 0, 0}, .lineWidth = 2, .fontSize = 16, .fillStyle = 2}).has_value());
    engine().beginStroke({10, 10});
    engine().moveStroke({90, 60});
    const auto first = engine().endStroke();
    ASSERT_TRUE(first.has_value()) << first.error().message;

    ASSERT_TRUE(engine().setPenParameters(PenParameters{.color = QColor{0, 0, 255}, .lineWidth = 2, .fontSize = 16, .fillStyle = 2}).has_value());
    engine().beginStroke({10, 10});
    engine().moveStroke({90, 60});
    ASSERT_TRUE(engine().endStroke().has_value());

    // 物理坐标：(50,35) 逻辑的二倍是 (100,70)
    const auto centerColor = [this] { return engine().exportImage().pixelColor(100, 70); };
    ASSERT_EQ(centerColor(), QColor(0, 0, 255)) << "后画的该在上面，后面的对照就没基准";

    ASSERT_TRUE(engine().select(*first).has_value());
    pressKeyOn(*window, Qt::Key_Up, Qt::ControlModifier | Qt::ShiftModifier);
    EXPECT_EQ(centerColor(), QColor(255, 0, 0)) << "Ctrl+Shift+↑ 没把选中的那条上移一层";

    pressKeyOn(*window, Qt::Key_Home, Qt::ControlModifier | Qt::ShiftModifier);
    EXPECT_EQ(centerColor(), QColor(0, 0, 255)) << "Ctrl+Shift+Home 没把选中的那条置底";

    pressKeyOn(*window, Qt::Key_End, Qt::ControlModifier | Qt::ShiftModifier);
    EXPECT_EQ(centerColor(), QColor(255, 0, 0)) << "Ctrl+Shift+End 没把选中的那条置顶";

    pressKeyOn(*window, Qt::Key_Down, Qt::ControlModifier | Qt::ShiftModifier);
    EXPECT_EQ(centerColor(), QColor(0, 0, 255)) << "Ctrl+Shift+↓ 没把选中的那条下移一层";
}

/**
 * @brief 换工具会按新尺寸把工具栏重摆一次：变宽之后仍以选区为中心
 * @details 可见段随工具收放（矩形档只剩填充/圆角/线型）。宽度 Qt 自己会跟着布局走，
 *          位置不会——这条钉的是 contextLayoutChanged 那条接线独有的产物。期望值不重算
 *          placeToolbar（重算与实现同源会成同义反复），用居中不变式：宽了 Δ，x 该左移 Δ/2。
 */
TEST_F(AnnotationWindowFixture, SwitchingToolsReplacesTheToolbarAgainstTheSelection)
{
    auto window = makeWindow();
    // 选区摆在屏幕中间：两侧都不贴边，重新居中的位移才出得来（贴边时被 clamp 吃掉了）
    window->startAnnotation(makeCanvas({200, 100}), QRect{0, 0, 1920, 1080}, QRect{800, 400, 320, 200});
    ASSERT_TRUE(window->isVisible());

    auto      &toolbar = window->toolbar();
    const auto before  = toolbar.geometry();
    EXPECT_EQ(before.width(), toolbar.sizeHint().width()) << "开局那一次摆放就该用当时的 sizeHint";

    toolbar.setActiveTool(ToolKind::Text);
    const auto after = toolbar.geometry();
    EXPECT_EQ(after.width(), toolbar.sizeHint().width()) << "宽度没跟上新尺寸";
    EXPECT_NE(after.width(), before.width()) << "文字档该比矩形档宽，这条判据的前提没成立";
    EXPECT_NEAR(after.x(), before.x() - (after.width() - before.width()) / 2.0, 1.0) << "收放后没有按选区重新居中：x " << before.x() << " -> " << after.x();
}

/**
 * @brief 读数用例：底图 2560×1440、画布上 30 个图形时，整窗重绘一次要多少毫秒（不断言时间）
 * @details 结论要进 README 的「性能实测」。标注窗每次改动都走整幅 `update()`（`canvasChanged` → `update()`），
 *          成本主要是把底图缩放画一遍、再把图形那一层叠上去，所以这一格要有自己的读数——
 *          「重绘是不是整幅重画」是形状判据，慢不慢只有量了才知道。
 *          它不断言时间：跑在什么机器上就报什么数。
 */
TEST_F(AnnotationWindowFixture, ReportsTheCostOfRepaintingAWindowFullOfShapes)
{
    constexpr int kShapes = 30;

    auto window = makeWindow();
    window->startAnnotation(makeCanvas(QSize(2560, 1440), 1.0), QRect(0, 0, 1920, 1080), QRect(100, 100, 800, 600));
    ASSERT_TRUE(window->isVisible());

    // 三十个矩形铺满画布：图形多到一定程度，重绘成本里图形那一层才看得出来
    for (int index = 0; index < kShapes; ++index)
    {
        const auto x = 40 + (index % 10) * 240;
        const auto y = 40 + (index / 10) * 420;
        engine().beginStroke({x, y});
        engine().moveStroke({x + 180, y + 320});
        ASSERT_TRUE(engine().endStroke().has_value()) << "第 " << index << " 个矩形没画上，后面的读数不成立";
    }

    constexpr int       kRounds = 10;
    std::vector<double> milliseconds;
    milliseconds.reserve(kRounds);
    for (int round = 0; round < kRounds; ++round)
    {
        QElapsedTimer watch;
        watch.start();
        const auto frame = window->grab();
        milliseconds.push_back(static_cast<double>(watch.nsecsElapsed()) / 1'000'000.0);
        ASSERT_FALSE(frame.isNull()) << "抓不到整窗画面，读数不成立";
    }

    std::ranges::sort(milliseconds);
    std::printf("[读数] 标注窗整幅重绘一次：中位 %.1fms（底图 2560×1440、%d 个图形、%d 轮，min %.1f / max %.1f）\n", milliseconds[static_cast<std::size_t>(kRounds) / 2U], kShapes,
                kRounds, milliseconds.front(), milliseconds.back());
}

/**
 * @brief 钉住屏显与导出的图形边缘走同一档抗锯齿
 * @details 屏显走 AnnotationView 的普通分支、导出走 AnnotationEngine::exportImage()，两条路各自设渲染提示；
 *          图形那层漏掉抗锯齿时，斜线与箭头的边缘会在屏上糊出一圈成品里没有的灰边。
 *          判据是同一块区域逐像素相等：四边各留 4 像素，那里只有仅屏显才有的提示线。
 */
TEST_F(AnnotationWindowFixture, ScreenAndExportUseTheSameAntialiasing)
{
    ASSERT_TRUE(engine().setActiveTool(ToolKind::Pen).has_value());
    ASSERT_TRUE(engine().setPenParameters(PenParameters{.color = QColor{255, 0, 0}, .lineWidth = 3, .fontSize = 16, .fillStyle = 0}).has_value());

    // 起笔与行进没有返回值，这一笔能不能落定由收笔那一下说了算
    engine().beginStroke({20, 20});
    engine().moveStroke({180, 80});
    const auto settled = engine().endStroke();
    ASSERT_TRUE(settled.has_value()) << settled.error().message;

    GyanisScribe::Gui::AnnotationView view{engine()};
    view.resize(400, 200);
    ASSERT_EQ(view.size(), QSize(400, 200)) << "控件没按 400x200 摆，下面的坐标对不上";

    // 用 render 而不是 grab：抓屏按控件自己的倍比出图，离屏窗口的倍比不一定是 1，图就不是 400x200 了
    QImage screen{400, 200, QImage::Format_ARGB32};
    screen.fill(Qt::transparent);
    {
        QPainter painter{&screen};
        view.render(&painter);
    }

    const auto exported = engine().exportImage();
    ASSERT_EQ(screen.size(), QSize(400, 200)) << "屏显那一路的图不是 400x200，后面的坐标对不上";
    ASSERT_EQ(exported.size(), QSize(400, 200)) << "导出图不是 400x200，后面的坐标对不上";

    // 阳性对照：屏显上真有一笔红色笔迹，逐像素对照才是在比「同一笔的两条渲染路径」，
    // 两边都空白时差异恒为 0，抗锯齿去掉也照样绿
    int penPixels = 0;
    for (int y = 0; y < screen.height(); ++y)
    {
        for (int x = 0; x < screen.width(); ++x)
        {
            const QColor pixel = screen.pixelColor(x, y);
            if (pixel.red() > 200 && pixel.green() < 80 && pixel.blue() < 80)
            {
                ++penPixels;
            }
        }
    }
    EXPECT_GT(penPixels, 0) << "屏显上找不到笔迹：两边都是空白，逐像素对照等于没比";

    constexpr int kMargin = 4; // 提示线压在控件最外一圈，只上屏、不进成品
    int           differences = 0;
    std::string   firstDifference;
    for (int y = kMargin; y < screen.height() - kMargin; ++y)
    {
        for (int x = kMargin; x < screen.width() - kMargin; ++x)
        {
            const QColor screenColor = screen.pixelColor(x, y);
            const QColor exportColor = exported.pixelColor(x, y);
            if (screenColor == exportColor)
            {
                continue;
            }
            if (differences == 0)
            {
                firstDifference = std::format("第一处差异在 ({}, {})：屏显 ({},{},{},{})、导出 ({},{},{},{})", x, y, screenColor.red(), screenColor.green(), screenColor.blue(),
                                              screenColor.alpha(), exportColor.red(), exportColor.green(), exportColor.blue(), exportColor.alpha());
            }
            ++differences;
        }
    }
    EXPECT_EQ(differences, 0) << firstDifference;
}

/**
 * @brief 钉住矩形在拖动途中就画出来，而不是松手才出现
 * @details 引擎默认工具此前没标成 active，paintPending 直接跳过，用户只能看到松手后的成品。
 */
TEST_F(AnnotationWindowFixture, ThePendingRectangleIsPaintedWhileDragging)
{
    auto window = makeWindow();
    window->startAnnotation(makeCanvas(QSize(200, 100)), QRect(0, 0, 1920, 1080), QRect(100, 100, 200, 100));
    ASSERT_TRUE(engine().setActiveTool(ToolKind::Rectangle).has_value());
    ASSERT_TRUE(engine().setPenParameters(PenParameters{.color = QColor{255, 0, 0}, .lineWidth = 4, .fontSize = 16, .fillStyle = 0}).has_value());

    auto *view = window->findChild<GyanisScribe::Gui::AnnotationView *>();
    ASSERT_NE(view, nullptr);

    QMouseEvent press{QEvent::MouseButtonPress, QPointF{20, 20}, QPointF{20, 20}, Qt::LeftButton, Qt::LeftButton, Qt::NoModifier};
    QApplication::sendEvent(view, &press);
    QMouseEvent move{QEvent::MouseMove, QPointF{120, 80}, QPointF{120, 80}, Qt::NoButton, Qt::LeftButton, Qt::NoModifier};
    QApplication::sendEvent(view, &move);
    QApplication::processEvents();

    const auto shot = view->grab().toImage();
    const auto edge = shot.pixelColor(70, 20);
    EXPECT_GT(edge.red(), 180) << "拖动中的矩形上边没画出来：松手前还是空白";
    EXPECT_LT(edge.green(), 100);
}

/**
 * @brief 钉住「选择」档拖窗口边框能重裁截图范围
 * @details 松手即进标注窗之后，重裁不再回遮罩；窗口把新的全局几何报给宿主，
 *          宿主负责藏窗重抓。这里只钉窗口这一半：几何变了、信号带的是新范围。
 */
TEST_F(AnnotationWindowFixture, SelectToolResizesTheCapturedRegionByDraggingAnEdge)
{
    auto window            = makeWindow();
    auto captured          = makeCanvas(QSize(200, 100));
    captured.screenName    = QStringLiteral("FakeScreen-9");
    captured.logicalRegion = QRect(100, 100, 200, 100);
    window->startAnnotation(captured, QRect(0, 0, 1920, 1080), QRect(100, 100, 200, 100));
    QApplication::processEvents();
    ASSERT_TRUE(engine().setActiveTool(ToolKind::Select).has_value());

    auto *view = window->findChild<GyanisScribe::Gui::AnnotationView *>();
    ASSERT_NE(view, nullptr);

    QString seenScreen;
    QRect   seenSelection;
    QObject::connect(window.get(), &GyanisScribe::Gui::AnnotationWindow::cropRequested,
                     [&](const QString &screenName, const QRect &selection)
                     {
                         seenScreen    = screenName;
                         seenSelection = selection;
                     });

    const auto  globalOf = [view](const QPoint &local) { return QPointF{view->mapToGlobal(local)}; };
    QMouseEvent press{QEvent::MouseButtonPress, QPointF{0, 50}, globalOf(QPoint{0, 50}), Qt::LeftButton, Qt::LeftButton, Qt::NoModifier};
    QApplication::sendEvent(view, &press);
    QMouseEvent move{QEvent::MouseMove, QPointF{-10, 50}, globalOf(QPoint{-10, 50}), Qt::NoButton, Qt::LeftButton, Qt::NoModifier};
    QApplication::sendEvent(view, &move);
    QMouseEvent release{QEvent::MouseButtonRelease, QPointF{-10, 50}, globalOf(QPoint{-10, 50}), Qt::LeftButton, Qt::NoButton, Qt::NoModifier};
    QApplication::sendEvent(view, &release);

    EXPECT_EQ(window->geometry(), QRect(90, 100, 210, 100)) << "拖左边框没有把窗口左边带出去";
    EXPECT_EQ(seenScreen, QStringLiteral("FakeScreen-9"));
    EXPECT_EQ(seenSelection, QRect(90, 100, 210, 100)) << "报给宿主的新范围不是拖出来的那一块";
}

/**
 * @brief 钉住「选择」档按住图内空白能整体挪走截图范围
 * @details 内部空白过去只清选中、拖动没反应；现在超过手抖阈值后整窗跟着走，
 *          松手把新范围交给宿主重抓，标注随后按同一段位移平移。
 */
TEST_F(AnnotationWindowFixture, SelectToolMovesTheCapturedRegionByDraggingInside)
{
    auto window            = makeWindow();
    auto captured          = makeCanvas(QSize(200, 100));
    captured.screenName    = QStringLiteral("FakeScreen-9");
    captured.logicalRegion = QRect(100, 100, 200, 100);
    window->startAnnotation(captured, QRect(0, 0, 1920, 1080), QRect(100, 100, 200, 100));
    QApplication::processEvents();
    ASSERT_TRUE(engine().setActiveTool(ToolKind::Select).has_value());

    auto *view = window->findChild<GyanisScribe::Gui::AnnotationView *>();
    ASSERT_NE(view, nullptr);

    int   crops = 0;
    QRect cropRegion;
    QObject::connect(window.get(), &GyanisScribe::Gui::AnnotationWindow::cropRequested,
                     [&crops, &cropRegion](const QString &, const QRect &selection)
                     {
                         ++crops;
                         cropRegion = selection;
                     });

    const QPointF pressGlobal = view->mapToGlobal(QPoint{100, 50});
    const QPointF moveGlobal  = view->mapToGlobal(QPoint{120, 60});
    QMouseEvent   press{QEvent::MouseButtonPress, QPointF{100, 50}, pressGlobal, Qt::LeftButton, Qt::LeftButton, Qt::NoModifier};
    QApplication::sendEvent(view, &press);
    QMouseEvent move{QEvent::MouseMove, QPointF{120, 60}, moveGlobal, Qt::NoButton, Qt::LeftButton, Qt::NoModifier};
    QApplication::sendEvent(view, &move);
    QApplication::processEvents();

    EXPECT_EQ(window->geometry(), QRect(120, 110, 200, 100)) << "拖图内空白没有把整块截图范围带走";
    EXPECT_EQ(crops, 0) << "还没松手就交出了新范围";

    const auto expectedToolbar = GyanisScribe::Gui::placeToolbar(QRect(0, 0, 1920, 1080), window->toolbar().sizeHint(), QRect(120, 110, 200, 100));
    ASSERT_TRUE(expectedToolbar.has_value());
    EXPECT_EQ(window->toolbar().geometry(), expectedToolbar->frame) << "整体移动途中工具栏没有跟着走";

    QMouseEvent release{QEvent::MouseButtonRelease, QPointF{120, 60}, moveGlobal, Qt::LeftButton, Qt::NoButton, Qt::NoModifier};
    QApplication::sendEvent(view, &release);

    EXPECT_EQ(crops, 1);
    EXPECT_EQ(cropRegion, QRect(120, 110, 200, 100)) << "松手交出的新范围不是拖出来的那一块";
}

/**
 * @brief 钉住图内单击仍只是清选中：新加的整块移动不能把一次点击变成一次重抓
 */
TEST_F(AnnotationWindowFixture, SelectToolClickingInsideOnlyClearsTheShapeSelection)
{
    auto window            = makeWindow();
    auto captured          = makeCanvas(QSize(200, 100));
    captured.screenName    = QStringLiteral("FakeScreen-9");
    captured.logicalRegion = QRect(100, 100, 200, 100);
    window->startAnnotation(captured, QRect(0, 0, 1920, 1080), QRect(100, 100, 200, 100));
    QApplication::processEvents();
    ASSERT_TRUE(drawAndSelect({10, 10}, {80, 60}).has_value());
    ASSERT_TRUE(engine().setActiveTool(ToolKind::Select).has_value());

    auto *view = window->findChild<GyanisScribe::Gui::AnnotationView *>();
    ASSERT_NE(view, nullptr);
    int crops = 0;
    QObject::connect(window.get(), &GyanisScribe::Gui::AnnotationWindow::cropRequested, [&crops](const QString &, const QRect &) { ++crops; });

    const auto    before = window->geometry();
    const QPointF global = view->mapToGlobal(QPoint{150, 25});
    QMouseEvent   press{QEvent::MouseButtonPress, QPointF{150, 25}, global, Qt::LeftButton, Qt::LeftButton, Qt::NoModifier};
    QApplication::sendEvent(view, &press);
    QMouseEvent release{QEvent::MouseButtonRelease, QPointF{150, 25}, global, Qt::LeftButton, Qt::NoButton, Qt::NoModifier};
    QApplication::sendEvent(view, &release);

    EXPECT_EQ(window->geometry(), before) << "单击把窗口挪走了";
    EXPECT_TRUE(engine().selections().empty()) << "单击空白没有清掉图形的选中";
    EXPECT_EQ(crops, 0) << "单击空白不该发起重抓";
}

/**
 * @brief 钉住重裁拖边时放大镜从整屏冻帧取样、松手即收
 * @details 抓点压在窗口边上，放大镜取样的是整屏冻帧而不是拉伸后的画布：
 *          这里把冻帧铺成洋红，放大镜中心就该是洋红；松手之后它不许留在画面上。
 */
TEST_F(AnnotationWindowFixture, CropResizeShowsMagnifierSampledFromTheFrozenFrame)
{
    auto window                  = makeWindow();
    auto captured                = makeCanvas(QSize(400, 400));
    captured.screenName          = QStringLiteral("FakeScreen-9");
    captured.logicalRegion       = QRect(100, 100, 400, 400);
    captured.frozenLogicalSize   = QSize(800, 600);
    captured.frozenLogicalRegion = QRect(QPoint{0, 0}, QSize(800, 600));
    captured.frozenImage         = QImage{QSize{800, 600}, QImage::Format_ARGB32};
    captured.frozenImage.fill(QColor{255, 0, 255});
    window->startAnnotation(captured, QRect(0, 0, 1920, 1080), QRect(100, 100, 400, 400));
    QApplication::processEvents();
    ASSERT_TRUE(engine().setActiveTool(ToolKind::Select).has_value());

    auto *view = window->findChild<GyanisScribe::Gui::AnnotationView *>();
    ASSERT_NE(view, nullptr);

    const auto    pressLocal  = QPoint{399, 150};
    const auto    moveLocal   = QPoint{379, 150};
    const QPointF pressGlobal = view->mapToGlobal(pressLocal);
    const QPointF moveGlobal  = view->mapToGlobal(moveLocal);
    QMouseEvent   press{QEvent::MouseButtonPress, QPointF{pressLocal}, pressGlobal, Qt::LeftButton, Qt::LeftButton, Qt::NoModifier};
    QApplication::sendEvent(view, &press);
    QMouseEvent move{QEvent::MouseMove, QPointF{moveLocal}, moveGlobal, Qt::NoButton, Qt::LeftButton, Qt::NoModifier};
    QApplication::sendEvent(view, &move);
    QApplication::processEvents();

    const auto magnifier = view->cropMagnifierRectForTesting();
    ASSERT_FALSE(magnifier.isEmpty()) << "拖右边框时没有出现放大镜";
    EXPECT_TRUE(view->rect().contains(magnifier)) << "放大镜画到了控件外";
    EXPECT_FALSE(view->cropMagnifierReadoutRectForTesting().isEmpty()) << "放大镜下沿没有读数条";

    const auto   shot         = view->grab().toImage();
    const QColor frozenSample = QColor(255, 0, 255);
    EXPECT_EQ(shot.pixelColor(magnifier.center()), frozenSample) << "放大镜没有从整屏冻帧取样";

    QMouseEvent release{QEvent::MouseButtonRelease, QPointF{moveLocal}, moveGlobal, Qt::LeftButton, Qt::NoButton, Qt::NoModifier};
    QApplication::sendEvent(view, &release);

    EXPECT_TRUE(view->cropMagnifierRectForTesting().isEmpty()) << "松手后放大镜还留在画面上";
}

/**
 * @brief 钉住放大镜视野档：关掉像素插图时读数条仍在
 * @details 设置里那一档写明「仅隐藏像素放大图，坐标读数保留」，重裁这一处也照同一口径办。
 */
TEST_F(AnnotationWindowFixture, CropResizeMagnifierHonoursTheOffSettingButKeepsReadout)
{
    ASSERT_TRUE(settings().setValue(GyanisScribe::Core::kCaptureMagnifierView, QStringLiteral("off")).has_value());

    auto window                  = makeWindow();
    auto captured                = makeCanvas(QSize(400, 400));
    captured.screenName          = QStringLiteral("FakeScreen-9");
    captured.logicalRegion       = QRect(100, 100, 400, 400);
    captured.frozenLogicalSize   = QSize(800, 600);
    captured.frozenLogicalRegion = QRect(QPoint{0, 0}, QSize(800, 600));
    captured.frozenImage         = QImage{QSize{800, 600}, QImage::Format_ARGB32};
    captured.frozenImage.fill(QColor{0, 255, 0});
    window->startAnnotation(captured, QRect(0, 0, 1920, 1080), QRect(100, 100, 400, 400));
    QApplication::processEvents();
    ASSERT_TRUE(engine().setActiveTool(ToolKind::Select).has_value());

    auto *view = window->findChild<GyanisScribe::Gui::AnnotationView *>();
    ASSERT_NE(view, nullptr);

    const QPointF pressGlobal = view->mapToGlobal(QPoint{399, 150});
    QMouseEvent   press{QEvent::MouseButtonPress, QPointF{399, 150}, pressGlobal, Qt::LeftButton, Qt::LeftButton, Qt::NoModifier};
    QApplication::sendEvent(view, &press);

    EXPECT_TRUE(view->cropMagnifierRectForTesting().isEmpty()) << "视野档是「不显示」时还画出了像素插图";
    EXPECT_FALSE(view->cropMagnifierReadoutRectForTesting().isEmpty()) << "关掉插图把坐标读数也一起收走了";

    QMouseEvent release{QEvent::MouseButtonRelease, QPointF{399, 150}, pressGlobal, Qt::LeftButton, Qt::NoButton, Qt::NoModifier};
    QApplication::sendEvent(view, &release);
}

/**
 * @brief 钉住拖边途中工具栏就跟到新范围旁边，而不是等松手后的重抓回来
 * @details 窗口几何在拖动每一帧都改了，工具栏却只在 recropAnnotation 里重摆，
 *          于是全程停旧位置，松手后才跳过去。这条按「还没发 cropRequested」那一刻核对。
 */
TEST_F(AnnotationWindowFixture, ToolbarFollowsTheResizeBeforeTheRecaptureBegins)
{
    auto window            = makeWindow();
    auto captured          = makeCanvas(QSize(200, 100));
    captured.screenName    = QStringLiteral("FakeScreen-9");
    captured.logicalRegion = QRect(100, 100, 200, 100);
    window->startAnnotation(captured, QRect(0, 0, 1920, 1080), QRect(100, 100, 200, 100));
    QApplication::processEvents();
    ASSERT_TRUE(engine().setActiveTool(ToolKind::Select).has_value());

    auto *view = window->findChild<GyanisScribe::Gui::AnnotationView *>();
    ASSERT_NE(view, nullptr);
    int crops = 0;
    QObject::connect(window.get(), &GyanisScribe::Gui::AnnotationWindow::cropRequested, [&crops](const QString &, const QRect &) { ++crops; });

    const auto before = window->toolbar().geometry();

    const QPointF pressGlobal = view->mapToGlobal(QPoint{100, 99});
    const QPointF moveGlobal  = view->mapToGlobal(QPoint{100, 129});
    QMouseEvent   press{QEvent::MouseButtonPress, QPointF{100, 99}, pressGlobal, Qt::LeftButton, Qt::LeftButton, Qt::NoModifier};
    QApplication::sendEvent(view, &press);
    QMouseEvent move{QEvent::MouseMove, QPointF{100, 129}, moveGlobal, Qt::NoButton, Qt::LeftButton, Qt::NoModifier};
    QApplication::sendEvent(view, &move);
    QApplication::processEvents();

    const auto expected = GyanisScribe::Gui::placeToolbar(QRect(0, 0, 1920, 1080), window->toolbar().sizeHint(), QRect(100, 100, 200, 130));
    ASSERT_TRUE(expected.has_value());
    EXPECT_EQ(window->toolbar().geometry(), expected->frame) << "拖边途中工具栏没有跟到新选区旁边";
    EXPECT_NE(window->toolbar().geometry(), before) << "工具栏停在旧位置：没有实时联动";
    EXPECT_EQ(crops, 0) << "还没松手就发起了重抓";

    QMouseEvent release{QEvent::MouseButtonRelease, QPointF{100, 129}, moveGlobal, Qt::LeftButton, Qt::NoButton, Qt::NoModifier};
    QApplication::sendEvent(view, &release);
}

/**
 * @brief 钉住拖边的高频移动按一拍一帧落地，松手冲刷最后一帧
 * @details 高回报率鼠标一秒能发上千个移动事件；逐个 setGeometry 加重绘会把主线程占满。
 *          这里连发 20 个移动：第一个立即生效，其余只留最后一帧，下一拍落到终点，松手交出的也是终点。
 */
TEST_F(AnnotationWindowFixture, CropResizeCoalescesBurstMovesAndFlushesOnRelease)
{
    auto window            = makeWindow();
    auto captured          = makeCanvas(QSize(200, 100));
    captured.screenName    = QStringLiteral("FakeScreen-9");
    captured.logicalRegion = QRect(100, 100, 200, 100);
    window->startAnnotation(captured, QRect(0, 0, 1920, 1080), QRect(100, 100, 200, 100));
    QApplication::processEvents();
    ASSERT_TRUE(engine().setActiveTool(ToolKind::Select).has_value());

    auto *view = window->findChild<GyanisScribe::Gui::AnnotationView *>();
    ASSERT_NE(view, nullptr);

    QSignalSpy moved{view, &GyanisScribe::Gui::AnnotationView::captureResizeMoved};
    QSignalSpy cropped{window.get(), &GyanisScribe::Gui::AnnotationWindow::cropRequested};

    const QPointF pressGlobal = view->mapToGlobal(QPoint{100, 99});
    QMouseEvent   press{QEvent::MouseButtonPress, QPointF{100, 99}, pressGlobal, Qt::LeftButton, Qt::LeftButton, Qt::NoModifier};
    QApplication::sendEvent(view, &press);

    // 20 个移动事件一口气投完：全局点按初始窗口位置算，位移逐格加到 20 像素
    for (int step = 1; step <= 20; ++step)
    {
        const QPointF global = view->mapToGlobal(QPoint{100, 99}) + QPointF{0.0, static_cast<qreal>(step)};
        QMouseEvent   move{QEvent::MouseMove, QPointF{100.0, static_cast<qreal>(99 + step)}, global, Qt::NoButton, Qt::LeftButton, Qt::NoModifier};
        QApplication::sendEvent(view, &move);
    }

    EXPECT_EQ(moved.count(), 1) << "第一个移动之后没有再发生同步落地才叫节流";
    EXPECT_EQ(window->geometry(), QRect(100, 100, 200, 101)) << "第一个移动没有立即生效，窗口要等到下一拍才动";

    ASSERT_TRUE(moved.wait(100)) << "节拍没有把最后一帧落下来";
    EXPECT_EQ(window->geometry(), QRect(100, 100, 200, 120)) << "下一拍没有落到最后一帧的目标几何";
    ASSERT_GE(moved.count(), 2);
    EXPECT_EQ(moved.last().at(0).toRect(), QRect(100, 100, 200, 120)) << "节拍落下的不是最后那一帧";

    QMouseEvent release{QEvent::MouseButtonRelease, QPointF{100, 119}, pressGlobal + QPointF{0.0, 20.0}, Qt::LeftButton, Qt::NoButton, Qt::NoModifier};
    QApplication::sendEvent(view, &release);

    ASSERT_EQ(cropped.count(), 1);
    EXPECT_EQ(cropped.last().at(1).toRect(), QRect(100, 100, 200, 120)) << "松手交出的不是光标停下的那一块";
}

/**
 * @brief 画过标注之后不再允许重裁：重抓会换掉整张画布，标注会一起没
 */
TEST_F(AnnotationWindowFixture, SelectToolKeepsAnnotationsWhenResizing)
{
    auto window            = makeWindow();
    auto captured          = makeCanvas(QSize(200, 100));
    captured.screenName    = QStringLiteral("FakeScreen-9");
    captured.logicalRegion = QRect(100, 100, 200, 100);
    window->startAnnotation(captured, QRect(0, 0, 1920, 1080), QRect(100, 100, 200, 100));
    QApplication::processEvents();

    ASSERT_TRUE(engine().setActiveTool(ToolKind::Rectangle).has_value());
    engine().beginStroke(QPoint{20, 20});
    engine().moveStroke(QPoint{80, 60});
    ASSERT_TRUE(engine().endStroke().has_value());
    ASSERT_TRUE(engine().setActiveTool(ToolKind::Select).has_value());

    auto *view = window->findChild<GyanisScribe::Gui::AnnotationView *>();
    ASSERT_NE(view, nullptr);
    int   crops = 0;
    QRect cropRegion;
    QObject::connect(window.get(), &GyanisScribe::Gui::AnnotationWindow::cropRequested,
                     [&crops, &cropRegion](const QString &, const QRect &selection)
                     {
                         ++crops;
                         cropRegion = selection;
                     });

    QMouseEvent press{QEvent::MouseButtonPress, QPointF{0, 50}, QPointF{view->mapToGlobal(QPoint{0, 50})}, Qt::LeftButton, Qt::LeftButton, Qt::NoModifier};
    QApplication::sendEvent(view, &press);
    QMouseEvent move{QEvent::MouseMove, QPointF{-10, 50}, QPointF{view->mapToGlobal(QPoint{-10, 50})}, Qt::NoButton, Qt::LeftButton, Qt::NoModifier};
    QApplication::sendEvent(view, &move);
    QMouseEvent release{QEvent::MouseButtonRelease, QPointF{-10, 50}, QPointF{view->mapToGlobal(QPoint{-10, 50})}, Qt::LeftButton, Qt::NoButton, Qt::NoModifier};
    QApplication::sendEvent(view, &release);
    ASSERT_EQ(crops, 1);
    EXPECT_EQ(cropRegion, QRect(90, 100, 210, 100));

    auto fresh          = makeCanvas(QSize(210, 100));
    fresh.screenName    = captured.screenName;
    fresh.logicalRegion = QRect(90, 100, 210, 100);
    window->recropAnnotation(fresh, QRect(0, 0, 1920, 1080), QRect(90, 100, 210, 100));

    // 矩形原在逻辑 (20,20)-(80,60)；新画布原点左移 10，图形跟到 (30,20)-(90,60)
    const auto baked = engine().exportImage();
    EXPECT_EQ(baked.pixelColor(120, 40), GyanisScribe::Core::kAnnotationPaletteColors[0]) << "重裁之后标注没有跟着平移";
    EXPECT_EQ(baked.pixelColor(40, 40), QColor(180, 180, 180)) << "旧位置还留着一条，图形被复制了一份";
    EXPECT_EQ(window->geometry(), QRect(90, 100, 210, 100)) << "窗口没有按新范围摆好";
}

/**
 * @brief 钉住重裁途中新露出的区域来自整屏冻帧，不是拉伸的旧画布、也不留白边
 * @details 视图在拖边框时用冻帧裁出的预览填满整窗；没收到的冻帧才退回旧画布。
 */
TEST_F(AnnotationWindowFixture, CropPreviewComesFromTheFrozenFrame)
{
    auto *screen = QGuiApplication::primaryScreen();
    ASSERT_NE(screen, nullptr);

    auto full = makeCanvas(QSize(400, 300));
    full.image.fill(QColor(0, 255, 0));
    full.frozenImage         = full.image;
    full.frozenLogicalSize   = full.logicalSize;
    full.frozenLogicalRegion = QRect(0, 0, 400, 300);
    full.screenName          = screen->name();

    auto initial = makeCanvas(QSize(200, 100));
    initial.image.fill(QColor(0, 0, 255));
    initial.logicalRegion       = QRect(100, 100, 200, 100);
    initial.screenName          = screen->name();
    initial.frozenImage         = full.frozenImage;
    initial.frozenLogicalSize   = full.frozenLogicalSize;
    initial.frozenLogicalRegion = full.frozenLogicalRegion;

    auto window = makeWindow();
    window->startAnnotation(initial, screen->availableGeometry(), QRect(100, 100, 200, 100));
    QApplication::processEvents();
    ASSERT_TRUE(engine().setActiveTool(ToolKind::Select).has_value());

    auto *view = window->findChild<GyanisScribe::Gui::AnnotationView *>();
    ASSERT_NE(view, nullptr);

    QMouseEvent press{QEvent::MouseButtonPress, QPointF{0, 50}, QPointF{view->mapToGlobal(QPoint{0, 50})}, Qt::LeftButton, Qt::LeftButton, Qt::NoModifier};
    QApplication::sendEvent(view, &press);
    QMouseEvent move{QEvent::MouseMove, QPointF{-10, 50}, QPointF{view->mapToGlobal(QPoint{-10, 50})}, Qt::NoButton, Qt::LeftButton, Qt::NoModifier};
    QApplication::sendEvent(view, &move);
    QApplication::processEvents();

    const auto during = view->grab().toImage();
    EXPECT_EQ(during.pixelColor(5, 50), QColor(0, 255, 0)) << "新露出的左边不是冻帧内容";
    EXPECT_NE(during.pixelColor(5, 50), QColor(255, 255, 255)) << "重裁途中出现白边";

    QMouseEvent release{QEvent::MouseButtonRelease, QPointF{-10, 50}, QPointF{view->mapToGlobal(QPoint{-10, 50})}, Qt::LeftButton, Qt::NoButton, Qt::NoModifier};
    QApplication::sendEvent(view, &release);

    auto fresh = makeCanvas(QSize(210, 100));
    fresh.image.fill(QColor(0, 255, 0));
    fresh.logicalRegion       = QRect(90, 100, 210, 100);
    fresh.screenName          = screen->name();
    fresh.frozenImage         = full.frozenImage;
    fresh.frozenLogicalSize   = full.frozenLogicalSize;
    fresh.frozenLogicalRegion = full.frozenLogicalRegion;
    window->recropAnnotation(fresh, screen->availableGeometry(), QRect(90, 100, 210, 100));
    QApplication::processEvents();

    const auto after = view->grab().toImage();
    EXPECT_EQ(after.pixelColor(5, 50), QColor(0, 255, 0)) << "重裁之后左边留了一条不是截图的底色";
}

/**
 * @brief 钉住标注窗四周有一圈只上屏的主色提示线
 * @details 标注窗与桌面内容逐像素相同，没有这圈线时用户分不清哪一块是截图。
 *          判据同时反向确认它没有进成品：exportImage 的边缘仍是底图色。
 */
TEST_F(AnnotationWindowFixture, TheAnnotationWindowShowsAScreenOnlyFrame)
{
    auto window = makeWindow();
    window->startAnnotation(makeCanvas(QSize(200, 100)), QRect(0, 0, 1920, 1080), QRect(100, 100, 200, 100));
    QApplication::processEvents();

    const auto frame = window->grab().toImage();
    ASSERT_FALSE(frame.isNull());
    const auto accent = GyanisScribe::Gui::kAccentColor;
    const auto edgeY  = frame.height() / 2;
    EXPECT_EQ(frame.pixelColor(0, edgeY), accent) << "左边没有提示线";
    EXPECT_EQ(frame.pixelColor(frame.width() - 1, edgeY), accent) << "右边没有提示线";
    EXPECT_EQ(frame.pixelColor(frame.width() / 2, 0), accent) << "上边没有提示线";
    EXPECT_EQ(frame.pixelColor(frame.width() / 2, frame.height() - 1), accent) << "下边没有提示线";

    EXPECT_EQ(engine().exportImage().pixelColor(0, 50), QColor(180, 180, 180)) << "提示线被烘进成品了";
}

/**
 * @brief 钉住行内编辑器去边框后高度仍按内容行数算，文本不被裁
 * @details 浅色与深色档给 editor 留的是 1px 透明边框占位：可见描边没了，边框仍算在盒模型里。
 *          resizeTextEditorToContent 的高度按块数乘行距，再加文档边距与 frameWidth 现算；
 *          两行与四行各核一次，期望值用编辑器自己的度量重算，不靠界面截图目测。
 *          文本没被裁的判据是视口高度装得下整份文档。
 */
TEST_F(AnnotationWindowFixture, BorderlessInlineEditorResizesToItsContent)
{
    struct StyleGuard final
    {
        QString sheet = qApp->styleSheet();
        ~StyleGuard()
        {
            qApp->setStyleSheet(sheet);
        }
    } guard;
    qApp->setStyleSheet(GyanisScribe::Gui::ThemeManager::styleSheetFor(GyanisScribe::Gui::ThemeKind::Light));

    GyanisScribe::Gui::AnnotationView view{engine()};
    view.resize(400, 200);
    ASSERT_TRUE(engine().setActiveTool(ToolKind::Text).has_value());
    engine().beginStroke(QPoint(20, 20));
    ASSERT_TRUE(engine().setPendingText(QStringLiteral("AB")).has_value());
    const auto placed = engine().endStroke();
    ASSERT_TRUE(placed.has_value()) << placed.error().message;
    ASSERT_TRUE(engine().select(*placed).has_value());

    const auto bounds = engine().selectionBounds();
    ASSERT_TRUE(bounds.has_value());
    const QPoint widgetCenter{(bounds->center().x() + 1) * 2, (bounds->center().y() + 1) * 2};
    QMouseEvent doubleClick{QEvent::MouseButtonDblClick, QPointF{widgetCenter}, QPointF{widgetCenter}, Qt::LeftButton, Qt::NoButton, Qt::NoModifier};
    QApplication::sendEvent(&view, &doubleClick);

    auto *editor = view.findChild<QTextEdit *>();
    ASSERT_NE(editor, nullptr) << "双击一条文字没有打开输入框";
    editor->ensurePolished();
    // 去边框之后靠这条 1px 透明占位保住盒模型：可见边缘没有，但边框仍占一格
    const auto sheet = GyanisScribe::Gui::ThemeManager::styleSheetFor(GyanisScribe::Gui::ThemeKind::Light);
    const QRegularExpression editorRule{QStringLiteral(R"(QTextEdit#annotationTextEditor\s*\{([^}]*)\})")};
    const auto               ruleMatch = editorRule.match(sheet);
    ASSERT_TRUE(ruleMatch.hasMatch()) << "浅色样式表里没有行内编辑器的规则";
    EXPECT_NE(ruleMatch.captured(1).indexOf(QStringLiteral("border: 1px solid transparent")), -1) << "1px 透明边框占位被删掉了";
#if QT_VERSION >= QT_VERSION_CHECK(6, 7, 0)
    EXPECT_EQ(editor->font().featureValue(QFont::Tag("tnum")), 0u) << "标注正文被误加了等宽数字";
#endif

    for (const auto lineCount: {2, 4})
    {
        QString text;
        for (int line = 0; line < lineCount; ++line)
        {
            if (line > 0)
            {
                text += QLatin1Char('\n');
            }
            text += QStringLiteral("第 %1 行").arg(line + 1);
        }
        editor->setPlainText(text);
        QCoreApplication::processEvents();
        ASSERT_EQ(editor->document()->blockCount(), lineCount) << "用例造的行数没有落进文档";

        const QFontMetricsF metrics{editor->font()};
        const auto          contentHeight = static_cast<int>(std::ceil(metrics.lineSpacing() * static_cast<qreal>(lineCount)));
        const auto          expected      = std::clamp(contentHeight + static_cast<int>(2 * editor->document()->documentMargin()) + 2 * editor->frameWidth() + 6, 24, 200);
        EXPECT_EQ(editor->height(), expected) << lineCount << " 行时输入框高度与内容行数对不上";
        // 文本没被裁：视口装得下整份文档（含上下文档边距），不靠截图目测
        EXPECT_GE(editor->viewport()->height(), editor->document()->size().toSize().height()) << lineCount << " 行时文本被输入框裁掉了一截";
    }
}

/**
 * @brief 钉住贴着窗口边缘的图形手柄优先于窗口抓点
 * @details 上边中点手柄落在窗口上边八格抓点带里：按下这一点时用户要拉的是图形。
 *          先问窗口抓点会把这一趟变成重裁截图范围，图形一动不动而窗口整体挪走。
 *          按下点取两者命中区的交集（手柄圆心在 y=10，窗口上边带在 y<=8），
 *          拖动方向朝窗口左上，两条路的产物一眼分得开。
 */
TEST_F(AnnotationWindowFixture, EdgeHandleBeatsWindowGrip)
{
    auto window         = makeWindow();
    auto captured       = makeCanvas(QSize(200, 100));
    captured.screenName = QStringLiteral("FakeScreen-9");
    window->startAnnotation(captured, QRect(0, 0, 1920, 1080), QRect(100, 100, 200, 100));
    QApplication::processEvents();

    ASSERT_TRUE(drawAndSelect({10, 10}, {190, 90}).has_value()) << "选中的矩形没画出来，后面的手柄判定没有对象";
    ASSERT_TRUE(engine().setActiveTool(ToolKind::Select).has_value());

    auto *view = window->findChild<GyanisScribe::Gui::AnnotationView *>();
    ASSERT_NE(view, nullptr);
    ASSERT_EQ(view->size(), QSize(200, 100)) << "视图没铺满窗口，控件坐标与画布坐标对不上";

    const auto frameBefore  = engine().selectionFrame();
    const auto windowBefore = window->geometry();
    ASSERT_TRUE(frameBefore.has_value()) << "选中一条图形之后没有手柄框";
    ASSERT_EQ(windowBefore.size(), QSize(200, 100)) << "窗口没按开窗时的画布尺寸摆好";

    const QPoint  pressLocal{100, 8};
    const QPoint  moveLocal{80, -12};
    const QPointF pressGlobal{view->mapToGlobal(pressLocal)};
    const QPointF moveGlobal{view->mapToGlobal(moveLocal)};
    for (const auto &[type, local, global]: {std::tuple{QEvent::MouseButtonPress, pressLocal, pressGlobal}, std::tuple{QEvent::MouseMove, moveLocal, moveGlobal},
                                             std::tuple{QEvent::MouseButtonRelease, moveLocal, moveGlobal}})
    {
        QMouseEvent event{type, QPointF{local}, global, Qt::LeftButton, Qt::LeftButton, Qt::NoModifier};
        QApplication::sendEvent(view, &event);
    }

    const auto frameAfter = engine().selectionFrame();
    ASSERT_TRUE(frameAfter.has_value());
    // 上边手柄只动上沿：上沿往上走，下沿与左右两边留在原处
    EXPECT_LT((*frameAfter)[0].y(), (*frameBefore)[0].y() - 5.0) << "拖上边中点手柄没把上沿拉上去：这一按被当成了重裁窗口";
    EXPECT_DOUBLE_EQ((*frameAfter)[4].y(), (*frameBefore)[4].y()) << "拉上沿把下沿也带走了：拖的不是上边手柄";
    EXPECT_DOUBLE_EQ((*frameAfter)[0].x(), (*frameBefore)[0].x()) << "拉上沿改了左边：拖的不是上边手柄";
    EXPECT_EQ(window->geometry(), windowBefore) << "按下图形手柄却挪走了窗口";
}

/**
 * @brief 钉住重裁预览按裁切片的原点与逻辑尺寸映射图形
 * @details 冻帧只盖住新窗口的一部分：预览图被裁掉右侧一截后拉伸到整个窗口宽度，
 *          图形必须按同一份「被裁原点 + 预览逻辑尺寸」缩放，而不是按老坐标平移。
 *          冻帧 400×300 物理像素、缩放因子 2，即 200×150 逻辑像素；
 *          初始画布 200×100，向左扩 10 后窗口 210×100，预览取到的是 200×100 的逻辑片。
 *          矩形右边逻辑 80，映射后应落在 (80 + 10) × 210 / 200 一带；老写法停在 90。
 */
TEST_F(AnnotationWindowFixture, CropPreviewMapsShapesByClippedOriginAndScale)
{
    // 这条只测预览映射：放大镜像素插图会盖住要扫的那一行，按「不显示」档收掉它（读数条仍在，但不压 y=40）
    ASSERT_TRUE(settings().setValue(GyanisScribe::Core::kCaptureMagnifierView, QStringLiteral("off")).has_value());

    auto *screen = QGuiApplication::primaryScreen();
    ASSERT_NE(screen, nullptr);

    QImage frozen{QSize{400, 300}, QImage::Format_ARGB32};
    frozen.fill(QColor(0, 255, 0));

    auto initial = makeCanvas(QSize(200, 100));
    initial.image.fill(QColor(0, 0, 255));
    initial.logicalRegion       = QRect(100, 100, 200, 100);
    initial.screenName          = screen->name();
    initial.frozenImage         = frozen;
    initial.frozenLogicalSize   = QSize(200, 150);
    initial.frozenLogicalRegion = QRect(90, 100, 200, 150);

    auto window = makeWindow();
    window->startAnnotation(initial, screen->availableGeometry(), QRect{screen->geometry().topLeft() + QPoint(100, 100), QSize{200, 100}});
    QApplication::processEvents();

    ASSERT_TRUE(engine().setActiveTool(ToolKind::Rectangle).has_value());
    ASSERT_TRUE(engine().setPenParameters(PenParameters{.color = QColor(255, 0, 0), .lineWidth = 2, .fontSize = 16, .fillStyle = 0}).has_value());
    engine().beginStroke(QPoint(20, 20));
    engine().moveStroke(QPoint(80, 60));
    ASSERT_TRUE(engine().endStroke().has_value()) << "参照矩形没画上";
    ASSERT_TRUE(engine().setActiveTool(ToolKind::Select).has_value());

    auto *view = window->findChild<GyanisScribe::Gui::AnnotationView *>();
    ASSERT_NE(view, nullptr);
    ASSERT_EQ(view->size(), QSize(200, 100)) << "开窗时视图不是 200×100，拖动前的坐标对不上";

    const QPoint  pressLocal{0, 50};
    const QPoint  moveLocal{-10, 50};
    const QPointF pressGlobal{view->mapToGlobal(pressLocal)};
    const QPointF moveGlobal{view->mapToGlobal(moveLocal)};
    for (const auto &[type, local, global]: {std::tuple{QEvent::MouseButtonPress, pressLocal, pressGlobal}, std::tuple{QEvent::MouseMove, moveLocal, moveGlobal}})
    {
        QMouseEvent event{type, QPointF{local}, global, Qt::LeftButton, Qt::LeftButton, Qt::NoModifier};
        QApplication::sendEvent(view, &event);
    }
    QApplication::processEvents();
    ASSERT_EQ(view->size(), QSize(210, 100)) << "拖左边框没有把视图扩到 210 宽，后面的映射判据不成立";

    const QImage shot = view->grab().toImage();
    // grab 出的是物理像素：探点都按抓屏倍比折回控件逻辑坐标，100% 与 150% 缩放共用同一套判据
    const auto primaryColor = [](const QColor &pixel) { return pixel.red() > 180 && pixel.green() < 100 && pixel.blue() < 100; };
    const auto shotScaleOf  = [](const QImage &frame) { return frame.devicePixelRatio() > 0.0 ? frame.devicePixelRatio() : 1.0; };
    const auto rightMostInkOn = [&primaryColor, &shotScaleOf](const QImage &frame, const qreal logicalY)
    {
        const qreal scale     = shotScaleOf(frame);
        const int   row       = static_cast<int>(std::lround(logicalY * scale));
        int         rightMost = -1;
        for (int x = 0; x < frame.width(); ++x)
        {
            if (primaryColor(frame.pixelColor(x, row)))
            {
                rightMost = x;
            }
        }
        return rightMost < 0 ? -1.0 : static_cast<qreal>(rightMost) / scale;
    };
    const auto inkAt = [&primaryColor, &shotScaleOf](const QImage &frame, const qreal logicalX, const qreal logicalY)
    {
        const qreal scale = shotScaleOf(frame);
        return primaryColor(frame.pixelColor(static_cast<int>(std::lround(logicalX * scale)), static_cast<int>(std::lround(logicalY * scale))));
    };

    const qreal rightMostInk = rightMostInkOn(shot, 40.0);
    ASSERT_GE(rightMostInk, 0.0) << "预览里找不到矩形的右边，后面两处判据都没有基准";
    EXPECT_NEAR(rightMostInk, (80.0 + 10.0) * 210.0 / 200.0, 2.0) << "矩形右边没按预览的裁切原点与缩放映射";
    EXPECT_FALSE(inkAt(shot, 90.0, 40.0)) << "图形的旧位置还留着主色：这一趟只做了平移，没有按预览比例缩放";

    // 预览逻辑尺寸为空时只平移不缩放：跳过 scale 分支，不会拿空宽去做除法
    view->setCropPreview(frozen, frozen.rect(), QPointF{5.0, 5.0}, QSizeF{});
    QApplication::processEvents();
    const QImage fallback              = view->grab().toImage();
    const qreal  rightMostWithoutScale = rightMostInkOn(fallback, 40.0);
    ASSERT_GE(rightMostWithoutScale, 0.0) << "空预览尺寸那一档找不到矩形的右边";
    EXPECT_NEAR(rightMostWithoutScale, 80.0 + 5.0, 2.0) << "空预览尺寸时图形没有按原点平移（或按别的比例缩放了）";
    EXPECT_FALSE(inkAt(fallback, 20.0, 40.0)) << "空预览尺寸时矩形还停在原位：平移没生效";
}

/**
 * @brief 钉住重裁预览里的图形抗锯齿、选框与抓点边缘保持纯色
 * @details 预览分支与普通分支各设一次渲染提示：斜放的矩形边要在预览里落出中间色，
 *          否则预览与成品的边缘不是同一档；选框与抓点是界面辅助线，边缘必须仍是纯色。
 *          用 render 按 1:1 出图，抓屏会按设备倍比放大，1px 辅助线的纯色判据会被缩放糊掉。
 */
TEST_F(AnnotationWindowFixture, CropPreviewAntialiasesShapeEdges)
{
    // 矩形转 45°，四条边都成为斜线：抗锯齿与否在像素上分得开
    ASSERT_TRUE(engine().setActiveTool(ToolKind::Rectangle).has_value());
    ASSERT_TRUE(engine().setPenParameters(PenParameters{.color = QColor(255, 0, 0), .lineWidth = 3, .fontSize = 16, .fillStyle = 0}).has_value());
    engine().beginStroke(QPoint(60, 35));
    engine().moveStroke(QPoint(140, 65));
    const auto settled = engine().endStroke();
    ASSERT_TRUE(settled.has_value()) << settled.error().message;
    ASSERT_TRUE(engine().select(*settled).has_value());
    ASSERT_TRUE(engine().beginSelectionDrag(GyanisScribe::Core::SelectionHandle::Rotate, QPoint(100, 0)).has_value()) << "没抓住旋转手柄，矩形转不到斜放";
    ASSERT_TRUE(engine().updateSelectionDrag(QPoint(150, 0), GyanisScribe::Core::SelectionDragModifiers{}).has_value()) << "旋转拖不动，矩形仍是横平竖直";
    engine().endSelectionDrag();
    ASSERT_TRUE(engine().setActiveTool(ToolKind::Select).has_value());

    GyanisScribe::Gui::AnnotationView view{engine()};
    view.resize(200, 100);
    QImage preview{200, 100, QImage::Format_ARGB32};
    preview.fill(QColor(0, 180, 0));
    view.setCropPreview(preview, preview.rect(), QPointF{0.0, 0.0}, QSizeF{200.0, 100.0});
    QApplication::processEvents();

    QImage shot{200, 100, QImage::Format_ARGB32};
    shot.fill(Qt::transparent);
    {
        QPainter painter{&shot};
        view.render(&painter);
    }

    const QColor background{0, 180, 0};
    const QColor ink{255, 0, 0};
    int          intermediatePixels = 0;
    for (int y = 8; y < 92; ++y)
    {
        for (int x = 8; x < 192; ++x)
        {
            const QColor pixel = shot.pixelColor(x, y);
            // 红绿相混的像素既不是底色也不是笔色：只有开了抗锯齿的斜边会落在这一档
            if (pixel != background && pixel != ink && pixel.red() > 0 && pixel.green() > 0)
            {
                ++intermediatePixels;
            }
        }
    }
    EXPECT_GT(intermediatePixels, 0) << "预览里的斜边没有中间色：图形那一层没开抗锯齿";

    // 反向对照：顶边选框线与右侧中点抓点的左边缘都不该被抗锯齿糊开
    for (const int x: {20, 40, 70, 120, 150, 180})
    {
        EXPECT_EQ(shot.pixelColor(x, 1), GyanisScribe::Gui::kAccentColor) << "预览顶边的选框线在 x=" << x << " 不是纯色";
    }
    for (const int y: {47, 49, 51})
    {
        EXPECT_EQ(shot.pixelColor(195, y), GyanisScribe::Gui::kAccentColor) << "右侧中点抓点的左边缘在 y=" << y << " 不是纯色";
    }
}

/**
 * @brief 钉住文字输入框跟着成品字体与基线摆
 * @details 成品把 position 当首行基线画字，输入框若把基线点当左上角，
 *          提交瞬间文字会向上跳一个 ascent；输入框还必须是画笔参数拼出的那个字体。
 */
TEST_F(AnnotationWindowFixture, TextEditorUsesTheProductFontAndBaseline)
{
    GyanisScribe::Gui::AnnotationView view{engine()};
    view.resize(200, 100);
    ASSERT_EQ(view.size(), QSize(200, 100)) << "视图没按画布逻辑尺寸摆，按下点与基线对不上";

    ASSERT_TRUE(engine().setActiveTool(ToolKind::Text).has_value());
    ASSERT_TRUE(engine().setPenParameters(PenParameters{.color = QColor(255, 0, 0), .lineWidth = 2, .fontSize = 20, .fillStyle = 0}).has_value());

    const QPoint pressPoint{50, 50};
    QMouseEvent  press{QEvent::MouseButtonPress, QPointF{pressPoint}, QPointF{pressPoint}, Qt::LeftButton, Qt::LeftButton, Qt::NoModifier};
    QApplication::sendEvent(&view, &press);

    auto *editor = view.findChild<QTextEdit *>(QStringLiteral("annotationTextEditor"));
    ASSERT_NE(editor, nullptr) << "文字工具下按下画布没有打开输入框";
    EXPECT_EQ(editor->font().pointSize(), 20) << "输入框用的不是画笔参数里的字号";

    const QFontMetricsF metrics{editor->font()};
    const auto expectedTop = pressPoint.y() - static_cast<int>(std::ceil(metrics.ascent() + editor->document()->documentMargin() + static_cast<qreal>(editor->frameWidth())));
    EXPECT_EQ(editor->geometry().top(), expectedTop) << "输入框上沿还停在基线上，没有退一个 ascent 与文档边距";
    EXPECT_EQ(editor->geometry().left(), pressPoint.x()) << "输入框左沿不该跟着基线调整一起挪";
}

/**
 * @brief 钉住提交前后首行字形顶不跳
 * @details 输入框与成品走两条渲染路：判据取两边各自首行第一个墨点的行号。
 *          输入框把基线当左上角时，提交后成品字会整体上移一个 ascent，两边的墨点顶差出一行多。
 *          输入框与画布同为 200×100，控件坐标与画布逻辑坐标一一对应，导出图按倍比 2 折算物理行号。
 */
TEST_F(AnnotationWindowFixture, TextEditorCommitKeepsItsTopEdge)
{
    GyanisScribe::Gui::AnnotationView view{engine()};
    view.resize(200, 100);
    ASSERT_EQ(view.size(), QSize(200, 100)) << "视图没按画布逻辑尺寸摆，两边的行号不能直接比";

    ASSERT_TRUE(engine().setActiveTool(ToolKind::Text).has_value());
    ASSERT_TRUE(engine().setPenParameters(PenParameters{.color = QColor(255, 0, 0), .lineWidth = 2, .fontSize = 20, .fillStyle = 0}).has_value());

    const QPoint pressPoint{50, 50};
    QMouseEvent  press{QEvent::MouseButtonPress, QPointF{pressPoint}, QPointF{pressPoint}, Qt::LeftButton, Qt::LeftButton, Qt::NoModifier};
    QApplication::sendEvent(&view, &press);

    auto *editor = view.findChild<QTextEdit *>(QStringLiteral("annotationTextEditor"));
    ASSERT_NE(editor, nullptr) << "文字工具下按下画布没有打开输入框";
    editor->setPlainText(QStringLiteral("H"));
    editor->selectAll();
    editor->setTextColor(QColor(255, 0, 0));
    editor->moveCursor(QTextCursor::End);

    const auto firstInkRow = [](const QImage &image)
    {
        for (int y = 0; y < image.height(); ++y)
        {
            for (int x = 0; x < image.width(); ++x)
            {
                const QColor pixel = image.pixelColor(x, y);
                if (pixel.red() > 180 && pixel.green() < 100 && pixel.blue() < 100)
                {
                    return y;
                }
            }
        }
        return -1;
    };

    QImage editorShot{editor->size(), QImage::Format_ARGB32};
    editorShot.fill(Qt::white);
    {
        QPainter painter{&editorShot};
        editor->render(&painter);
    }
    const int editorInkTop = firstInkRow(editorShot);
    ASSERT_GE(editorInkTop, 0) << "输入框里找不到红字，后面的对照没有基准";
    const int editorInkTopInView = editor->geometry().top() + editorInkTop;

    pressKeyOn(*editor, Qt::Key_Return, Qt::ControlModifier);
    const int exportedInkTop = firstInkRow(engine().exportImage());
    ASSERT_GE(exportedInkTop, 0) << "导出图里找不到红字，提交没落到画布上";
    EXPECT_LE(std::abs(exportedInkTop - editorInkTopInView * 2), 1)
            << "提交后首行字形顶跳了：输入框在视图第 " << editorInkTopInView << " 行，成品在第 " << exportedInkTop << " 行（物理）";
}

/**
 * @brief 钉住信息面板：开关开着且来源齐备时，四行文本逐项写明应用、窗口、屏幕与时刻
 * @details 面板是独立顶层窗，用例经只读接缝取它；应用名要去掉 .exe，时间按同一份格式核对。
 *          顺带钉住两件事：面板不跑出本轮屏幕可用区，且「未知」那一档不冒充任何字段。
 */
TEST_F(AnnotationWindowFixture, InfoPanelShowsTheSourceFieldsWhenEnabled)
{
    ASSERT_TRUE(settings().setValue(GyanisScribe::Core::kCaptureShowInfoPanel, true).has_value()) << "写入信息面板开关失败";
    auto window = makeWindow();

    CaptureSourceInfo source;
    source.processName = QStringLiteral("notepad.exe");
    source.windowTitle = QStringLiteral("无标题 - 记事本");
    source.screenName  = QStringLiteral("TestScreen");
    source.capturedAt  = QDateTime{QDate{2026, 11, 9}, QTime{13, 14, 15}};
    const QRect available{0, 0, 1920, 1080};

    window->startAnnotation(makeCanvas({200, 100}), available, QRect{800, 400, 200, 100}, source);

    ASSERT_TRUE(window->isInfoPanelVisible()) << "开关开着、来源齐备，信息面板却没露出来";
    const QWidget *panel = window->infoPanelForTesting();
    ASSERT_NE(panel, nullptr) << "面板可见却取不到它";
    EXPECT_EQ(panel->objectName(), QStringLiteral("captureInfoPanel"));
    EXPECT_TRUE(panel->isWindow()) << "面板得是顶层窗：当子控件会被窗口裁切，也说不清有没有被导出";
    EXPECT_TRUE(panel->windowFlags().testFlag(Qt::Tool)) << "面板该是工具窗：不进任务栏、不抢焦点";

    EXPECT_EQ(panelLabelText(*panel, "captureInfoApp"), QStringLiteral("notepad")) << "应用名要去掉 .exe";
    EXPECT_EQ(panelLabelText(*panel, "captureInfoTitle"), source.windowTitle);
    EXPECT_EQ(panelLabelText(*panel, "captureInfoMonitor"), source.screenName);
    EXPECT_EQ(panelLabelText(*panel, "captureInfoTime"), QStringLiteral("2026-11-09 13:14:15"));

    const auto summary = window->infoPanelText();
    EXPECT_TRUE(summary.contains(QStringLiteral("notepad")));
    EXPECT_TRUE(summary.contains(source.windowTitle));
    EXPECT_TRUE(summary.contains(source.screenName));
    EXPECT_TRUE(summary.contains(QStringLiteral("2026-11-09 13:14:15")));

    EXPECT_TRUE(available.contains(panel->geometry())) << "面板摆到了屏幕可用区外面：" << panel->geometry().x() << "," << panel->geometry().y();
    EXPECT_FALSE(panel->geometry().intersects(QRect{800, 400, 200, 100})) << "有地方摆时面板不该压住截下来的选区：" << panel->geometry().x() << "," << panel->geometry().y();
}

/**
 * @brief 钉住面板的三个不露面条件：开关默认关、开关关、来源全空
 * @details 空白画布与回溯换帧的轮次传的是空来源，拿未知四项凑一块面板只是噪声；
 *          开关关掉之后再开一轮，上一轮的面板也不能留在桌面上。
 */
TEST_F(AnnotationWindowFixture, InfoPanelStaysHiddenWhenDisabledOrSourceEmpty)
{
    auto              window = makeWindow();
    const QRect       available{0, 0, 1920, 1080};
    const QRect       selection{800, 400, 200, 100};
    CaptureSourceInfo source;
    source.processName = QStringLiteral("notepad.exe");
    source.windowTitle = QStringLiteral("无标题 - 记事本");
    source.screenName  = QStringLiteral("TestScreen");
    source.capturedAt  = QDateTime{QDate{2026, 11, 9}, QTime{13, 14, 15}};

    window->startAnnotation(makeCanvas({200, 100}), available, selection, source);
    EXPECT_FALSE(window->isInfoPanelVisible()) << "开关默认关着，面板不该自己跑出来";

    ASSERT_TRUE(settings().setValue(GyanisScribe::Core::kCaptureShowInfoPanel, true).has_value()) << "写入信息面板开关失败";
    window->startAnnotation(makeCanvas({200, 100}), available, selection, {});
    EXPECT_FALSE(window->isInfoPanelVisible()) << "来源全空的轮次不该显示信息面板";

    window->startAnnotation(makeCanvas({200, 100}), available, selection, source);
    ASSERT_TRUE(window->isInfoPanelVisible()) << "开关开着、来源齐备时面板没出来，后面的收起判据没有基准";

    ASSERT_TRUE(settings().setValue(GyanisScribe::Core::kCaptureShowInfoPanel, false).has_value()) << "写入信息面板开关失败";
    window->startAnnotation(makeCanvas({200, 100}), available, selection, source);
    EXPECT_FALSE(window->isInfoPanelVisible()) << "开关关掉后新一轮还留着面板";
}

/**
 * @brief 钉住面板右上角那枚关闭：只收这一轮的面板，标注窗不动；下一轮开窗再摆出来
 * @details 面板是可选的信息层，关掉它不该等于结束标注；开关还开着时，下一轮必须重新显示，
 *          否则用户会以为这一格开关只管第一次。
 */
TEST_F(AnnotationWindowFixture, InfoPanelCloseButtonHidesOnlyThisRound)
{
    ASSERT_TRUE(settings().setValue(GyanisScribe::Core::kCaptureShowInfoPanel, true).has_value()) << "写入信息面板开关失败";
    auto window = makeWindow();

    CaptureSourceInfo source;
    source.processName = QStringLiteral("notepad.exe");
    source.windowTitle = QStringLiteral("无标题 - 记事本");
    source.screenName  = QStringLiteral("TestScreen");
    source.capturedAt  = QDateTime{QDate{2026, 11, 9}, QTime{13, 14, 15}};
    const QRect available{0, 0, 1920, 1080};
    const QRect selection{800, 400, 200, 100};

    window->startAnnotation(makeCanvas({200, 100}), available, selection, source);
    ASSERT_TRUE(window->isInfoPanelVisible());

    const QWidget *panel = window->infoPanelForTesting();
    ASSERT_NE(panel, nullptr);
    auto *closeButton = panel->findChild<QAbstractButton *>(QStringLiteral("captureInfoClose"));
    ASSERT_NE(closeButton, nullptr) << "信息面板上找不到关闭按钮";
    closeButton->click();

    EXPECT_FALSE(window->isInfoPanelVisible()) << "点了关闭按钮面板还浮在桌面上";
    EXPECT_TRUE(window->isVisible()) << "关信息面板把整轮标注一起收了";

    window->startAnnotation(makeCanvas({200, 100}), available, selection, source);
    EXPECT_TRUE(window->isInfoPanelVisible()) << "下一轮开窗没有把面板重新摆出来";
}

/**
 * @brief 钉住抓屏前的浮层收起：面板随窗口与工具栏一起消失，抓完只按本轮策略恢复
 * @details F5 与拖边框重抓都走这条路。面板是独立顶层窗，只藏窗口与工具栏会把它留在抓帧里；
 *          用户在这一轮亲手收起过面板时，恢复不许替用户打开，下一轮开窗仍按开关与来源重新判定。
 */
TEST_F(AnnotationWindowFixture, CaptureHidingTakesTheInfoPanelAndRestoresOnlyAllowedOnes)
{
    ASSERT_TRUE(settings().setValue(GyanisScribe::Core::kCaptureShowInfoPanel, true).has_value()) << "写入信息面板开关失败";
    auto window = makeWindow();

    CaptureSourceInfo source;
    source.processName = QStringLiteral("notepad.exe");
    source.windowTitle = QStringLiteral("无标题 - 记事本");
    source.screenName  = QStringLiteral("TestScreen");
    source.capturedAt  = QDateTime{QDate{2026, 11, 9}, QTime{13, 14, 15}};
    const QRect available{0, 0, 1920, 1080};
    const QRect selection{800, 400, 200, 100};

    window->startAnnotation(makeCanvas({200, 100}), available, selection, source);
    ASSERT_TRUE(window->isInfoPanelVisible());
    const QWidget *panel = window->infoPanelForTesting();
    ASSERT_NE(panel, nullptr);

    window->hideOverlaysForCapture();
    EXPECT_FALSE(window->isVisible()) << "抓屏前标注窗还露着，会把自己拍进底图";
    EXPECT_FALSE(window->toolbar().isVisible()) << "抓屏前工具栏还露着，会把自己拍进底图";
    EXPECT_FALSE(window->isInfoPanelVisible()) << "抓屏前信息面板还露着，会把自己拍进底图";

    window->restoreOverlaysAfterCapture();
    EXPECT_TRUE(window->isVisible()) << "抓完没把标注窗放回来";
    EXPECT_TRUE(window->toolbar().isVisible()) << "抓完没把工具栏放回来";
    EXPECT_TRUE(window->isInfoPanelVisible()) << "抓完没把本轮该露的信息面板放回来";

    // 用户点了面板右上角的关闭，再走一轮抓屏：恢复只还窗口与工具栏，面板不复活
    auto *closeButton = panel->findChild<QAbstractButton *>(QStringLiteral("captureInfoClose"));
    ASSERT_NE(closeButton, nullptr);
    closeButton->click();
    ASSERT_FALSE(window->isInfoPanelVisible());

    window->hideOverlaysForCapture();
    window->restoreOverlaysAfterCapture();
    EXPECT_TRUE(window->isVisible());
    EXPECT_TRUE(window->toolbar().isVisible());
    EXPECT_FALSE(window->isInfoPanelVisible()) << "用户亲手收起的面板被抓屏恢复重新打开了";

    // 新一轮开窗重置「本轮收起」：开关与来源仍成立时面板要重新露出来
    window->startAnnotation(makeCanvas({200, 100}), available, selection, source);
    EXPECT_TRUE(window->isInfoPanelVisible()) << "新一轮没有按开关与来源重新评估面板";
}

/**
 * @brief 钉住窄屏兜底：工具栏两侧都放不下时面板落到可用区左上角内缩一格
 * @details 摆放下限是「不出屏」：兜底位仍要整块落在可用区内，也不压住截下来的选区；
 *          位置钉死是为了让后续改摆放顺序时，这一档不会悄悄漂走。
 */
TEST_F(AnnotationWindowFixture, InfoPanelFallsBackToTheAvailableCornerOnANarrowScreen)
{
    ASSERT_TRUE(settings().setValue(GyanisScribe::Core::kCaptureShowInfoPanel, true).has_value()) << "写入信息面板开关失败";
    auto window = makeWindow();

    CaptureSourceInfo source;
    source.processName = QStringLiteral("notepad.exe");
    source.windowTitle = QStringLiteral("无标题 - 记事本");
    source.screenName  = QStringLiteral("TestScreen");
    source.capturedAt  = QDateTime{QDate{2026, 11, 9}, QTime{13, 14, 15}};
    const QRect available{0, 0, 360, 720};
    const QRect selection{80, 300, 200, 100};

    window->startAnnotation(makeCanvas({200, 100}), available, selection, source);

    ASSERT_TRUE(window->isInfoPanelVisible());
    const QWidget *panel = window->infoPanelForTesting();
    ASSERT_NE(panel, nullptr);
    const QPoint expectedFallback = available.topLeft() + QPoint(12, 12);
    EXPECT_EQ(panel->geometry().topLeft(), expectedFallback) << "窄屏兜底位不是可用区左上角内缩 12px";
    EXPECT_TRUE(available.contains(panel->geometry())) << "兜底位把面板摆出了屏幕可用区";
    EXPECT_FALSE(panel->geometry().intersects(selection)) << "兜底位压住了截下来的选区";
}

/**
 * @brief 钉住收场各出口都收面板：Esc 与交付（回车复制）都不许把面板留在桌面上
 * @details 面板是独立顶层窗，只藏标注窗会把它留在原地；每档各开一轮新窗口分别核对。
 */
TEST_F(AnnotationWindowFixture, InfoPanelHidesWhenTheRoundEnds)
{
    ASSERT_TRUE(settings().setValue(GyanisScribe::Core::kCaptureShowInfoPanel, true).has_value()) << "写入信息面板开关失败";

    CaptureSourceInfo source;
    source.processName = QStringLiteral("notepad.exe");
    source.windowTitle = QStringLiteral("无标题 - 记事本");
    source.screenName  = QStringLiteral("TestScreen");
    source.capturedAt  = QDateTime{QDate{2026, 11, 9}, QTime{13, 14, 15}};
    const QRect available{0, 0, 1920, 1080};
    const QRect selection{800, 400, 200, 100};

    const std::vector<std::pair<Qt::Key, Qt::KeyboardModifiers>> endings{
            {Qt::Key_Escape, Qt::NoModifier},
            {Qt::Key_Return, Qt::NoModifier},
    };
    for (const auto &[key, modifiers]: endings)
    {
        auto window = makeWindow();
        window->startAnnotation(makeCanvas({200, 100}), available, selection, source);
        ASSERT_TRUE(window->isInfoPanelVisible()) << "按键 " << static_cast<int>(key) << " 那一轮的开始面板没出来";

        pressKeyOn(*window, key, modifiers);

        EXPECT_FALSE(window->isVisible()) << "按键 " << static_cast<int>(key) << " 没收起标注窗";
        EXPECT_FALSE(window->isInfoPanelVisible()) << "按键 " << static_cast<int>(key) << " 收场后信息面板还留在桌面上";
    }

    // 系统关窗与工具栏最右那枚 X 也汇到同一条收场路：面板不许留在桌面上
    auto closedBySystem = makeWindow();
    closedBySystem->startAnnotation(makeCanvas({200, 100}), available, selection, source);
    ASSERT_TRUE(closedBySystem->isInfoPanelVisible());
    closedBySystem->close();
    EXPECT_FALSE(closedBySystem->isInfoPanelVisible()) << "系统关窗之后信息面板还留在桌面上";

    auto closedByToolbar = makeWindow();
    closedByToolbar->startAnnotation(makeCanvas({200, 100}), available, selection, source);
    ASSERT_TRUE(closedByToolbar->isInfoPanelVisible());
    QToolButton *toolbarClose = nullptr;
    for (auto *candidate: closedByToolbar->toolbar().findChildren<QToolButton *>())
    {
        if (candidate->accessibleName() == GyanisScribe::Base::uiText("关闭标注"))
        {
            toolbarClose = candidate;
        }
    }
    ASSERT_NE(toolbarClose, nullptr) << "工具栏上找不到关闭按钮";
    toolbarClose->click();
    EXPECT_FALSE(closedByToolbar->isInfoPanelVisible()) << "点了工具栏的 X 之后信息面板还留在桌面上";
}

/**
 * @brief 钉住面板不改画布与选中：底图、逻辑尺寸与选中包围盒都与没有面板时同解
 * @details 面板是盖在屏幕上的另一扇窗，不该进引擎：底图不变、选中还在原处，
 *          导出走的仍是画布那一份（400×200 物理像素，不会把面板烘进去）。
 */
TEST_F(AnnotationWindowFixture, InfoPanelLeavesTheCanvasAndSelectionAlone)
{
    ASSERT_TRUE(settings().setValue(GyanisScribe::Core::kCaptureShowInfoPanel, true).has_value()) << "写入信息面板开关失败";
    auto window = makeWindow();

    CaptureSourceInfo source;
    source.processName = QStringLiteral("notepad.exe");
    source.windowTitle = QStringLiteral("无标题 - 记事本");
    source.screenName  = QStringLiteral("TestScreen");
    source.capturedAt  = QDateTime{QDate{2026, 11, 9}, QTime{13, 14, 15}};
    window->startAnnotation(makeCanvas({200, 100}), QRect{0, 0, 1920, 1080}, QRect{800, 400, 200, 100}, source);
    ASSERT_TRUE(window->isInfoPanelVisible());

    const auto canvasKey = window->canvas().image.cacheKey();
    const auto selected  = drawAndSelect({10, 10}, {60, 40});
    ASSERT_TRUE(selected.has_value());

    EXPECT_EQ(window->canvas().image.cacheKey(), canvasKey) << "面板露出来时底图被换了";
    EXPECT_EQ(window->canvas().logicalSize, QSize(200, 100)) << "面板改了画布逻辑尺寸";
    EXPECT_EQ(engine().selections().size(), 1U) << "面板把选中集改了";
    EXPECT_EQ(engine().selectionBounds(), selected) << "面板挪动了选中项";
    EXPECT_EQ(window->canvas().image.size(), QSize(400, 200)) << "导出用的画布被面板改了尺寸";
}

/**
 * @brief 钉住部分来源照样显示：应用与标题空着时给「未知」两档，屏幕与时刻照实写
 * @details 回溯换帧那类来源只报屏幕与时刻；控制器要的是把拿到的那几项摆出来，
 *          不是整块面板一起藏掉。
 */
TEST_F(AnnotationWindowFixture, InfoPanelKeepsPartialSourceWithUnknownFields)
{
    ASSERT_TRUE(settings().setValue(GyanisScribe::Core::kCaptureShowInfoPanel, true).has_value()) << "写入信息面板开关失败";
    auto window = makeWindow();

    CaptureSourceInfo source;
    source.screenName = QStringLiteral("TestScreen");
    source.capturedAt = QDateTime{QDate{2026, 11, 9}, QTime{8, 9, 10}};

    window->startAnnotation(makeCanvas({200, 100}), QRect{0, 0, 1920, 1080}, QRect{800, 400, 200, 100}, source);

    ASSERT_TRUE(window->isInfoPanelVisible()) << "只有屏幕与时刻也算有来源，面板该露出来";
    const QWidget *panel = window->infoPanelForTesting();
    ASSERT_NE(panel, nullptr);
    EXPECT_EQ(panelLabelText(*panel, "captureInfoApp"), GyanisScribe::Base::uiText("未知应用"));
    EXPECT_EQ(panelLabelText(*panel, "captureInfoTitle"), GyanisScribe::Base::uiText("未知窗口"));
    EXPECT_EQ(panelLabelText(*panel, "captureInfoMonitor"), source.screenName);
    EXPECT_EQ(panelLabelText(*panel, "captureInfoTime"), QStringLiteral("2026-11-09 08:09:10"));

    // 只剩应用名时另外三项都写「未知」：进程名带路径也按主名显示，时刻无效不猜一个现在
    CaptureSourceInfo appOnly;
    appOnly.processName = QStringLiteral("C:/Tools/notepad.exe");
    window->startAnnotation(makeCanvas({200, 100}), QRect{0, 0, 1920, 1080}, QRect{800, 400, 200, 100}, appOnly);

    ASSERT_TRUE(window->isInfoPanelVisible()) << "只有应用名也算有来源，面板该露出来";
    EXPECT_EQ(panelLabelText(*panel, "captureInfoApp"), QStringLiteral("notepad")) << "带路径的进程名也要只留主名";
    EXPECT_EQ(panelLabelText(*panel, "captureInfoTitle"), GyanisScribe::Base::uiText("未知窗口"));
    EXPECT_EQ(panelLabelText(*panel, "captureInfoMonitor"), GyanisScribe::Base::uiText("未知屏幕"));
    EXPECT_EQ(panelLabelText(*panel, "captureInfoTime"), GyanisScribe::Base::uiText("未知时间"));
}

/**
 * @brief 钉住「美化分享」入口：发出成品、标注窗不收场
 * @details 美化是继续编辑之前的出口，不是交付终点；与复制/保存不同，
 *          点完按钮窗还开着、轮次还活着，也不能发 closed。
 *          成品与画布原图同为 400x200，靠倍比与笔迹像素区分，判的就是「烘过没有」。
 */
TEST_F(AnnotationWindowFixture, ShareButtonEmitsTheBakedImageAndKeepsTheRoundOpen)
{
    auto window = makeWindow();
    window->startAnnotation(makeCanvas({200, 100}), QRect{0, 0, 1920, 1080}, QRect{0, 0, 200, 100});
    ASSERT_TRUE(window->isVisible());

    // 画一条：交出去的若是画布原图，成品上就找不到这条笔迹
    ASSERT_TRUE(drawAndSelect({10, 10}, {60, 40}).has_value());

    QToolButton *share = nullptr;
    for (auto *candidate: window->toolbar().findChildren<QToolButton *>())
    {
        if (candidate->objectName() == QStringLiteral("shareButton"))
        {
            share = candidate;
            break;
        }
    }
    ASSERT_NE(share, nullptr) << "工具栏上找不到美化分享按钮";
    EXPECT_TRUE(share->toolTip().contains(QStringLiteral("美化分享")));

    QSignalSpy shared{window.get(), &AnnotationWindow::shareRequested};
    QSignalSpy closed{window.get(), &AnnotationWindow::closed};
    share->click();

    ASSERT_EQ(shared.count(), 1) << "按钮点了没把成品发出来";
    const auto image = shared.at(0).at(0).value<QImage>();
    EXPECT_FALSE(image.isNull());
    EXPECT_EQ(image.size(), QSize(400, 200)) << "交出去的不是画布的物理尺寸";
    // 画布原图的倍比是 1、导出图按物理/逻辑算成 2：只比尺寸时两者都是 400x200，判不出烘没烘
    EXPECT_DOUBLE_EQ(image.devicePixelRatio(), 2.0) << "交出去的是画布原图，不是标回倍比的成品";
    // 矩形上边框那一段在物理图上不会是底色的灰（逻辑坐标乘 2）
    EXPECT_NE(image.pixelColor(60, 20), QColor(180, 180, 180)) << "交出去的是底图，不是画上图形的成品";
    EXPECT_TRUE(window->isVisible()) << "美化分享把标注窗关了";
    EXPECT_TRUE(window->isRoundActive()) << "美化分享把这一轮画上了句号";
    EXPECT_EQ(closed.count(), 0) << "美化分享不该发 closed";
}

/**
 * @brief 钉住「Bug 报告」入口：交出去的是成品，窗口不收场
 * @details 与美化分享同口径：报告里要的是用户看到的成品，宿主拿着它去拼报告；
 *          报告对话框由宿主接手，标注窗留着，这一轮也不算收场。
 */
TEST_F(AnnotationWindowFixture, BugReportButtonEmitsTheRenderedImageAndKeepsTheWindow)
{
    auto window = makeWindow();
    window->startAnnotation(makeCanvas({200, 100}), QRect{0, 0, 1920, 1080}, QRect{0, 0, 200, 100});
    ASSERT_TRUE(window->isVisible());
    ASSERT_TRUE(drawAndSelect({10, 10}, {60, 40}).has_value());

    auto *button = window->toolbar().findChild<QToolButton *>(QStringLiteral("bugReportButton"));
    ASSERT_NE(button, nullptr) << "工具栏上找不到 Bug 报告按钮";
    EXPECT_TRUE(button->toolTip().contains(QStringLiteral("Bug 报告")));

    QSignalSpy requested{window.get(), &AnnotationWindow::bugReportRequested};
    QSignalSpy closed{window.get(), &AnnotationWindow::closed};
    button->click();

    ASSERT_EQ(requested.count(), 1) << "按钮点了没发 bugReportRequested";
    const auto image = requested.at(0).at(0).value<QImage>();
    ASSERT_FALSE(image.isNull());
    EXPECT_EQ(image.size(), QSize(400, 200));
    EXPECT_DOUBLE_EQ(image.devicePixelRatio(), 2.0) << "交出去的是画布原图，不是标回倍比的成品";
    EXPECT_NE(image.pixelColor(60, 20), QColor(180, 180, 180)) << "交出去的是底图，不是画上图形的成品";
    EXPECT_TRUE(window->isVisible()) << "Bug 报告把标注窗收了";
    EXPECT_TRUE(window->isRoundActive());
    EXPECT_EQ(closed.count(), 0);
}

/**
 * @brief 钉住长截图的自动发起：置了标记时载入底图后自己发一次，且只发一次
 * @details 标记是一次性的：下一次普通载入不该再触发，否则每张截图都会弹一遍审核。
 */
TEST_F(AnnotationWindowFixture, AutoPrivacyOnLoadFiresOnceAndOnlyOnce)
{
    auto window       = makeWindow();
    auto privacyCalls = 0;
    QObject::connect(window.get(), &AnnotationWindow::privacyRequested, [&](const QImage &) { ++privacyCalls; });

    window->setAutoPrivacyOnLoad(true);
    window->startAnnotation(makeCanvas({200, 100}), QRect{0, 0, 1920, 1080}, QRect{0, 0, 200, 100});
    QElapsedTimer watch;
    watch.start();
    while (privacyCalls == 0 && watch.elapsed() < 3'000)
    {
        QApplication::processEvents(QEventLoop::AllEvents, 10);
    }
    ASSERT_EQ(privacyCalls, 1) << "长截图落地后没有自动发起隐私打码";

    // 第二次普通载入：标记已清，不该再发
    window->startAnnotation(makeCanvas({200, 100}), QRect{0, 0, 1920, 1080}, QRect{0, 0, 200, 100});
    for (int round = 0; round < 5; ++round)
    {
        QApplication::processEvents();
    }
    EXPECT_EQ(privacyCalls, 1) << "一次性标记没有清掉，第二张也被扫了";
}
