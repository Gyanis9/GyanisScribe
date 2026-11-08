#include "Core/Geometry/ContinuousCorner.h"
#include "Gui/Controls/Button.h"
#include "Gui/Controls/Menu.h"
#include "Gui/Controls/SurfaceWidget.h"
#include "Gui/Theme/ControlPalette.h"
#include "Gui/Theme/SquircleStyle.h"

#include <gtest/gtest.h>

#include <QAction>
#include <QApplication>
#include <QCoreApplication>
#include <QImage>
#include <QMenu>
#include <QPainter>
#include <QWidget>

#include <memory>

namespace
{
    std::unique_ptr<QApplication> application;

    class CornerControlsFixture : public ::testing::Test
    {
    protected:
        static void SetUpTestSuite()
        {
            static int   argumentCount  = 1;
            static char  argumentZero[] = "TestCornerControls";
            static char *arguments[]    = {argumentZero, nullptr};
            application = std::make_unique<QApplication>(argumentCount, arguments);
            GyanisScribe::Gui::SquircleStyle::install();
        }

        static void TearDownTestSuite()
        {
            application.reset();
        }

        void SetUp() override
        {
            GyanisScribe::Gui::setTextThemeKind(GyanisScribe::Gui::ThemeKind::Light);
        }
    };
} // namespace

TEST_F(CornerControlsFixture, SurfaceWidgetPaintsSurfaceAndLeavesCornersToTheHost)
{
    QWidget host;
    host.resize(140, 100);
    host.setAutoFillBackground(true);
    host.setPalette(QPalette{QColor{255, 0, 0}});

    auto *surface = new GyanisScribe::Gui::SurfaceWidget{
            GyanisScribe::Core::CornerRole::Card, GyanisScribe::Gui::SurfaceFillRole::Surface, &host};
    surface->setGeometry(10, 10, 120, 80);
    host.show();
    QCoreApplication::processEvents();

    const QImage image = host.grab().toImage();
    EXPECT_EQ(image.pixelColor(70, 50), GyanisScribe::Gui::activePalette().surface);
    EXPECT_EQ(image.pixelColor(10, 10), (QColor{255, 0, 0})) << "左上角应露出宿主底色";
}

TEST_F(CornerControlsFixture, ButtonPaintsControlFillAndAccentDefault)
{
    QWidget host;
    host.resize(140, 60);
    host.setAutoFillBackground(true);
    host.setPalette(QPalette{QColor{255, 0, 0}});

    auto *button = new GyanisScribe::Gui::Button{QStringLiteral("确定"), &host};
    button->setGeometry(10, 10, 120, 30);
    host.show();
    QCoreApplication::processEvents();

    QImage image = host.grab().toImage();
    EXPECT_EQ(image.pixelColor(70, 25), GyanisScribe::Gui::activePalette().controlFill);

    button->setDefault(true);
    button->update();
    QCoreApplication::processEvents();
    image = host.grab().toImage();
    EXPECT_EQ(image.pixelColor(70, 25), GyanisScribe::Gui::activePalette().accent);
}

/**
 * @brief 钉住工具按钮的选中底与禁用透明底
 * @details 选中态整块铺 accentSoft；禁用态不铺底，宿主底色原样露出来。
 *          「透明」用宿主的红底判：宿主是不透明窗口，抓图里那里的 alpha 本来就是 255，
 *          能区分铺底与否的是颜色本身。
 */
TEST_F(CornerControlsFixture, ToolButtonShowsCheckedAndDisabledStates)
{
    QWidget host;
    host.resize(80, 60);
    host.setAutoFillBackground(true);
    host.setPalette(QPalette{QColor{255, 0, 0}});

    auto *button = new GyanisScribe::Gui::ToolButton{&host};
    button->setCheckable(true);
    button->setChecked(true);
    button->setGeometry(10, 10, 40, 40);
    host.show();
    QCoreApplication::processEvents();

    auto image = host.grab().toImage();
    EXPECT_EQ(image.pixelColor(30, 30), GyanisScribe::Gui::activePalette().accentSoft);

    button->setEnabled(false);
    button->update();
    QCoreApplication::processEvents();
    image = host.grab().toImage();
    EXPECT_EQ(image.pixelColor(30, 30), QColor(255, 0, 0)) << "禁用工具按钮应保持透明底，露出宿主色";
}

