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
