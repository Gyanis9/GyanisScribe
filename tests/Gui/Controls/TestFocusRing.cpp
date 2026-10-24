// 焦点环覆盖层的像素判据：输入面双环、失焦回收、高对比度与标注工具栏例外，
// 以及勾选的工具按钮仍要拿到单环。与 TestControlsChrome 同进程，共用它那份 QApplication。
#include "Gui/Controls/FocusRing.h"

#include "Gui/Theme/DesignTokens.h"
#include "Gui/Theme/ThemeManager.h"

#include <gtest/gtest.h>

#include <QApplication>
#include <QCheckBox>
#include <QLineEdit>
#include <QStyle>
#include <QStyleOptionButton>
#include <QToolButton>
#include <QWidget>

#include <memory>

using GyanisScribe::Gui::FocusRingController;
using GyanisScribe::Gui::FocusRingOverlay;
using GyanisScribe::Gui::kLightPalette;
using GyanisScribe::Gui::ThemeKind;

namespace
{
    class FocusRingFixture : public ::testing::Test
    {
    protected:
        void SetUp() override
        {
            m_controller = std::make_unique<FocusRingController>([] { return kLightPalette; });
            m_controller->setThemeKind(ThemeKind::Light);
            qApp->installEventFilter(m_controller.get());
        }

        void TearDown() override
        {
            qApp->removeEventFilter(m_controller.get());
            m_controller.reset();
            qApp->setStyleSheet(QString{});
        }

        std::unique_ptr<FocusRingController> m_controller;
    };

    bool isBluish(const QColor &color)
    {
        return color.blue() > color.red() + 18;
    }
} // namespace

/**
 * @brief 钉住输入框聚焦时画出内环、空隙与外环三段
 * @details 取输入框左侧同一行的三个像素：内环贴边、中间露出父底、外环在外侧。
 */
TEST_F(FocusRingFixture, LineEditFocusDrawsTwoRingsOutsideTheField)
{
    QWidget host;
    host.setStyleSheet(QStringLiteral("background-color: #FCFCFB;"));
    host.resize(260, 90);
    auto *edit = new QLineEdit{&host};
    edit->setGeometry(40, 30, 180, 32);
    host.show();
    edit->setFocus();
    QCoreApplication::processEvents();

    const auto shot = host.grab().toImage();
    const int  y    = edit->geometry().center().y();
    const int  left = edit->geometry().left();

    EXPECT_TRUE(isBluish(shot.pixelColor(left - 1, y))) << "内环没贴住输入框边缘";
    EXPECT_FALSE(isBluish(shot.pixelColor(left - 3, y))) << "双环之间没有露出父底色的空隙";
    EXPECT_TRUE(isBluish(shot.pixelColor(left - 5, y))) << "外环没画出来";
}

/**
 * @brief 钉住失焦回收覆盖层，且高对比度档从不安装覆盖层
 * @details 高对比度靠 QSS 的 2px 描边表达焦点，多一层自绘环会破坏这一档的视觉契约。
 */
TEST_F(FocusRingFixture, BlurHidesTheRingAndHighContrastNeverInstallsOne)
{
    QWidget host;
    host.setStyleSheet(QStringLiteral("background-color: #FFFFFF;"));
    host.resize(200, 80);
    auto *edit = new QLineEdit{&host};
    edit->setGeometry(30, 20, 140, 32);
    host.show();
    edit->setFocus();
    QCoreApplication::processEvents();
    edit->clearFocus();
    QCoreApplication::processEvents();
    EXPECT_EQ(host.findChildren<FocusRingOverlay *>().size(), 0) << "失焦后覆盖层没回收";

    m_controller->setThemeKind(ThemeKind::HighContrast);
    edit->setFocus();
    QCoreApplication::processEvents();
    EXPECT_EQ(host.findChildren<FocusRingOverlay *>().size(), 0) << "高对比度档不该装覆盖层";
}

/**
 * @brief 钉住标注工具栏内的控件不装双环
 * @details 工具栏是 26px 紧凑档、行距只有 2px，6px 外扩的环会压到相邻按钮上。
 */
TEST_F(FocusRingFixture, CompactToolbarKeepsItsOwnFocusEdge)
{
    QWidget host;
    host.setObjectName(QStringLiteral("AnnotationToolbar"));
    host.resize(200, 60);
    auto *edit = new QLineEdit{&host};
    edit->setGeometry(20, 15, 120, 26);
    host.show();
    edit->setFocus();
    QCoreApplication::processEvents();
    EXPECT_EQ(host.findChildren<FocusRingOverlay *>().size(), 0) << "紧凑工具栏不该装双环";
}

/**
 * @brief 钉住复选框的环套在 indicator 上而不是整行文字上
 * @details 环的几何等于指示器矩形四周各外扩 6px，左边框贴住指示器。
 */
TEST_F(FocusRingFixture, CheckBoxGetsASingleRingAroundTheIndicator)
{
    QWidget host;
    host.setStyleSheet(QStringLiteral("background-color: #FCFCFB;"));
    host.resize(220, 70);
    auto *check = new QCheckBox{QStringLiteral("勾选项"), &host};
    check->setGeometry(20, 20, 160, 24);
    host.show();
    check->setFocus();
    QCoreApplication::processEvents();
    const auto overlays = host.findChildren<FocusRingOverlay *>();
    ASSERT_EQ(overlays.size(), 1);

    QStyleOptionButton option;
    option.initFrom(check);
    option.rect           = check->rect();
    const QRect indicator = check->style()->subElementRect(QStyle::SE_CheckBoxIndicator, &option, check);
    EXPECT_EQ(overlays.front()->geometry().size(), indicator.size() + QSize(12, 12)) << "复选框的环没有套在 indicator 上";
}

/**
 * @brief 钉住勾选并聚焦的工具按钮仍有单环
 * @details QSS 的 QToolButton:checked 会把 1px 透明边框占回来，同一条边上的焦点描边在勾选态看不见；
 *          覆盖层必须独立于勾选状态，且这里只许有单环，不能出现双环。
 */
TEST_F(FocusRingFixture, CheckedToolButtonStillGetsTheSingleRing)
{
    qApp->setStyleSheet(GyanisScribe::Gui::ThemeManager::styleSheetFor(ThemeKind::Light));

    QWidget host;
    host.setStyleSheet(QStringLiteral("background-color: #FCFCFB;"));
    host.resize(220, 80);
    auto *button = new QToolButton{&host};
    button->setCheckable(true);
    button->setChecked(true);
    button->setGeometry(40, 24, 32, 32);
    host.show();
    button->setFocus();
    QCoreApplication::processEvents();

    const auto overlays = host.findChildren<FocusRingOverlay *>();
    ASSERT_EQ(overlays.size(), 1) << "勾选的工具按钮没装焦点环";
    EXPECT_EQ(overlays.front()->geometry().size(), button->size() + QSize(12, 12));

    const auto shot = host.grab().toImage();
    const int  y    = button->geometry().center().y();
    const int  left = button->geometry().left();
    EXPECT_TRUE(isBluish(shot.pixelColor(left - 1, y))) << "单环没贴住工具按钮边缘";
    EXPECT_FALSE(isBluish(shot.pixelColor(left - 5, y))) << "工具按钮只许单环，外圈不该再画一道";
}