/**
 * @brief 钉住按钮的焦点边由自绘类画出
 * @details 高对比度档静息是 2px borderStrong 边、聚焦换成同宽主色边；
 *          这两档都只在 paintEvent 里落笔，QSS 已无按钮描边规则。
 */
TEST_F(CornerControlsFixture, ButtonSelfPaintsHighContrastBorderAndFocusEdge)
{
    GyanisScribe::Gui::setTextThemeKind(GyanisScribe::Gui::ThemeKind::HighContrast);
    const auto &palette = GyanisScribe::Gui::activePalette();

    QWidget host;
    host.resize(160, 60);
    host.setAutoFillBackground(true);
    host.setPalette(QPalette{QColor{255, 0, 0}});

    auto *button = new GyanisScribe::Gui::Button{&host};
    button->setGeometry(10, 10, 120, 32);
    host.show();
    QCoreApplication::processEvents();
    // show 之后第一个可聚焦控件会自动拿到焦点：先清掉，量的才是静息边
    button->clearFocus();
    button->update();
    QCoreApplication::processEvents();

    QImage image = host.grab().toImage();
    EXPECT_EQ(image.pixelColor(70, 26), palette.controlFill) << "高对比度档的按钮底没铺 controlFill";
    EXPECT_EQ(image.pixelColor(11, 26), palette.borderStrong) << "无焦点时没有 2px 静息边";

    button->setFocus(Qt::OtherFocusReason);
    QCoreApplication::processEvents();
    ASSERT_TRUE(button->hasFocus()) << "判据的前提没构造出来：按钮没拿到焦点";
    button->update();
    QCoreApplication::processEvents();
    image = host.grab().toImage();
    EXPECT_EQ(image.pixelColor(11, 26), palette.accent) << "聚焦后没有换成主色焦点边";
}

/**
 * @brief 钉住标注工具栏内的按钮自绘紧凑焦点边
 * @details 工具栏内的按钮靠 usesInlineFocusBorder 认出来，焦点边由 paintEvent 画；
 *          未选中时按钮不铺底，中心露出宿主色，只有一圈 1px 主色边。
 */
TEST_F(CornerControlsFixture, ToolButtonInsideTheAnnotationToolbarPaintsTheInlineFocusEdge)
{
    QWidget host;
    host.resize(120, 80);
    host.setAutoFillBackground(true);
    host.setPalette(QPalette{QColor{255, 0, 0}});
    QWidget toolbar{&host};
    toolbar.setObjectName(QStringLiteral("AnnotationToolbar"));
    toolbar.setGeometry(0, 0, 120, 80);

    auto *button = new GyanisScribe::Gui::ToolButton{&toolbar};
    button->setGeometry(20, 20, 32, 32);
    host.show();
    button->setFocus(Qt::OtherFocusReason);
    QCoreApplication::processEvents();
    ASSERT_TRUE(button->hasFocus()) << "判据的前提没构造出来：按钮没拿到焦点";

    const QImage image = toolbar.grab().toImage();
    EXPECT_EQ(image.pixelColor(20, 36), GyanisScribe::Gui::activePalette().accent) << "工具栏内的按钮没画 1px 主色焦点边";
    EXPECT_EQ(image.pixelColor(36, 36), QColor(255, 0, 0)) << "未选中、悬停与按下的工具按钮不该铺底，宿主色应露出来";
}

/**
 * @brief 钉住工具栏祖先链的判定
 * @details 焦点边是否走紧凑档只看祖先里有没有 AnnotationToolbar；
 *          工具栏内的按钮认成自绘，工具栏外的按钮不认。
 */
TEST_F(CornerControlsFixture, InlineFocusBorderIsDetectedInsideTheAnnotationToolbar)
{
    QWidget host;
    QWidget toolbar{&host};
    toolbar.setObjectName(QStringLiteral("AnnotationToolbar"));
    GyanisScribe::Gui::ToolButton button{&toolbar};
    EXPECT_TRUE(GyanisScribe::Gui::usesInlineFocusBorder(button));
    GyanisScribe::Gui::ToolButton outside{&host};
    EXPECT_FALSE(GyanisScribe::Gui::usesInlineFocusBorder(outside));
}

