#include "Core/Geometry/ContinuousCorner.h"
#include "Gui/Controls/Button.h"
#include "Gui/Controls/ComboBox.h"
#include "Gui/Controls/FontItemDelegate.h"
#include "Gui/Controls/ItemDelegate.h"
#include "Gui/Controls/LineEdit.h"
#include "Gui/Controls/ListView.h"
#include "Gui/Controls/ListWidget.h"
#include "Gui/Controls/Menu.h"
#include "Gui/Controls/ScrollBar.h"
#include "Gui/Controls/Slider.h"
#include "Gui/Controls/SpinBox.h"
#include "Gui/Controls/SurfaceWidget.h"
#include "Gui/Controls/TextEdit.h"
#include "Gui/Theme/ControlPalette.h"
#include "Gui/Theme/SquircleStyle.h"
#include "Gui/Theme/ThemeManager.h"

#include <gtest/gtest.h>

#include <QAction>
#include <QAbstractScrollArea>
#include <QApplication>
#include <QCoreApplication>
#include <QFont>
#include <QHoverEvent>
#include <QImage>
#include <QMenu>
#include <QPainter>
#include <QPalette>
#include <QStringListModel>
#include <QStyleOption>
#include <QStyledItemDelegate>
#include <QWidget>

#include <memory>

namespace
{
    std::unique_ptr<QApplication> application;

    /**
     * @brief 数一块区域里的深色笔画
     * @details 菜单指示器用文字色落笔，浅底上只有箭头笔画会同时压暗三个通道；
     *          宿主铺白底就是为了让这个判据分得清箭头与底色。
     * @param image 抓到的图
     * @param area 取样区域
     * @return 三个通道都低于 128 且不透明的像素数
     */
    int countInkPixels(const QImage &image, const QRect &area)
    {
        int count = 0;
        for (int y = area.top(); y <= area.bottom(); ++y)
        {
            for (int x = area.left(); x <= area.right(); ++x)
            {
                const QColor color = image.pixelColor(x, y);
                if (color.alpha() == 255 && color.red() < 128 && color.green() < 128 && color.blue() < 128)
                {
                    ++count;
                }
            }
        }
        return count;
    }

    /**
     * @brief 菜单指示器的取样窗口
     * @details Qt 把工具按钮的菜单指示器放在按钮右下角：原生风格用 PM_MenuButtonIndicator 见方的一小格，
     *          套上样式表后是 13px 见方的 ::menu-indicator 块，两档都落在右下角 20px 见方里
     * @param button 目标按钮
     * @return 相对按钮左上角的取样矩形
     */
    QRect menuIndicatorProbe(const QWidget &button)
    {
        constexpr int probeExtent = 20;
        return QRect{button.width() - probeExtent, button.height() - probeExtent, probeExtent, probeExtent};
    }

    /**
     * @brief 记录委托准备绘制参数时换上的字体族
     * @details FontItemDelegate::prepareOption() 是保护成员，用例通过子类在调用基类之后
     *          抄下 option.font 的族名，从而在离屏绘制里核对字体预览。
     */
    class RecordingFontItemDelegate : public GyanisScribe::Gui::FontItemDelegate
    {
    public:
        using GyanisScribe::Gui::FontItemDelegate::FontItemDelegate;

        mutable QString preparedFamily; ///< 最近一次绘制准备出的字体族

    protected:
        /**
         * @brief 抄下基类换好的字体族
         * @details 重写 FontItemDelegate::prepareOption()：先让基类换字体，再记录结果。
         * @param option 绘制参数
         * @param index 项下标
         */
        void prepareOption(QStyleOptionViewItem &option, const QModelIndex &index) const override
        {
            GyanisScribe::Gui::FontItemDelegate::prepareOption(option, index);
            preparedFamily = option.font.family();
        }
    };

