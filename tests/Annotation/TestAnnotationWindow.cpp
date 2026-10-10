#include "Annotation/AnnotationView.h"

#include "Core/ImageFit.h"
#include "Annotation/AnnotationWindow.h"

#include "Annotation/AnnotationEngine.h"
#include "Annotation/AnnotationToolbar.h"
#include "Annotation/IAnnotationEngine.h"

#include "Settings/SettingsStore.h"
#include "Ui/IconProvider.h"

#include <gtest/gtest.h>

#include <QApplication>
#include <QImage>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QPoint>
#include <QRect>
#include <QSize>
#include <QWidget>

#include <filesystem>
#include <format>
#include <memory>
#include <optional>
#include <utility>

using GyanisScribe::Annotation::AnnotationToolbar;
using GyanisScribe::Annotation::AnnotationWindow;
using GyanisScribe::Annotation::ShortcutAction;
using GyanisScribe::Annotation::shortcutActionFor;
using GyanisScribe::Annotation::toCanvasPoint;
using GyanisScribe::Annotation::ToolKind;
using GyanisScribe::Annotation::toWidgetPoint;
using GyanisScribe::Core::CapturedImage;
using GyanisScribe::Core::displayRectFor;
using GyanisScribe::Core::ErrorCode;
using GyanisScribe::Settings::SettingsStore;
using GyanisScribe::Ui::IconProvider;

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

            m_engine = std::make_unique<GyanisScribe::Annotation::AnnotationEngine>();
            m_engine->loadCanvas(makeCanvas({200, 100}));
            // 这两步都要验成功：SetUp 里悄悄失败会让后面的用例在「工具不是矩形」的状态上跑，红了也看不出原因
            ASSERT_TRUE(m_engine->setActiveTool(ToolKind::Rectangle).has_value());
            ASSERT_TRUE(m_engine->setPenParameters(QColor{255, 0, 0}, 2, 16).has_value());
        }

        void TearDown() override
        {
            m_engine.reset();
        }

        [[nodiscard]] GyanisScribe::Annotation::IAnnotationEngine &engine() const
        {
            return *m_engine;
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
            return std::make_unique<AnnotationWindow>(engine(), std::make_unique<AnnotationToolbar>(*m_settings, *m_icons));
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
        std::unique_ptr<GyanisScribe::Annotation::AnnotationEngine> m_engine;   ///< 被测引擎，提供选中项包围盒
        std::filesystem::path                                       m_iniPath;  ///< 本次用例专用的设置文件
        std::unique_ptr<IconProvider>                               m_icons;    ///< 工具栏要的图标供给器
        std::unique_ptr<SettingsStore>                              m_settings; ///< 工具栏要的设置store
    };

} // namespace

/**
 * @brief 钉住两套坐标互指同一个点
 * @details 显示区本身的判据已上收到 tests/Core/TestImageFit.cpp，这里只测点位往返。
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

    // 这些组合不归窗口管：文字输入里按 s/z、没有 Ctrl 的 Esc 之外的键都要交回 Qt
    EXPECT_EQ(shortcutActionFor(Qt::Key_S, Qt::NoModifier), ShortcutAction::None);
    EXPECT_EQ(shortcutActionFor(Qt::Key_Z, Qt::NoModifier), ShortcutAction::None);
    EXPECT_EQ(shortcutActionFor(Qt::Key_A, Qt::ControlModifier), ShortcutAction::None);
    EXPECT_EQ(shortcutActionFor(Qt::Key_Escape, Qt::ControlModifier), ShortcutAction::None);
    EXPECT_EQ(shortcutActionFor(Qt::Key_Delete, Qt::ControlModifier), ShortcutAction::None);
    // 带 Ctrl 的方向键留给 Qt 自己用（换焦点那一类），截走就少一条既有行为
    EXPECT_EQ(shortcutActionFor(Qt::Key_Right, Qt::ControlModifier), ShortcutAction::None);
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
    GyanisScribe::Annotation::AnnotationView view{engine()};
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
    engine().clearSelection();
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