TEST_F(CornerControlsFixture, MenuPaintsSquirclePanelAndActiveItem)
{
    GyanisScribe::Gui::Menu menu;
    auto *first = menu.addAction(QStringLiteral("第一项"));
    menu.addAction(QStringLiteral("第二项"));
    menu.setActiveAction(first);
    menu.show();
    QCoreApplication::processEvents();

    const QImage image = menu.grab().toImage();
    const QRect item = menu.actionGeometry(first);
    ASSERT_FALSE(item.isEmpty());
    // 高亮取项内靠上沿的一格：项中心正落在「一」字的横画上，那里必须是文字而不是底色
    const QPoint itemBackground{item.center().x(), item.top() + 2};
    EXPECT_EQ(image.pixelColor(itemBackground).alpha(), 255);
    EXPECT_EQ(image.pixelColor(itemBackground), GyanisScribe::Gui::activePalette().hover);
    EXPECT_EQ(image.pixelColor(0, 0).alpha(), 0) << "菜单左上角必须透明，不能露出方形面板";
    EXPECT_EQ(image.pixelColor(menu.width() / 2, menu.height() - 8), GyanisScribe::Gui::activePalette().surface);
}

TEST_F(CornerControlsFixture, ControlPaletteMapsStatesToThemeRoles)
{
    const auto &palette = GyanisScribe::Gui::activePalette();
    EXPECT_EQ(GyanisScribe::Gui::buttonColors(palette, GyanisScribe::Gui::ThemeKind::Light, false, true, false, false, false, false).fill,
              palette.controlFill);
    EXPECT_EQ(GyanisScribe::Gui::buttonColors(palette, GyanisScribe::Gui::ThemeKind::Light, false, false, false, false, false, false).fill,
              palette.mutedFill);
    EXPECT_EQ(GyanisScribe::Gui::buttonColors(palette, GyanisScribe::Gui::ThemeKind::Light, true, true, false, false, false, false).fill,
              palette.accent);
    EXPECT_EQ(GyanisScribe::Gui::buttonColors(palette, GyanisScribe::Gui::ThemeKind::HighContrast, false, true, false, false, true, false).borderWidth,
              2);

    // 高对比度档的控件不靠底色分层：无焦点也要一圈 borderStrong 2px 边，
    // 有焦点时换成主色边，不能被静息边盖回去
    const auto &contrast = GyanisScribe::Gui::paletteFor(GyanisScribe::Gui::ThemeKind::HighContrast);
    const auto  resting  = GyanisScribe::Gui::buttonColors(contrast, GyanisScribe::Gui::ThemeKind::HighContrast, false, true, false, false, false, false);
    EXPECT_EQ(resting.border, contrast.borderStrong);
    EXPECT_EQ(resting.borderWidth, 2);
    const auto focused = GyanisScribe::Gui::buttonColors(contrast, GyanisScribe::Gui::ThemeKind::HighContrast, false, true, false, false, true, false);
    EXPECT_EQ(focused.border, contrast.accent) << "聚焦的主色边被静息黑边盖住了";

    // 工具按钮在这一档选中时同样保留主色边，对齐 QSS 的 QToolButton:checked；
    // 禁用态的 :disabled 把边压成透明，输入控件无焦点也有一圈 borderStrong 2px 边
    const auto checkedTool = GyanisScribe::Gui::toolButtonColors(contrast, GyanisScribe::Gui::ThemeKind::HighContrast, true, true, false, false, false, false);
    EXPECT_EQ(checkedTool.border, contrast.accent);
    EXPECT_EQ(checkedTool.borderWidth, 2);
    const auto checkedDisabled = GyanisScribe::Gui::toolButtonColors(contrast, GyanisScribe::Gui::ThemeKind::HighContrast, true, false, false, false, false, false);
    EXPECT_EQ(checkedDisabled.borderWidth, 0) << "禁用态的工具按钮不该有可见边";
    const auto restingInput = GyanisScribe::Gui::inputColors(contrast, GyanisScribe::Gui::ThemeKind::HighContrast, true, false, false, false);
    EXPECT_EQ(restingInput.border, contrast.borderStrong);
    EXPECT_EQ(restingInput.borderWidth, 2);
}