    class CornerControlsFixture : public ::testing::Test
    {
    protected:
        static void SetUpTestSuite()
        {
            // 像素判据按逻辑像素取样：真机 125%/150% 缩放下 grab() 给出的是更大的图，同一个采样点会落到别处。
            // 这个二进制里的像素用例共用同一份 QApplication，强制倍比为 1；真缩放屏的观感留给实机核对
            qputenv("QT_SCALE_FACTOR", "1");
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
 * @brief 钉住工具按钮的菜单指示箭头
 * @details 带菜单的 InstantPopup 按钮本该在右下角落下指示器；没有菜单的同尺寸按钮那里必须是空的。
 *          判据用宿主白底上的深色笔画，带菜单的那枚要数得出箭头，另一枚要一粒都没有。
 */
TEST_F(CornerControlsFixture, ToolButtonPaintsTheMenuIndicatorOnlyWhenItHasAMenu)
{
    QWidget host;
    host.resize(120, 80);
    host.setAutoFillBackground(true);
    host.setPalette(QPalette{QColor{Qt::white}});

    auto *plain = new GyanisScribe::Gui::ToolButton{&host};
    plain->setGeometry(10, 10, 40, 40);

    auto *menu = new QMenu{&host};
    menu->addAction(QStringLiteral("一项"));
    auto *withMenu = new GyanisScribe::Gui::ToolButton{&host};
    withMenu->setGeometry(60, 10, 40, 40);
    withMenu->setPopupMode(QToolButton::InstantPopup);
    withMenu->setMenu(menu);

    host.show();
    QCoreApplication::processEvents();

    const QImage image = host.grab().toImage();
    const int    plainInk = countInkPixels(image, menuIndicatorProbe(*plain).translated(plain->pos()));
    const int    menuInk  = countInkPixels(image, menuIndicatorProbe(*withMenu).translated(withMenu->pos()));
    EXPECT_EQ(plainInk, 0) << "没有菜单的工具按钮不该出现指示器";
    EXPECT_GT(menuInk, 0) << "带菜单的 InstantPopup 工具按钮丢了菜单指示箭头";
}

/**
 * @brief 钉住色块按钮收掉菜单指示器
 * @details 色块自己就是入口提示，样式表用 QToolButton#annotationColorButton::menu-indicator 的 image: none
 *          把箭头收掉；同一档样式表下，把对象名换掉就该重新出现箭头，否则规则没有消费方。
 */
TEST_F(CornerControlsFixture, AnnotationColorButtonSuppressesTheMenuIndicator)
{
    struct StyleSheetGuard
    {
        QString previous = qApp->styleSheet();

        ~StyleSheetGuard()
        {
            qApp->setStyleSheet(previous);
        }
    } guard;
    qApp->setStyleSheet(GyanisScribe::Gui::ThemeManager::styleSheetFor(GyanisScribe::Gui::ThemeKind::Light));

    QWidget host;
    host.resize(140, 80);
    host.setAutoFillBackground(true);
    host.setPalette(QPalette{QColor{Qt::white}});

    auto *menu = new QMenu{&host};
    menu->addAction(QStringLiteral("一项"));

    auto *colorButton = new GyanisScribe::Gui::ToolButton{&host};
    colorButton->setObjectName(QStringLiteral("annotationColorButton"));
    colorButton->setPopupMode(QToolButton::InstantPopup);
    colorButton->setMenu(menu);
    colorButton->setGeometry(10, 10, 40, 40);

    auto *twin = new GyanisScribe::Gui::ToolButton{&host};
    twin->setPopupMode(QToolButton::InstantPopup);
    twin->setMenu(menu);
    twin->setGeometry(70, 10, 40, 40);

    host.show();
    QCoreApplication::processEvents();

    const QImage image = host.grab().toImage();
    const int    namedInk = countInkPixels(image, menuIndicatorProbe(*colorButton).translated(colorButton->pos()));
    const int    twinInk  = countInkPixels(image, menuIndicatorProbe(*twin).translated(twin->pos()));
    EXPECT_GT(twinInk, 0) << "同一档样式表下，没有这个对象名的按钮该有菜单指示箭头";
    EXPECT_EQ(namedInk, 0) << "色块按钮的菜单指示器没被样式表收掉";
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

/**
 * @brief 钉住菜单当前项高亮用的是紧凑档形状
 * @details 高亮由 Gui::Menu 按 CornerRole::Compact 自绘，样式表不再给 QMenu::item:selected 铺底。
 *          取样取当前项矩形的左上角：方角填充会把这格整块涂成悬停色，连续曲率把角切掉之后
 *          这一格只剩菜单面板自身抗锯齿留下的极浅覆盖（本机实测 alpha 2）。先在同一行的右侧
 *          断言悬停底确实在场，判据才不会被「根本没画高亮」蒙混过去。
 */
TEST_F(CornerControlsFixture, MenuHighlightUsesCompactShape)
{
    GyanisScribe::Gui::Menu menu;
    auto *action = menu.addAction(QStringLiteral("项"));
    menu.setActiveAction(action);
    menu.show();
    QCoreApplication::processEvents();
    const QRect  item  = menu.actionGeometry(action);
    const QImage image = menu.grab().toImage();
    ASSERT_FALSE(item.isEmpty());
    EXPECT_EQ(image.pixelColor(item.right() - 12, item.top() + 2), GyanisScribe::Gui::activePalette().hover) << "当前项没铺悬停底，判据的前提不成立";
    EXPECT_LT(image.pixelColor(item.left(), item.top()).alpha(), 16) << "高亮左上角不应是方形";
}

/**
 * @brief 钉住提示面板由代理风格按连续曲率画出
 * @details QToolTip 的样式表只留文字色与内边距：任何边框声明（哪怕是透明的）都会让
 *          QStyleSheetStyle 抢先把这一层画完，代理收不到 PE_PanelTipLabel。
 *          判据直接调 drawPrimitive：左上角必须保持透明（角被切掉），中心必须是本档 surface。
 */
TEST_F(CornerControlsFixture, TooltipPanelIsDrawnByTheProxyStyle)
{
    QImage image{QSize{120, 40}, QImage::Format_ARGB32_Premultiplied};
    image.fill(Qt::transparent);
    QStyleOptionFrame option;
    option.rect = QRect{0, 0, 120, 40};
    {
        QPainter painter{&image};
        qApp->style()->drawPrimitive(QStyle::PE_PanelTipLabel, &option, &painter, nullptr);
    }
    EXPECT_EQ(image.pixelColor(0, 0).alpha(), 0);
    EXPECT_EQ(image.pixelColor(60, 20), GyanisScribe::Gui::activePalette().surface);
}

/**
 * @brief 钉住列表的项高亮与容器底都由连续曲率自绘
 * @details 高亮取当前行矩形中心：那里必须是 accentSoft，样式表已经不画选中底了。
 *          容器底取两行之下的一格：那里必须铺 surface，样式表删掉 background 之后
 *          这块底只能由列表自己画；角部取自身抓图的左上角，那里必须透明，
 *          方角没有把整块矩形填满。
 */
TEST_F(CornerControlsFixture, ListWidgetPaintsItemHighlightAndContainer)
{
    GyanisScribe::Gui::ListWidget list;
    list.resize(200, 120);
    list.addItem(QStringLiteral("第一项"));
    list.addItem(QStringLiteral("第二项"));
    list.setCurrentRow(0);
    list.show();
    QCoreApplication::processEvents();

    const QImage viewportImage = list.viewport()->grab().toImage();
    const QRect  item          = list.visualItemRect(list.item(0));
    ASSERT_FALSE(item.isEmpty());
    EXPECT_EQ(viewportImage.pixelColor(item.center()), GyanisScribe::Gui::activePalette().accentSoft);
    const QImage image = list.grab().toImage();
    EXPECT_EQ(image.pixelColor(0, 0).alpha(), 0) << "列表容器左上角应透明";
    EXPECT_EQ(image.pixelColor(100, 100), GyanisScribe::Gui::activePalette().surface) << "列表容器底没铺 surface";
}

/**
 * @brief 钉住列表项的悬停反馈与选中优先
 * @details 悬停底与选中底都由 ItemDelegate 按项的 State_MouseOver 与 State_Selected 画，
 *          而这两个状态只在视口收到悬停事件后才更新。样式表撤掉 item:hover 之后，
 *          WA_Hover 与鼠标跟踪不会再被 QStyleSheetStyle 打开，所以先钉住视口的这两个属性，
 *          再手工把 HoverEnter 送到视口：QAbstractItemView 在 HoverEnter 上就会落下悬停项，
 *          一条事件即是最短可靠序列，不必先 Enter 再 Move。
 *          取样取项内靠右的一格：文字靠左，那里不会落到笔画上。
 *          最后把悬停移到选中行：选中底必须压过悬停底。
 */
TEST_F(CornerControlsFixture, ListWidgetItemShowsHoverAndSelectionWins)
{
    GyanisScribe::Gui::ListWidget list;
    list.resize(200, 120);
    list.addItem(QStringLiteral("第一项"));
    list.addItem(QStringLiteral("第二项"));
    list.setCurrentRow(0);
    list.show();
    QCoreApplication::processEvents();

    ASSERT_TRUE(list.viewport()->testAttribute(Qt::WA_Hover)) << "视口没开 WA_Hover，真实鼠标移动不会产生悬停事件";
    EXPECT_TRUE(list.viewport()->hasMouseTracking()) << "视口没开鼠标跟踪";

    const QRect hoveredItem = list.visualItemRect(list.item(1));
    ASSERT_FALSE(hoveredItem.isEmpty());
    const QPoint hoverProbe{hoveredItem.right() - 10, hoveredItem.center().y()};
    QHoverEvent  hoverEnter{QEvent::HoverEnter, QPointF{hoverProbe}, list.viewport()->mapToGlobal(hoverProbe), QPointF{hoverProbe}};
    QCoreApplication::sendEvent(list.viewport(), &hoverEnter);
    QCoreApplication::processEvents();

    QImage viewportImage = list.viewport()->grab().toImage();
    EXPECT_EQ(viewportImage.pixelColor(hoverProbe), GyanisScribe::Gui::activePalette().hover) << "悬停项没铺 hover 底";

    const QRect selectedItem = list.visualItemRect(list.item(0));
    ASSERT_FALSE(selectedItem.isEmpty());
    const QPoint selectedProbe{selectedItem.right() - 10, selectedItem.center().y()};
    QHoverEvent  hoverMove{QEvent::HoverMove, QPointF{selectedProbe}, list.viewport()->mapToGlobal(selectedProbe), QPointF{hoverProbe}};
    QCoreApplication::sendEvent(list.viewport(), &hoverMove);
    QCoreApplication::processEvents();

    viewportImage = list.viewport()->grab().toImage();
    EXPECT_EQ(viewportImage.pixelColor(selectedProbe), GyanisScribe::Gui::activePalette().accentSoft) << "悬停压过了选中底";
}

/**
 * @brief 钉住关掉容器底的列表完全透明
 * @details 导航压在别的面板上，setSurfaceVisible(false) 之后列表自己不铺 surface；
 *          宿主铺红底，列表中心与左上角都必须露出宿主色，说明容器底真的没画。
 *          列表里只放两项且不选中，项底与文字都落不进这两个取样点。
 */
TEST_F(CornerControlsFixture, ListWidgetWithoutSurfaceLeavesTheHostVisible)
{
    QWidget host;
    host.resize(220, 140);
    host.setAutoFillBackground(true);
    host.setPalette(QPalette{QColor{255, 0, 0}});

    auto *list = new GyanisScribe::Gui::ListWidget{&host};
    list->resize(200, 120);
    list->move(10, 10);
    list->addItem(QStringLiteral("第一项"));
    list->addItem(QStringLiteral("第二项"));
    list->setSurfaceVisible(false);
    host.show();
    QCoreApplication::processEvents();

    const QImage image = host.grab().toImage();
    EXPECT_EQ(image.pixelColor(110, 100), QColor(255, 0, 0)) << "关掉容器底后列表中心应露出宿主底色";
    EXPECT_EQ(image.pixelColor(10, 10), QColor(255, 0, 0)) << "关掉容器底后列表左上角应露出宿主底色";
}

/**
 * @brief 钉住高对比度档选中项的主色描边
 * @details 这一档不靠底色分层：选中项在 accentSoft 底上再描一圈 2px 主色边。
 *          边取项内左沿第二列，底取项内靠右一格避开文字；切回浅色档后同一格必须是 accentSoft，
 *          说明描边只出现在高对比度档，其余档只铺底。
 */
TEST_F(CornerControlsFixture, HighContrastSelectedItemKeepsTheAccentOutline)
{
    GyanisScribe::Gui::setTextThemeKind(GyanisScribe::Gui::ThemeKind::HighContrast);
    const auto &palette = GyanisScribe::Gui::activePalette();

    GyanisScribe::Gui::ListWidget list;
    list.resize(200, 120);
    list.addItem(QStringLiteral("第一项"));
    list.addItem(QStringLiteral("第二项"));
    list.setCurrentRow(0);
    list.show();
    QCoreApplication::processEvents();

    const QRect item = list.visualItemRect(list.item(0));
    ASSERT_FALSE(item.isEmpty());
    const int edgeY = item.center().y();
    QImage    image = list.viewport()->grab().toImage();
    EXPECT_EQ(image.pixelColor(item.left() + 1, edgeY), palette.accent) << "高对比度档的选中项没有 2px 主色边";
    EXPECT_EQ(image.pixelColor(item.right() - 10, edgeY), palette.accentSoft) << "选中底没铺 accentSoft";

    GyanisScribe::Gui::setTextThemeKind(GyanisScribe::Gui::ThemeKind::Light);
    list.viewport()->update();
    QCoreApplication::processEvents();
    image = list.viewport()->grab().toImage();
    EXPECT_EQ(image.pixelColor(item.left() + 1, edgeY), GyanisScribe::Gui::activePalette().accentSoft) << "浅色档的选中项不该有描边";
}

/**
 * @brief 钉住输入类控件的连续曲率填充
 * @details 宿主铺红底：控件中心必须是 controlFill，左上角必须露出宿主色，
 *          后者说明方角没有填满——圆角路径画在控件自己的矩形里。
 */
TEST_F(CornerControlsFixture, LineEditPaintsControlFill)
{
    QWidget host;
    host.resize(220, 60);
    host.setAutoFillBackground(true);
    host.setPalette(QPalette{QColor{255, 0, 0}});
    auto *edit = new GyanisScribe::Gui::LineEdit{&host};
    edit->setGeometry(10, 10, 200, 32);
    host.show();
    QCoreApplication::processEvents();
    const QImage image = host.grab().toImage();
    EXPECT_EQ(image.pixelColor(120, 26), GyanisScribe::Gui::activePalette().controlFill);
    EXPECT_EQ(image.pixelColor(10, 10), (QColor{255, 0, 0}));
}

TEST_F(CornerControlsFixture, ComboBoxPaintsControlFill)
{
    QWidget host;
    host.resize(220, 60);
    host.setAutoFillBackground(true);
    host.setPalette(QPalette{QColor{255, 0, 0}});
    auto *combo = new GyanisScribe::Gui::ComboBox{&host};
    combo->addItem(QStringLiteral("一"));
    combo->setGeometry(10, 10, 200, 32);
    host.show();
    QCoreApplication::processEvents();
    const QImage image = host.grab().toImage();
    EXPECT_EQ(image.pixelColor(120, 26), GyanisScribe::Gui::activePalette().controlFill);
    EXPECT_EQ(image.pixelColor(10, 10), (QColor{255, 0, 0}));
}

/**
 * @brief 钉住下拉弹层用的是连续曲率视图
 * @details 收起态由 paintEvent 自绘，弹层却由 QComboBox 在内部容器里显示；
 *          setView 之后弹层视图必须是 Gui::ListView，视口的悬停属性与鼠标跟踪也要打开，
 *          否则 ItemDelegate 的 State_MouseOver 分支永远不跑；省略模式保持 Qt 默认弹层的中间省略。
 */
TEST_F(CornerControlsFixture, ComboBoxPopupsUseTheSquircleListView)
{
    GyanisScribe::Gui::ComboBox combo;
    combo.addItem(QStringLiteral("一"));
    auto *comboView = dynamic_cast<GyanisScribe::Gui::ListView *>(combo.view());
    ASSERT_NE(comboView, nullptr) << "下拉框的弹层没用 Gui::ListView";
    EXPECT_TRUE(comboView->viewport()->testAttribute(Qt::WA_Hover)) << "弹层视口没开 WA_Hover";
    EXPECT_TRUE(comboView->viewport()->hasMouseTracking()) << "弹层视口没开鼠标跟踪";
    EXPECT_TRUE(comboView->window()->testAttribute(Qt::WA_TranslucentBackground)) << "弹层窗口没开透明背景，圆角外会露出方底";
    EXPECT_TRUE(comboView->window()->testAttribute(Qt::WA_NoSystemBackground)) << "弹层窗口还在铺系统底色";
    EXPECT_EQ(comboView->textElideMode(), Qt::ElideMiddle) << "弹层视图的省略模式偏离了 Qt 默认弹层的中间省略";

    GyanisScribe::Gui::FontComboBox fontCombo;
    auto *fontView = dynamic_cast<GyanisScribe::Gui::ListView *>(fontCombo.view());
    ASSERT_NE(fontView, nullptr) << "字体下拉框的弹层没用 Gui::ListView";
    EXPECT_TRUE(fontView->viewport()->testAttribute(Qt::WA_Hover)) << "字体弹层视口没开 WA_Hover";
    EXPECT_TRUE(fontView->window()->testAttribute(Qt::WA_TranslucentBackground)) << "字体弹层窗口没开透明背景";
}

/**
 * @brief 钉住弹层视图自己铺容器底并让出圆角
 * @details 样式表已删掉 QComboBox QAbstractItemView 的 background 与 border-radius，
 *          容器底只能由 ListView 在 event() 里画；中心取两行之下的一格必须是 surface，
 *          左上角取自身抓图必须透明，方角没有把整块矩形填满。
 */
TEST_F(CornerControlsFixture, ListViewPaintsSurfaceAndLeavesCornersToThePopup)
{
    QStringListModel model{QStringList{QStringLiteral("第一项"), QStringLiteral("第二项")}};
    GyanisScribe::Gui::ListView view;
    view.resize(200, 120);
    view.setModel(&model);
    view.show();
    QCoreApplication::processEvents();

    const QImage image = view.grab().toImage();
    EXPECT_EQ(image.pixelColor(100, 100), GyanisScribe::Gui::activePalette().surface) << "弹层视图没铺 surface";
    EXPECT_EQ(image.pixelColor(0, 0).alpha(), 0) << "弹层视图左上角应透明";
}

/**
 * @brief 钉住三档样式表下的弹层视图仍是自绘底
 * @details 其余弹层用例都没有挂应用样式表；三份 QSS 的 QComboBox QAbstractItemView 规则一旦补回
 *          background 或 border-radius，方底会压在自绘圆角上而用例仍全绿。
 *          弹层不打开时视图不可见、抓不到像素，这里在组合框下挂一个同源的 ListView 探针，
 *          让它走同一条 QComboBox QAbstractItemView 规则：中心必须是这一档的 surface，
 *          左上角必须透明；收尾把样式表与档位还原，别把状态留给后面的用例。
 */
TEST_F(CornerControlsFixture, ThemedPopupViewsKeepTheSquircleSurface)
{
    struct StyleSheetGuard
    {
        QString previous = qApp->styleSheet();

        ~StyleSheetGuard()
        {
            qApp->setStyleSheet(previous);
        }
    } styleSheetGuard;
    struct ThemeKindGuard
    {
        GyanisScribe::Gui::ThemeKind previous = GyanisScribe::Gui::activeThemeKind();

        ~ThemeKindGuard()
        {
            GyanisScribe::Gui::setTextThemeKind(previous);
        }
    } themeKindGuard;

    for (const auto kind: {GyanisScribe::Gui::ThemeKind::Light, GyanisScribe::Gui::ThemeKind::Dark, GyanisScribe::Gui::ThemeKind::HighContrast})
    {
        qApp->setStyleSheet(GyanisScribe::Gui::ThemeManager::styleSheetFor(kind));
        GyanisScribe::Gui::setTextThemeKind(kind);

        GyanisScribe::Gui::ComboBox combo;
        combo.resize(240, 160);
        combo.show();
        // 弹层要等打开才可见，抓不到离屏像素；这里用同源的 ListView 挂在组合框下，
        // 走同一条 QComboBox QAbstractItemView 规则，再按普通控件抓图
        auto *view = new GyanisScribe::Gui::ListView{&combo};
        view->setGeometry(20, 20, 200, 120);
        view->show();
        QCoreApplication::processEvents();
        const QImage image = view->grab().toImage();
        ASSERT_FALSE(image.isNull());
        EXPECT_EQ(image.pixelColor(100, 100), GyanisScribe::Gui::paletteFor(kind).surface)
                << static_cast<int>(kind) << " 档下弹层视图没铺该档 surface；中心取色 " << image.pixelColor(100, 100).name(QColor::HexArgb).toStdString();
        EXPECT_EQ(image.pixelColor(0, 0).alpha(), 0) << static_cast<int>(kind) << " 档下弹层视图左上角不透明，方角底又回来了；左上角取色 "
                                                     << image.pixelColor(0, 0).name(QColor::HexArgb).toStdString();
    }
}

/**
 * @brief 钉住 showPopup 只兜底核对弹层委托
 * @details Qt 不在 showPopup 里重装委托，ListView 构造时装的 ItemDelegate 会一直留着；
 *          这里先在视图上放一个普通委托当替身，模拟委托被换掉的情形，走一次 showPopup() 后必须补回 ItemDelegate。
 *          第二次打开只能做类型核对，不得再新建委托，否则每次打开都会多出一个。
 *          弹层在无交互桌面的测试进程里不会真的显示，所以委托判据只核类型、不核窗口可见性；
 *          容器左上角取自身渲染结果的 alpha，确认顶层窗口没在圆角外铺一块方底。
 */
TEST_F(CornerControlsFixture, ComboBoxReplacesThePopupDelegateAfterShowPopup)
{
    GyanisScribe::Gui::ComboBox combo;
    combo.addItem(QStringLiteral("第一项"));
    combo.addItem(QStringLiteral("第二项"));
    combo.resize(200, 32);
    combo.show();
    QCoreApplication::processEvents();

    auto *standIn = new QStyledItemDelegate{combo.view()};
    combo.view()->setItemDelegate(standIn);
    ASSERT_EQ(combo.view()->itemDelegate(), standIn);

    combo.showPopup();
    auto *installed = combo.view()->itemDelegate();
    ASSERT_NE(qobject_cast<GyanisScribe::Gui::ItemDelegate *>(installed), nullptr) << "弹层委托没有在 showPopup 之后补回 ItemDelegate";

    const QImage popupImage = combo.view()->window()->grab().toImage();
    EXPECT_EQ(popupImage.pixelColor(0, 0).alpha(), 0) << "弹层容器左上角应透明，不能露出方形窗口底";

    combo.hidePopup();
    combo.showPopup();
    EXPECT_EQ(combo.view()->itemDelegate(), installed) << "第二次打开又分配了一个委托";
    combo.hidePopup();
}

/**
 * @brief 钉住字体下拉弹层保留按族名预览的委托
 * @details 字体下拉框的视图在构造与 showPopup 之后都必须装 FontItemDelegate，
 *          普通下拉框仍用 ItemDelegate。离屏绘制一条族名项，委托准备出的字体族
 *          必须等于该项文本，否则弹层里的字体名会全部退回界面字体。
 */
TEST_F(CornerControlsFixture, FontComboPopupKeepsTheFamilyPreviewDelegate)
{
    GyanisScribe::Gui::FontComboBox fontCombo;
    EXPECT_NE(qobject_cast<GyanisScribe::Gui::FontItemDelegate *>(fontCombo.view()->itemDelegate()), nullptr) << "字体下拉框构造后没有装 FontItemDelegate";

    fontCombo.showPopup();
    EXPECT_NE(qobject_cast<GyanisScribe::Gui::FontItemDelegate *>(fontCombo.view()->itemDelegate()), nullptr) << "showPopup 之后字体预览委托被换掉了";
    fontCombo.hidePopup();

    GyanisScribe::Gui::ComboBox combo;
    combo.addItem(QStringLiteral("一"));
    EXPECT_EQ(qobject_cast<GyanisScribe::Gui::FontItemDelegate *>(combo.view()->itemDelegate()), nullptr) << "普通下拉框不该用字体预览委托";
    EXPECT_NE(dynamic_cast<GyanisScribe::Gui::ItemDelegate *>(combo.view()->itemDelegate()), nullptr) << "普通下拉框的弹层委托没走 ItemDelegate";

    const QString family = QStringLiteral("Courier New");
    ASSERT_NE(QFont{family}.family(), QApplication::font().family()) << "判据前提没构造出来：项族名与界面字体相同";

    QStringListModel          model{QStringList{family}};
    RecordingFontItemDelegate delegate;
    QImage                    image{120, 28, QImage::Format_ARGB32_Premultiplied};
    image.fill(Qt::transparent);
    QStyleOptionViewItem option;
    option.rect  = QRect{0, 0, 120, 28};
    option.state = QStyle::State_Enabled;
    {
        QPainter painter{&image};
        delegate.paint(&painter, option, model.index(0, 0));
    }
    EXPECT_EQ(delegate.preparedFamily, family) << "委托准备出的绘制字体还是界面字体，没有换成项自己的族名";
}

TEST_F(CornerControlsFixture, TextEditPaintsControlFill)
{
    QWidget host;
    host.resize(220, 120);
    host.setAutoFillBackground(true);
    host.setPalette(QPalette{QColor{255, 0, 0}});
    auto *edit = new GyanisScribe::Gui::TextEdit{&host};
    edit->setGeometry(10, 10, 200, 100);
    host.show();
    QCoreApplication::processEvents();
    const QImage image = host.grab().toImage();
    EXPECT_EQ(image.pixelColor(120, 60), GyanisScribe::Gui::activePalette().controlFill);
    EXPECT_EQ(image.pixelColor(10, 10), (QColor{255, 0, 0}));
}

TEST_F(CornerControlsFixture, SpinBoxPaintsControlFill)
{
    QWidget host;
    host.resize(220, 60);
    host.setAutoFillBackground(true);
    host.setPalette(QPalette{QColor{255, 0, 0}});
    auto *spin = new GyanisScribe::Gui::SpinBox{&host};
    spin->setGeometry(10, 10, 200, 32);
    host.show();
    QCoreApplication::processEvents();
    const QImage image = host.grab().toImage();
    EXPECT_EQ(image.pixelColor(60, 26), GyanisScribe::Gui::activePalette().controlFill);
}

/**
 * @brief 钉住滑块自绘槽与手柄
 * @details 样式表已没有 QSlider 规则，槽、已走过的子页与手柄都由 Gui::Slider 画。
 *          值 50 落在「控件宽减手柄边长」区间的中点，取样两处：手柄中心必须是主色，
 *          手柄右侧必须只剩槽的静默底。只断言「不是全透明」不行——控件底会把整块铺满，
 *          那样不画任何笔也能过。滑块放在宿主里并把焦点留在宿主上，手柄才不会落成
 *          聚焦反转档（底色芯加正文色圈）。
 */
TEST_F(CornerControlsFixture, SliderPaintsGrooveAndHandle)
{
    QWidget host;
    auto   *slider = new GyanisScribe::Gui::Slider{Qt::Horizontal, &host};
    slider->setRange(0, 100);
    slider->setValue(50);
    slider->setGeometry(0, 0, 160, 20);
    host.resize(160, 20);
    host.setFocusPolicy(Qt::StrongFocus);
    host.show();
    host.setFocus();
    QCoreApplication::processEvents();
    ASSERT_FALSE(slider->hasFocus()) << "焦点没留在宿主上，手柄会落成聚焦反转档";
    const QImage image = slider->grab().toImage();
    EXPECT_EQ(image.pixelColor(80, 10), GyanisScribe::Gui::activePalette().accent) << "手柄中心没画成主色";
    EXPECT_EQ(image.pixelColor(150, 10), GyanisScribe::Gui::activePalette().mutedFill) << "手柄右侧的槽没铺静默底";
}

/**
 * @brief 钉住滚动条自绘圆角手柄且轨道透明
 * @details 样式表已没有 QScrollBar 规则，手柄由 Gui::ScrollBar 按细槽档画；
 *          轨道本身不铺底，手柄上端之外的一格必须是透明的。
 */
TEST_F(CornerControlsFixture, ScrollBarPaintsRoundedHandle)
{
    GyanisScribe::Gui::ScrollBar bar{Qt::Vertical};
    bar.setRange(0, 100);
    bar.setPageStep(20);
    bar.resize(10, 120);
    bar.show();
    QCoreApplication::processEvents();
    const QImage image = bar.grab().toImage();
    int          opaque = 0;
    for (int y = 0; y < image.height(); ++y)
    {
        for (int x = 0; x < image.width(); ++x)
        {
            if (image.pixelColor(x, y).alpha() > 0)
            {
                ++opaque;
            }
        }
    }
    EXPECT_GT(opaque, 0);
    EXPECT_EQ(image.pixelColor(5, 0).alpha(), 0) << "轨道上端应透明；实际取色 " << image.pixelColor(5, 0).name(QColor::HexArgb).toStdString();
}

/**
 * @brief 钉住三个自绘滚动区挂的是本类的滚动条
 * @details 样式表不再给 QScrollBar 尺寸与颜色，挂回原生滚动条就没有圆角手柄；
 *          文本区、列表与弹层视图在构造时就要换掉两条滚动条。
 */
TEST_F(CornerControlsFixture, GuiScrollAreasUseTheSquircleScrollBars)
{
    GyanisScribe::Gui::TextEdit   text;
    GyanisScribe::Gui::ListWidget list;
    GyanisScribe::Gui::ListView   view;
    for (auto *area: {static_cast<QAbstractScrollArea *>(&text), static_cast<QAbstractScrollArea *>(&list), static_cast<QAbstractScrollArea *>(&view)})
    {
        EXPECT_NE(qobject_cast<GyanisScribe::Gui::ScrollBar *>(area->verticalScrollBar()), nullptr) << "垂直滚动条还是原生件";
        EXPECT_NE(qobject_cast<GyanisScribe::Gui::ScrollBar *>(area->horizontalScrollBar()), nullptr) << "水平滚动条还是原生件";
    }
}

TEST_F(CornerControlsFixture, HighContrastInputFocusDrawsAccentBorder)
{
    GyanisScribe::Gui::setTextThemeKind(GyanisScribe::Gui::ThemeKind::HighContrast);
    QWidget host;
    host.resize(220, 60);
    host.setAutoFillBackground(true);
    host.setPalette(QPalette{QColor{255, 0, 0}});
    auto *edit = new GyanisScribe::Gui::LineEdit{&host};
    edit->setGeometry(10, 10, 200, 34);
    host.show();
    edit->setFocus(Qt::TabFocusReason);
    QCoreApplication::processEvents();
    const QImage image = host.grab().toImage();
    EXPECT_EQ(image.pixelColor(110, 10), GyanisScribe::Gui::activePalette().accent);
    GyanisScribe::Gui::setTextThemeKind(GyanisScribe::Gui::ThemeKind::Light);
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
