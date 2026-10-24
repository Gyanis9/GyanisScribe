// 焦点环覆盖层的像素与几何判据：输入面双环、失焦回收、高对比度与标注工具栏例外、
// 零边距行与滚动区的宿主选择、下拉框/微调框的内部行编辑映射、移动缩放跟随、
// 只读控件跳过、主题切换重装，以及勾选的工具按钮仍要拿到单环。
// 与 TestControlsChrome 同进程，共用它那份 QApplication。
#include "Gui/Controls/FocusRing.h"

#include "Gui/Theme/DesignTokens.h"
#include "Gui/Theme/ThemeManager.h"

#include <gtest/gtest.h>

#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QLineEdit>
#include <QScrollArea>
#include <QScrollBar>
#include <QSpinBox>
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

    /// 排空 deleteLater 投递的延迟删除：覆盖层收起后要等事件循环转一圈才真正离开组件树
    void flushDeferredDeletes()
    {
        QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
        QCoreApplication::processEvents();
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
 * @brief 钉住标注工具栏内的控件不装双环，只把 1px 透明边框换成主色
 * @details 工具栏是 26px 紧凑档、行距只有 2px，6px 外扩的环会压到相邻按钮上；
 *          聚焦只换边色不换尺寸，输入框不应被挤动。
 */
TEST_F(FocusRingFixture, CompactToolbarKeepsItsOwnFocusEdge)
{
    qApp->setStyleSheet(GyanisScribe::Gui::ThemeManager::styleSheetFor(ThemeKind::Light));

    QWidget host;
    host.setObjectName(QStringLiteral("AnnotationToolbar"));
    host.resize(240, 80);
    auto *edit = new QLineEdit{&host};
    edit->setGeometry(30, 24, 180, 32);
    host.show();
    QCoreApplication::processEvents();

    const QSize sizeBeforeFocus = edit->size();
    edit->setFocus();
    QCoreApplication::processEvents();

    EXPECT_EQ(host.findChildren<FocusRingOverlay *>().size(), 0) << "紧凑工具栏不该装双环";
    EXPECT_EQ(edit->size(), sizeBeforeFocus) << "聚焦换边色把输入框尺寸改了";
    EXPECT_EQ(edit->size(), QSize(180, 32)) << "输入框没落在设定的尺寸上";

    const auto   shot = host.grab().toImage();
    const QColor edge = shot.pixelColor(edit->geometry().left(), edit->geometry().center().y());
    EXPECT_EQ(edge, kLightPalette.accent) << "紧凑工具栏的焦点边缘不是主色，实际是 " << edge.name().toStdString();
}

/**
 * @brief 钉住复选框的环套在 indicator 上而不是整行文字上
 * @details 环的几何等于指示器矩形四周各外扩 6px，左上角要落在指示器左上角外侧 6px。
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

    const QPoint indicatorInHost = check->mapTo(overlays.front()->parentWidget(), indicator.topLeft());
    EXPECT_EQ(overlays.front()->geometry().topLeft(), indicatorInHost - QPoint(6, 6)) << "复选框的环左上角没落在指示器外侧 6px";
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

/**
 * @brief 钉住零边距行里的输入框把覆盖层挂到能容下 6px 外扩的页卡片上
 * @details 行容器自己不吃边距时它的矩形与输入框等大，Qt 会把子控件裁在行矩形里；
 *          覆盖层必须挂到再上一层的页卡片，输入框左侧外的那几个像素才画得出来。
 */
TEST_F(FocusRingFixture, ZeroMarginRowParentsTheRingToThePageCard)
{
    QWidget page;
    page.setStyleSheet(QStringLiteral("background-color: #FCFCFB;"));
    page.resize(320, 160);
    auto *row = new QWidget{&page};
    row->setGeometry(20, 40, 200, 32);
    auto *edit = new QLineEdit{row};
    edit->setGeometry(0, 0, 200, 32);
    page.show();
    edit->setFocus();
    QCoreApplication::processEvents();

    const auto overlays = page.findChildren<FocusRingOverlay *>();
    ASSERT_EQ(overlays.size(), 1) << "输入框聚焦后没装焦点环";
    EXPECT_EQ(overlays.front()->parentWidget(), &page) << "覆盖层挂在了会被裁切的行容器上";
    EXPECT_EQ(overlays.front()->geometry(), QRect(14, 34, 212, 44)) << "覆盖层没按页卡片坐标系外扩 6px";

    const QPoint fieldTopLeft = edit->mapTo(&page, QPoint(0, 0));
    const auto   shot         = page.grab().toImage();
    const int    y            = fieldTopLeft.y() + edit->height() / 2;
    const int    left         = fieldTopLeft.x();
    EXPECT_TRUE(isBluish(shot.pixelColor(left - 1, y))) << "输入框左缘外的内环被行容器裁掉了";
    EXPECT_FALSE(isBluish(shot.pixelColor(left - 3, y))) << "双环之间没有露出页卡片底色";
    EXPECT_TRUE(isBluish(shot.pixelColor(left - 5, y))) << "输入框左缘外的外环被行容器裁掉了";
}

/**
 * @brief 钉住可编辑下拉框把内部行编辑的焦点映射回整块下拉框
 * @details 焦点由下拉框的 lineEdit 持有，覆盖层尺寸必须按整块下拉框算，不能只圈文字区。
 */
TEST_F(FocusRingFixture, EditableComboMapsItsLineEditBackToTheCombo)
{
    QWidget host;
    host.setStyleSheet(QStringLiteral("background-color: #FCFCFB;"));
    host.resize(280, 110);
    auto *combo = new QComboBox{&host};
    combo->setEditable(true);
    combo->setGeometry(40, 30, 180, 32);
    host.show();
    combo->lineEdit()->setFocus();
    QCoreApplication::processEvents();

    ASSERT_TRUE(combo->lineEdit()->hasFocus()) << "焦点没有落在下拉框的内部行编辑上";

    const auto overlays = host.findChildren<FocusRingOverlay *>();
    ASSERT_EQ(overlays.size(), 1) << "可编辑下拉框聚焦后没装焦点环";
    EXPECT_EQ(overlays.front()->parentWidget(), &host);
    EXPECT_EQ(overlays.front()->geometry(), QRect(34, 24, 192, 44)) << "覆盖层没按整块下拉框外扩 6px";
}

/**
 * @brief 钉住微调框把内部行编辑的焦点映射回整块微调框
 * @details 判据与下拉框同一套：尺寸是微调框尺寸加 12，不是内部行编辑的尺寸。
 */
TEST_F(FocusRingFixture, SpinBoxMapsItsLineEditBackToTheSpinBox)
{
    QWidget host;
    host.setStyleSheet(QStringLiteral("background-color: #FCFCFB;"));
    host.resize(280, 110);
    auto *spin = new QSpinBox{&host};
    spin->setGeometry(40, 30, 140, 32);
    host.show();

    // 微调框的内部行编辑平时不接焦点；这里显式放开它，验证焦点落在那层时仍映射回整块微调框
    auto *lineEdit = spin->findChild<QLineEdit *>();
    ASSERT_NE(lineEdit, nullptr) << "微调框内部没有行编辑";
    lineEdit->setFocusPolicy(Qt::StrongFocus);
    lineEdit->setFocus();
    QCoreApplication::processEvents();
    ASSERT_TRUE(lineEdit->hasFocus()) << "焦点没有落在微调框的内部行编辑上";

    const auto overlays = host.findChildren<FocusRingOverlay *>();
    ASSERT_EQ(overlays.size(), 1) << "微调框聚焦后没装焦点环";
    EXPECT_EQ(overlays.front()->parentWidget(), &host);
    EXPECT_EQ(overlays.front()->geometry().size(), spin->size() + QSize(12, 12)) << "覆盖层没按整块微调框外扩 6px";
}

/**
 * @brief 钉住目标移动或缩放时覆盖层跟着走
 * @details 目标挂在零边距行里，行移动时目标自身收不到移动事件，控制器必须旁听祖先的移动。
 */
TEST_F(FocusRingFixture, RingFollowsTargetMoveAndResize)
{
    QWidget page;
    page.setStyleSheet(QStringLiteral("background-color: #FCFCFB;"));
    page.resize(340, 240);
    auto *row = new QWidget{&page};
    row->setGeometry(30, 50, 200, 32);
    auto *edit = new QLineEdit{row};
    edit->setGeometry(0, 0, 200, 32);
    page.show();
    edit->setFocus();
    QCoreApplication::processEvents();

    const auto overlays = page.findChildren<FocusRingOverlay *>();
    ASSERT_EQ(overlays.size(), 1);
    auto *ring = overlays.front();
    EXPECT_EQ(ring->parentWidget(), &page);
    EXPECT_EQ(ring->geometry(), QRect(24, 44, 212, 44));

    row->move(60, 90);
    QCoreApplication::processEvents();
    EXPECT_EQ(ring->geometry(), QRect(54, 84, 212, 44)) << "目标随行容器移动后覆盖层没跟上";

    edit->resize(160, 28);
    QCoreApplication::processEvents();
    EXPECT_EQ(ring->geometry(), QRect(54, 84, 172, 40)) << "目标缩放后覆盖层没跟上";

    edit->move(10, 4);
    QCoreApplication::processEvents();
    EXPECT_EQ(ring->geometry(), QRect(64, 88, 172, 40)) << "目标在行内移动后覆盖层没跟上";
}

/**
 * @brief 钉住滚动区里的目标滚动后仍被覆盖层盖住
 * @details 覆盖层挂在页卡片这层内容祖先上，与目标一起随内容滚动；挂到视口上就会错位。
 */
TEST_F(FocusRingFixture, RingStaysWithTheTargetWhenTheScrollAreaScrolls)
{
    QScrollArea area;
    area.setFrameShape(QFrame::NoFrame);
    area.setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    area.resize(300, 220);
    auto *content = new QWidget;
    content->resize(300, 700);
    area.setWidget(content);

    auto *page = new QWidget{content};
    page->setStyleSheet(QStringLiteral("background-color: #FCFCFB;"));
    page->setGeometry(10, 10, 260, 520);
    auto *row = new QWidget{page};
    row->setGeometry(20, 200, 200, 32);
    auto *edit = new QLineEdit{row};
    edit->setGeometry(0, 0, 200, 32);

    area.show();
    edit->setFocus();
    QCoreApplication::processEvents();

    const auto overlays = area.findChildren<FocusRingOverlay *>();
    ASSERT_EQ(overlays.size(), 1);
    auto *ring = overlays.front();
    EXPECT_EQ(ring->parentWidget(), page) << "滚动区的覆盖层没挂到页卡片内容祖先上";
    EXPECT_NE(ring->parentWidget(), area.viewport()) << "滚动区的覆盖层挂到了视口上";
    EXPECT_EQ(ring->geometry(), QRect(14, 194, 212, 44));

    area.verticalScrollBar()->setValue(120);
    QCoreApplication::processEvents();
    ASSERT_EQ(area.verticalScrollBar()->value(), 120);
    EXPECT_TRUE(ring->isVisible());
    EXPECT_EQ(ring->geometry(), QRect(14, 194, 212, 44)) << "覆盖层没有与目标一起随内容滚动";

    const QPoint fieldTopLeft = edit->mapTo(area.viewport(), QPoint(0, 0));
    ASSERT_GT(fieldTopLeft.y(), 6);
    const auto shot = area.viewport()->grab().toImage();
    EXPECT_TRUE(isBluish(shot.pixelColor(fieldTopLeft.x() - 1, fieldTopLeft.y() + edit->height() / 2))) << "滚动后内环没盖住输入框左缘";
}

/**
 * @brief 钉住只读微调框与只读可编辑下拉框都不装覆盖层
 * @details 只读控件没有编辑焦点可表达，环会把它误画成可写输入面。
 */
TEST_F(FocusRingFixture, ReadOnlySpinBoxAndComboStayWithoutAnOverlay)
{
    QWidget host;
    host.setStyleSheet(QStringLiteral("background-color: #FCFCFB;"));
    host.resize(320, 140);
    auto *spin = new QSpinBox{&host};
    spin->setReadOnly(true);
    spin->setGeometry(30, 20, 120, 32);
    auto *combo = new QComboBox{&host};
    combo->setEditable(true);
    combo->lineEdit()->setReadOnly(true);
    combo->setGeometry(30, 70, 180, 32);
    host.show();

    spin->setFocus();
    QCoreApplication::processEvents();
    EXPECT_EQ(host.findChildren<FocusRingOverlay *>().size(), 0) << "只读微调框不该装焦点环";

    combo->setFocus();
    QCoreApplication::processEvents();
    EXPECT_EQ(host.findChildren<FocusRingOverlay *>().size(), 0) << "只读行编辑的可编辑下拉框不该装焦点环";
}

/**
 * @brief 钉住焦点不变时切档会撤环与补环
 * @details 高对比度不装覆盖层；从高对比度切回浅色或深色时焦点没变、不会再来 FocusIn，
 *          控制器必须主动给当前焦点补环。
 */
TEST_F(FocusRingFixture, ThemeChangeRingsAndUnringsTheFocusedWidget)
{
    QWidget host;
    host.setStyleSheet(QStringLiteral("background-color: #FCFCFB;"));
    host.resize(220, 90);
    auto *edit = new QLineEdit{&host};
    edit->setGeometry(30, 28, 160, 32);
    host.show();
    edit->setFocus();
    QCoreApplication::processEvents();
    ASSERT_EQ(host.findChildren<FocusRingOverlay *>().size(), 1);

    m_controller->setThemeKind(ThemeKind::HighContrast);
    flushDeferredDeletes();
    EXPECT_EQ(host.findChildren<FocusRingOverlay *>().size(), 0) << "切到高对比度后覆盖层没撤掉";
    ASSERT_TRUE(edit->hasFocus()) << "切档不该改变焦点";

    m_controller->setThemeKind(ThemeKind::Light);
    flushDeferredDeletes();
    EXPECT_EQ(host.findChildren<FocusRingOverlay *>().size(), 1) << "从高对比度切回浅色后没给当前焦点补环";

    m_controller->setThemeKind(ThemeKind::HighContrast);
    flushDeferredDeletes();
    EXPECT_EQ(host.findChildren<FocusRingOverlay *>().size(), 0) << "再次切到高对比度后覆盖层没撤掉";

    m_controller->setThemeKind(ThemeKind::Dark);
    flushDeferredDeletes();
    EXPECT_EQ(host.findChildren<FocusRingOverlay *>().size(), 1) << "从高对比度切到深色后没给当前焦点补环";
}
