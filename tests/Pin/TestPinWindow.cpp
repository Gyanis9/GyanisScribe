// 贴图窗的交互直测：键盘与双击改的是同一批状态量，判据取公开读数（缩放/角度/透明度），
// 不读私有成员——将来把几何搬到别处，这几条仍然判的是用户看得见的那一件事。
// 单测几何数学的部分住在 TestPinTransform.cpp，重启恢复住在 TestPinLayout.cpp，这里只补窗口本身那一格。

#include "Pin/PinWindow.h"

#include "Annotation/AnnotationView.h"

#include "PinTestAssets.h"

#include "Core/Paths.h"
#include "Core/Types.h"
#include "Platform/IPlatformIntegration.h"
#include "Platform/PlatformCapabilities.h"
#include "Settings/SettingsKeys.h"
#include "Settings/SettingsStore.h"

#include <gtest/gtest.h>

#include <QAction>
#include <QApplication>
#include <QClipboard>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QEnterEvent>
#include <QFileInfo>
#include <QGuiApplication>
#include <QImage>
#include <QKeyEvent>
#include <QMenu>
#include <QMimeData>
#include <QMovie>
#include <QScreen>
#include <QMouseEvent>
#include <QPoint>
#include <QSize>
#include <QWheelEvent>

#include <array>
#include <filesystem>
#include <cmath>
#include <format>
#include <fstream>
#include <memory>
#include <optional>
#include <vector>

// 整份用例 nest 进工程命名空间：匿名命名空间在外面时 `Core::Expected` 这类写法认不到名字
namespace GyanisScribe
{
    namespace
    {
        std::unique_ptr<QApplication> application;

        constexpr int    kImageEdge        = 40;
        constexpr double kScaleTolerance   = 0.001;
        constexpr double kDegreesTolerance = 0.01;
        constexpr double kOpacityTolerance = 0.001;

        /// 造一张不透明的小图：贴图窗只按 devicePixelRatio 折逻辑尺寸，颜色不参与这几条判据
        QImage makeImage()
        {
            QImage image{QSize{kImageEdge, kImageEdge}, QImage::Format_ARGB32};
            image.fill(QColor{200, 60, 60});
            image.setDevicePixelRatio(1.0);
            return image;
        }

        /// 投一条按键事件：判据不依赖「窗口恰好是当前焦点」
        void pressKey(QWidget &target, const Qt::Key key, const Qt::KeyboardModifiers modifiers = Qt::NoModifier)
        {
            QKeyEvent event{QEvent::KeyPress, key, modifiers};
            QApplication::sendEvent(&target, &event);
        }

        /// 投一条左键双击：位置给两份（窗口内与全局），buttons 给 NoButton——与 Qt 自己发双击的形状一致
        void doubleClickLeft(QWidget &target)
        {
            QMouseEvent event{QEvent::MouseButtonDblClick, QPointF{5.0, 5.0}, QPointF{5.0, 5.0}, Qt::LeftButton, Qt::NoButton, Qt::NoModifier};
            QApplication::sendEvent(&target, &event);
        }

        /// 浮点判等：状态量都是 double，容差吃掉最后的位噪声
        [[nodiscard]] bool nearEqual(const double actual, const double expected, const double tolerance)
        {
            return std::abs(actual - expected) <= tolerance;
        }

        class PinWindowFixture : public ::testing::Test
        {
        protected:
            static void SetUpTestSuite()
            {
                // argv 与 argc 的缓冲要有静态存储期：Qt 只存指针不拷贝
                static int   argumentCount  = 1;
                static char  argumentZero[] = "TestPinWindow";
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
                m_iniPath           = std::filesystem::temp_directory_path() / std::format("GyanisScribe-PinWindow-{}.ini", caseName);
                std::error_code ignored;
                std::filesystem::remove(m_iniPath, ignored);

                m_settings    = std::make_unique<Settings::SettingsStore>(m_iniPath);
                m_integration = Platform::createPlatformIntegration();
            }

            void TearDown() override
            {
                m_window.reset();
                m_integration.reset();
                m_settings.reset();
                std::error_code ignored;
                std::filesystem::remove(m_iniPath, ignored);
            }

            /// 开一张贴图：不 show，事件由 sendEvent 直投，判据只看状态量
            [[nodiscard]] Pin::PinWindow &makeWindow()
            {
                return makeWindow(makeImage());
            }

            /// 用指定图片开一张贴图：需要判「取到的是哪一半」这类位置判据时用它
            [[nodiscard]] Pin::PinWindow &makeWindow(const QImage &image)
            {
                m_window = std::make_unique<Pin::PinWindow>(image, *m_settings, *m_integration);
                return *m_window;
            }

            [[nodiscard]] Settings::ISettingsStore &settings() const
            {
                return *m_settings;
            }

        private:
            std::filesystem::path                           m_iniPath;     ///< 本用例专用的设置文件
            std::unique_ptr<Settings::SettingsStore>        m_settings;    ///< 贴图窗要的保存目录来源
            std::unique_ptr<Platform::IPlatformIntegration> m_integration; ///< 平台能力查询（本组用例只读能力表）
            std::unique_ptr<Pin::PinWindow>                 m_window;      ///< 被测窗口
        };

    } // namespace

    /**
     * @brief 钉住 `R` 与 `Shift+R` 各转一挡，且带 Ctrl 的同一个字母不算
     * @details 一挡取 90°：滚轮那 5° 是细调，键盘要的是「把这转正」；反向留给 Shift，
     *          再加一档键位就与「Ctrl+滚轮旋转」那一条抢字面。
     */
    TEST_F(PinWindowFixture, RotateKeysTurnOneQuarterTurnEachWay)
    {
        auto &window = makeWindow();
        ASSERT_TRUE(nearEqual(window.rotationDegrees(), 0.0, kDegreesTolerance));

        pressKey(window, Qt::Key_R);
        EXPECT_TRUE(nearEqual(window.rotationDegrees(), 90.0, kDegreesTolerance)) << window.rotationDegrees();

        pressKey(window, Qt::Key_R);
        EXPECT_TRUE(nearEqual(window.rotationDegrees(), 180.0, kDegreesTolerance)) << "连按两次该累计到半圈";

        pressKey(window, Qt::Key_R, Qt::ShiftModifier);
        EXPECT_TRUE(nearEqual(window.rotationDegrees(), 90.0, kDegreesTolerance)) << "Shift+R 没往回转";

        // 带 Ctrl 的 R 不归贴图窗管：状态不该动
        pressKey(window, Qt::Key_R, Qt::ControlModifier);
        EXPECT_TRUE(nearEqual(window.rotationDegrees(), 90.0, kDegreesTolerance)) << "Ctrl+R 把角度改走了";
    }

    /**
     * @brief 钉住角度折进 [0,360)：从 0 逆时针一挡要落到 270 而不是 -90
     * @details 角度这一格会被序列化进恢复布局，负值写进去下次读回来就是「形状对不上」。
     */
    TEST_F(PinWindowFixture, NegativeTurnFoldsIntoTheDocumentedRange)
    {
        auto &window = makeWindow();

        pressKey(window, Qt::Key_R, Qt::ShiftModifier);
        EXPECT_TRUE(nearEqual(window.rotationDegrees(), 270.0, kDegreesTolerance)) << window.rotationDegrees();
    }

    /**
     * @brief 钉住 `0` 与双击回到 1:1 与 0°，而透明度不受影响
     * @details 透明度是用户刻意调出来的一档，跟着尺寸一起抹掉就是替他做了没要过的事；
     *          两处入口（键盘与双击）必须落进同一个出口，否则将来只会改一处。
     */
    TEST_F(PinWindowFixture, ZeroKeyAndDoubleClickRestoreScaleAndAngleButNotOpacity)
    {
        auto &window = makeWindow();
        window.setScale(2.0);
        window.setRotationDegrees(45.0);
        window.setOpacityLevel(0.5);

        pressKey(window, Qt::Key_0);
        EXPECT_TRUE(nearEqual(window.scale(), 1.0, kScaleTolerance)) << "按 0 没回到 1:1";
        EXPECT_TRUE(nearEqual(window.rotationDegrees(), 0.0, kDegreesTolerance)) << "按 0 没把角度转正";
        EXPECT_TRUE(nearEqual(window.opacityLevel(), 0.5, kOpacityTolerance)) << "复位把用户调过的透明度一起抹了";

        window.setScale(3.0);
        window.setRotationDegrees(90.0);

        doubleClickLeft(window);
        EXPECT_TRUE(nearEqual(window.scale(), 1.0, kScaleTolerance)) << "双击没回到 1:1";
        EXPECT_TRUE(nearEqual(window.rotationDegrees(), 0.0, kDegreesTolerance)) << "双击没把角度转正";
        EXPECT_TRUE(nearEqual(window.opacityLevel(), 0.5, kOpacityTolerance)) << "双击同样不该动透明度";
    }

    /**
     * @brief 钉住右键双击不改任何状态
     * @details 右键是菜单的地盘：双击弹菜单之外还顺手把图放大，用户关不掉菜单就发现图变了。
     */
    TEST_F(PinWindowFixture, RightButtonDoubleClickChangesNothing)
    {
        auto &window = makeWindow();
        window.setScale(2.0);

        QMouseEvent event{QEvent::MouseButtonDblClick, QPointF{5.0, 5.0}, QPointF{5.0, 5.0}, Qt::RightButton, Qt::NoButton, Qt::NoModifier};
        QApplication::sendEvent(&window, &event);

        EXPECT_TRUE(nearEqual(window.scale(), 2.0, kScaleTolerance)) << "右键双击把缩放改掉了";
    }

    /**
     * @brief 钉住 Esc 仍然关闭本张贴图并通知管理器
     * @details 加了两个新键位之后，这条旧行为必须原样在：它同时是「管理器把自己那一份摘掉」的信号源。
     */
    TEST_F(PinWindowFixture, EscapeStillClosesAndNotifies)
    {
        auto &window = makeWindow();

        int closedSignals = 0;
        QObject::connect(&window, &Pin::PinWindow::closedByUser, &window, [&closedSignals] { ++closedSignals; });

        pressKey(window, Qt::Key_Escape);
        EXPECT_EQ(closedSignals, 1) << "Esc 没通知管理器，那一张贴图会一直留在册上";
        EXPECT_FALSE(window.isVisibleTo(&window)) << "Esc 之后窗口还在";
    }

    /**
     * @brief 钉住没绑定的字母不改任何状态
     * @details 拒绝面：键盘处理只认自己那三个键位，别的都要原样交回 Qt，
     *          否则输入法与系统快捷键会打到贴图窗身上。
     */
    TEST_F(PinWindowFixture, UnboundKeysLeaveThePinUntouched)
    {
        auto &window = makeWindow();
        window.setScale(2.0);
        window.setRotationDegrees(30.0);

        pressKey(window, Qt::Key_K);
        pressKey(window, Qt::Key_1);
        pressKey(window, Qt::Key_0, Qt::ShiftModifier);

        EXPECT_TRUE(nearEqual(window.scale(), 2.0, kScaleTolerance)) << "没绑的键改动了缩放";
        EXPECT_TRUE(nearEqual(window.rotationDegrees(), 30.0, kDegreesTolerance)) << "没绑的键改动了角度";
    }


    /**
     * @brief 钉住方向键把贴图逐像素挪走：裸按一格、按住 Shift 十格，带 Ctrl 的不认
     * @details 遮罩与标注窗早就是这个口径，贴图这一头以前只能拿鼠标拖，贴着别的窗对边线时
     *          差的那几格摸不出来。带 Ctrl 的方向键留给别的用途（行内文字框按词移光标那一类），
     *          这一档刻意不绑。
     */
    TEST_F(PinWindowFixture, ArrowKeysNudgeThePinByOneAndTenPixels)
    {
        auto &window = makeWindow();
        window.move(QPoint{300, 200});

        pressKey(window, Qt::Key_Left);
        const QPoint oneLeft{299, 200};
        EXPECT_EQ(window.pos(), oneLeft) << "裸按方向键该挪一个逻辑像素";
        pressKey(window, Qt::Key_Down);
        const QPoint oneDown{299, 201};
        EXPECT_EQ(window.pos(), oneDown) << "向下那一格同样是一像素";
        pressKey(window, Qt::Key_Right, Qt::ShiftModifier);
        const QPoint tenRight{309, 201};
        EXPECT_EQ(window.pos(), tenRight) << "按住 Shift 该走十格（与标注窗那档粗调同值）";
        pressKey(window, Qt::Key_Up, Qt::ControlModifier);
        EXPECT_EQ(window.pos(), tenRight) << "带 Ctrl 的方向键没绑，贴图不该动";
    }

    /**
     * @brief 钉住 `Ctrl+V` 只报「这一张要换内容」，而裸按 `V` 仍是上下翻转
     * @details 窗口不碰剪贴板：内容转换归界面层、「动图不换」那道拒绝归管理器，
     *          所以这里判的是上报次数——一次按键只该有一次，且不能顺手把翻转也带上。
     */
    TEST_F(PinWindowFixture, ControlVAsksToReplaceContentWhileBareVFlips)
    {
        auto      &window   = makeWindow();
        int        requests = 0;
        const auto hook     = QObject::connect(&window, &Pin::PinWindow::clipboardReplaceRequested, [&requests] { ++requests; });
        ASSERT_TRUE(hook) << "信号没接上，后面的计数就毫无意义";

        pressKey(window, Qt::Key_V, Qt::ControlModifier);
        EXPECT_EQ(requests, 1) << "Ctrl+V 该报一次「要换内容」";

        const auto mirroredBefore = window.mirroredVertically();
        pressKey(window, Qt::Key_V);
        EXPECT_EQ(requests, 1) << "裸按 V 不该被当成替换";
        EXPECT_NE(window.mirroredVertically(), mirroredBefore) << "裸按 V 走的还是上下翻转那一条";
    }

    /**
     * @brief 钉住右键「平滑缩放」默认勾着，点一下换档，且勾选态与读数同解
     */
    TEST_F(PinWindowFixture, SmoothScaleMenuItemIsCheckedByDefaultAndToggles)
    {
        auto &window = makeWindow();
        EXPECT_TRUE(window.smoothScaleEnabled()) << "平滑缩放默认该开着（与 Qt 的绘图缺省档一致）";

        auto    *menu   = window.buildContextMenu();
        QAction *action = nullptr;
        for (auto *candidate: menu->actions())
        {
            if (candidate->text() == QString::fromUtf8("平滑缩放"))
            {
                action = candidate;
                break;
            }
        }
        ASSERT_NE(action, nullptr) << "右键菜单里没有「平滑缩放」这一项";
        EXPECT_TRUE(action->isChecked()) << "菜单勾选态与该档的当前读数不同解";
        action->trigger();
        EXPECT_FALSE(window.smoothScaleEnabled()) << "点一下该把它关掉";
        delete menu;
    }

    /**
     * @brief 钉住关掉平滑缩放后，放大画出来的确实不是同一份像素
     * @details 判据用两次取图的对照而不是某个采样点：插值与最近邻在放大后的图上必然给出不同的铺法。
     *          若绘制那一步没读这一档，两张图会一模一样——那正是这条用例要打的缺陷形状。
     */
    TEST_F(PinWindowFixture, SmoothScaleOffPaintsDifferentPixelsThanOn)
    {
        QImage image{QSize{4, 4}, QImage::Format_ARGB32};
        image.setDevicePixelRatio(1.0);
        for (auto y = 0; y < 4; ++y)
        {
            for (auto x = 0; x < 4; ++x)
            {
                image.setPixelColor(x, y, QColor{x * 80, y * 80, 40});
            }
        }

        auto &window = makeWindow(image);
        window.setScale(3.0);
        const auto smoothed = window.grab().toImage();
        window.setSmoothScaleEnabled(false);
        const auto nearest = window.grab().toImage();

        ASSERT_EQ(smoothed.size(), nearest.size()) << "换档不该改这张贴图的大小";
        const bool identical = smoothed == nearest;
        EXPECT_FALSE(identical) << "关掉平滑缩放后画出来的与开着的一模一样：这一档没进到绘制里";
    }

    /**
     * @brief 钉住缩略图模式：缩成小方块、缩放与旋转原样留着、退出回原样
     * @details 缩略图只是「先让开地方」：它不进恢复布局，也不改用户调好的缩放与旋转。
     */
    TEST_F(PinWindowFixture, ThumbnailModeShrinksTheWindowAndRestoresIt)
    {
        auto &window = makeWindow();
        window.setScale(3.0);
        window.setRotationDegrees(30.0);
        const auto originalSize = window.size();
        ASSERT_GT(originalSize.width(), 100) << "前提不对：原尺寸得明显大于小方块，不然判不出「缩了」";

        window.setThumbnailMode(true);
        EXPECT_TRUE(window.isThumbnailMode());
        EXPECT_NEAR(window.width(), 48, 1) << "缩略图该是那枚固定的小方块";
        EXPECT_NEAR(window.height(), 48, 1);
        EXPECT_TRUE(nearEqual(window.scale(), 3.0, kScaleTolerance)) << "缩略图不该动缩放";
        EXPECT_TRUE(nearEqual(window.rotationDegrees(), 30.0, kDegreesTolerance)) << "缩略图不该动旋转";

        window.setThumbnailMode(false);
        EXPECT_FALSE(window.isThumbnailMode());
        EXPECT_EQ(window.size(), originalSize) << "退出缩略图要回到原来的大小";
    }

    /**
     * @brief 钉住裸按 T 切换缩略图，带修饰键的不认
     */
    TEST_F(PinWindowFixture, ThumbnailKeyTogglesAndIgnoresModifiers)
    {
        auto &window = makeWindow();

        pressKey(window, Qt::Key_T);
        EXPECT_TRUE(window.isThumbnailMode()) << "裸按 T 该缩到小图";
        pressKey(window, Qt::Key_T);
        EXPECT_FALSE(window.isThumbnailMode()) << "再按一次该还原";

        pressKey(window, Qt::Key_T, Qt::ControlModifier);
        EXPECT_FALSE(window.isThumbnailMode()) << "带修饰键的 T 不归贴图窗管";
    }

    /**
     * @brief 钉住布局记录里带着分组号，越界的组号夹回第一组
     * @details 分组号不进记录的话，重启后所有贴图挤回第一组，用户摆好的「哪几张算一摊」就没了。
     */
    TEST_F(PinWindowFixture, StateCarriesItsGroup)
    {
        auto &window = makeWindow();
        window.setGroup(3);
        EXPECT_EQ(window.state().group, 3) << "分组号没进布局记录";

        window.setGroup(0);
        EXPECT_EQ(window.state().group, 1) << "0 或负数组号要被夹到第一组，否则布局里会出现一个不存在的组";
    }

    /**
     * @brief 钉住右键菜单的「移动到分组」：列全推来的分组、当前组带勾不可点、末项按最大组号新建
     * @details 菜单项的动作都握在 lambda 里，不把菜单借出来核对，这层接线就只能靠人读代码：
     *          新建那一项算错组号（比如按「现有组数 +1」数）会把两张贴图并进同一组，
     *          用户以为新建了，却始终有一张看不见。
     */
    TEST_F(PinWindowFixture, GroupSubmenuListsChoicesAndRequestsTheChosenGroup)
    {
        auto &window = makeWindow();
        window.setGroup(2);
        window.setGroupChoices({1, 2, 4});

        std::vector<int> requested;
        QObject::connect(&window, &Pin::PinWindow::moveToGroupRequested, &window, [&requested](const int group) { requested.push_back(group); });

        const std::unique_ptr<QMenu> menu{window.buildContextMenu()};
        QMenu                       *groupMenu = nullptr;
        for (QAction *action: menu->actions())
        {
            if (action->menu() != nullptr && action->text() == QString::fromUtf8("移动到分组"))
            {
                groupMenu = action->menu();
            }
        }
        ASSERT_NE(groupMenu, nullptr) << "右键菜单里没有「移动到分组」这一项";

        const auto entries = groupMenu->actions();
        // 三项现有分组 + 一条分隔线 + 新建分组
        ASSERT_EQ(entries.size(), 5) << "分组子菜单的条目数与推过来的分组清单不同解";
        EXPECT_EQ(entries[0]->text(), QString::fromUtf8("分组 1"));
        EXPECT_EQ(entries[1]->text(), QString::fromUtf8("分组 2"));
        EXPECT_EQ(entries[2]->text(), QString::fromUtf8("分组 4"));
        EXPECT_TRUE(entries[1]->isChecked()) << "当前所在的那一组没有带勾";
        EXPECT_FALSE(entries[1]->isEnabled()) << "当前这一组还点得动：按下去什么都不会发生";
        EXPECT_FALSE(entries[0]->isChecked()) << "别的分组也带上了勾，看不出自己在哪一组";

        entries[0]->trigger();
        ASSERT_EQ(requested.size(), 1);
        EXPECT_EQ(requested.front(), 1) << "点「分组 1」要的是 1 号组";

        entries.back()->trigger();
        ASSERT_EQ(requested.size(), 2);
        EXPECT_EQ(requested.back(), 5) << "新建分组该按现有最大组号 +1，而不是按现有组数重数";
    }

    /**
     * @brief 钉住取色模式：菜单进模式、悬停出读数、点一下交出一个像素的颜色、Esc 先退出模式
     * @details 取色判据要能分辨「取到的是哪一格」：用左右两半不同色的图，并在 2 倍缩放下点两处，
     *          换算错一格（比如把窗口坐标当成像素坐标）就会取到隔壁那一半的颜色。
     */
    TEST_F(PinWindowFixture, ColorPickReportsThePixelUnderTheCursor)
    {
        // 左右两半不同色的图：取到哪一半立刻判得出来
        QImage twoTone{kImageEdge * 2, kImageEdge, QImage::Format_ARGB32};
        twoTone.fill(QColor{255, 0, 0});
        for (int y = 0; y < twoTone.height(); ++y)
        {
            for (int x = kImageEdge; x < twoTone.width(); ++x)
            {
                twoTone.setPixelColor(x, y, QColor{0, 0, 255});
            }
        }

        auto &window = makeWindow(twoTone);
        window.setScale(2.0);

        std::vector<QColor> picked;
        QObject::connect(&window, &Pin::PinWindow::colorPicked, &window, [&picked](const QColor &color) { picked.push_back(color); });

        // 菜单与它的动作同生共死：菜单一析构，动作就跟着没了（动作归菜单持有），
        // 所以这里把菜单留在手上，只借用里面那一条
        const auto pickActionIn = [](QMenu &menu) -> QAction *
        {
            for (QAction *action: menu.actions())
            {
                if (action->text() == QString::fromUtf8("取色"))
                {
                    return action;
                }
            }
            return nullptr;
        };

        // 悬停：读数跟着光标走，文本按设置里那一档写法
        ASSERT_TRUE(settings().setValue(GyanisScribe::Settings::kPickFormat, QVariant(QStringLiteral("Rgb"))).has_value());
        auto  menu       = std::unique_ptr<QMenu>{window.buildContextMenu()};
        auto *pickAction = pickActionIn(*menu);
        ASSERT_NE(pickAction, nullptr) << "右键菜单里没有「取色」这一项";
        pickAction->trigger();
        ASSERT_TRUE(window.isColorPickMode()) << "点了菜单项没进取色模式";

        // 图片 80×40、2 倍缩放 → 画布矩形正好铺满窗口；像素 (10,20) 的中心落在窗口 (21,41)
        QMouseEvent hover{QEvent::MouseMove, QPointF{21.0, 41.0}, QPointF{21.0, 41.0}, Qt::NoButton, Qt::NoButton, Qt::NoModifier};
        QApplication::sendEvent(&window, &hover);
        const auto readout = window.hoveredColorText();
        ASSERT_TRUE(readout.has_value()) << "光标在图上却没有读数";
        EXPECT_EQ(*readout, QStringLiteral("rgb(255, 0, 0)")) << "读数取到的不是光标下那一格（或没按设置里的写法排）";

        // 点一下：把这一格交出去，并自己退出模式（与屏幕取色「点一下拿一个色」同一口径）
        QMouseEvent press{QEvent::MouseButtonPress, QPointF{21.0, 41.0}, QPointF{21.0, 41.0}, Qt::LeftButton, Qt::LeftButton, Qt::NoModifier};
        QApplication::sendEvent(&window, &press);
        ASSERT_EQ(picked.size(), 1);
        EXPECT_EQ(picked.front(), QColor(255, 0, 0)) << "点左半取到的不是红";
        EXPECT_FALSE(window.isColorPickMode()) << "取完一个色该退出模式";

        // 再进一次，点右半：缩放换算错了这里会取到红
        menu       = std::unique_ptr<QMenu>{window.buildContextMenu()};
        pickAction = pickActionIn(*menu);
        ASSERT_NE(pickAction, nullptr);
        pickAction->trigger();
        ASSERT_TRUE(window.isColorPickMode());
        QMouseEvent secondPress{QEvent::MouseButtonPress, QPointF{141.0, 41.0}, QPointF{141.0, 41.0}, Qt::LeftButton, Qt::LeftButton, Qt::NoModifier};
        QApplication::sendEvent(&window, &secondPress);
        ASSERT_EQ(picked.size(), 2);
        EXPECT_EQ(picked.back(), QColor(0, 0, 255)) << "点右半取到的不是蓝：窗口坐标没按缩放折回像素格";

        // Esc 先退出取色，而不是把贴图关掉
        menu       = std::unique_ptr<QMenu>{window.buildContextMenu()};
        pickAction = pickActionIn(*menu);
        ASSERT_NE(pickAction, nullptr);
        pickAction->trigger();
        ASSERT_TRUE(window.isColorPickMode());
        int closedSignals = 0;
        QObject::connect(&window, &Pin::PinWindow::closedByUser, &window, [&closedSignals] { ++closedSignals; });
        pressKey(window, Qt::Key_Escape);
        EXPECT_FALSE(window.isColorPickMode()) << "取色模式里 Esc 没退出取色";
        EXPECT_EQ(closedSignals, 0) << "取色模式里按 Esc 把贴图关掉了：这一下多半只是想不取了";
    }

    /**
     * @brief 钉住右键菜单的「保存」真按设置里的目录与命名模板写下一个文件
     * @details 这一条同时是「在文件夹中查看」那一半的守护：两条菜单项共用同一处起名与写盘，
     *          保存坏了它们一起坏。文件落在用例自己的临时目录里，不碰用户真实的图片目录。
     */
    TEST_F(PinWindowFixture, SaveMenuItemWritesIntoTheConfiguredDirectory)
    {
        const auto      target = std::filesystem::temp_directory_path() / std::format("GyanisScribe-PinSave-{}", ::testing::UnitTest::GetInstance()->current_test_info()->name());
        std::error_code ignored;
        std::filesystem::remove_all(target, ignored);
        ASSERT_TRUE(settings().setValue(GyanisScribe::Settings::kPathScreenshot, QVariant(Core::Paths::toDisplayText(target))).has_value());
        ASSERT_TRUE(settings().setValue(GyanisScribe::Settings::kSaveImageFormat, QVariant(QStringLiteral("Png"))).has_value());
        ASSERT_TRUE(settings().setValue(GyanisScribe::Settings::kSaveNameTemplate, QVariant(QStringLiteral("pin_%date%"))).has_value());

        auto                        &window = makeWindow();
        const std::unique_ptr<QMenu> menu{window.buildContextMenu()};
        QAction                     *saveAction = nullptr;
        for (QAction *action: menu->actions())
        {
            if (action->text() == QString::fromUtf8("保存"))
            {
                saveAction = action;
            }
        }
        ASSERT_NE(saveAction, nullptr) << "右键菜单里没有「保存」这一项";
        saveAction->trigger();

        // 文件名按模板起（pin_ 打头、.png 收尾），目录就是设置里那一格
        int files = 0;
        for (const auto &entry: std::filesystem::directory_iterator(target, ignored))
        {
            const auto name = entry.path().filename().string();
            EXPECT_EQ(name.rfind("pin_", 0), 0u) << "文件名没按模板起：" << name;
            EXPECT_EQ(entry.path().extension().string(), ".png") << "后缀没跟着设置里那一档格式：" << name;
            ++files;
        }
        EXPECT_EQ(files, 1) << "保存过后目录里的文件数不对：" << files;

        std::filesystem::remove_all(target, ignored);
    }

    /**
     * @brief 钉住拖着放到屏边会贴上去：松手先露着，指针离开收成一条，悬停再露出来
     * @details 三条判据各打一处：贴边那一步（释放时吸边）、收起来那一步（离开时只剩一条）、
     *          露出来那一步（进入时回到贴齐的位置）。没有这三步，「贴边」只是在边上停着的一张图。
     */
    TEST_F(PinWindowFixture, DroppingAtTheScreenEdgeDocksAndHoverReveals)
    {
        QScreen *screen = QGuiApplication::primaryScreen();
        if (screen == nullptr)
        {
            GTEST_SKIP() << "这个环境没有屏幕可枚举，贴边的几何无从谈起";
        }
        const auto available = screen->availableGeometry();

        auto &window = makeWindow();
        window.move(available.left() + 5, available.top() + 200);
        ASSERT_EQ(window.edgeDock(), Pin::ScreenEdge::None);

        // 拖一下再松手（只点一下不触发吸边，这一条顺带钉住）。
        // 落点取窗口正中：八个手柄的占位框贴着四边，按在边上会成为「拖手柄改大小」而不是「拖窗口」
        const QPointF grip = QRectF(QPointF(0, 0), QSizeF(window.size())).center();
        QMouseEvent   press{QEvent::MouseButtonPress, grip, grip, Qt::LeftButton, Qt::LeftButton, Qt::NoModifier};
        QApplication::sendEvent(&window, &press);
        QMouseEvent drag{QEvent::MouseMove, grip + QPointF{7.0, 7.0}, grip + QPointF{7.0, 7.0}, Qt::NoButton, Qt::LeftButton, Qt::NoModifier};
        QApplication::sendEvent(&window, &drag);
        QMouseEvent release{QEvent::MouseButtonRelease, grip + QPointF{7.0, 7.0}, grip + QPointF{7.0, 7.0}, Qt::LeftButton, Qt::NoButton, Qt::NoModifier};
        QApplication::sendEvent(&window, &release);

        EXPECT_EQ(window.edgeDock(), Pin::ScreenEdge::Left) << "拖到左边附近松手没贴上去";
        EXPECT_EQ(window.x(), available.left()) << "贴上之后没有贴齐那条边";
        const auto revealedWidth = window.width();

        // 指针离开：收成一条
        QEvent leave{QEvent::Leave};
        QApplication::sendEvent(&window, &leave);
        EXPECT_EQ(window.geometry().right(), Pin::kEdgeStripWidth - 1) << "指针走了没收起来，或者收得太多（整张藏进了屏外）";
        EXPECT_EQ(window.width(), revealedWidth) << "收起只挪位置，不改尺寸";

        // 悬停：露回来
        QEnterEvent enter{QPointF{1.0, 1.0}, QPointF{1.0, 1.0}, QPointF{1.0, 1.0}};
        QApplication::sendEvent(&window, &enter);
        EXPECT_EQ(window.x(), available.left()) << "悬停没有露回来：收起的贴图就再也点不到了";
    }

    /**
     * @brief 钉住菜单那一项：点一下就地贴边收起，再点一下摆回屏上；拖离屏边也会自动脱开
     */
    TEST_F(PinWindowFixture, EdgeDockMenuTogglesAndDraggingAwayClearsIt)
    {
        if (QGuiApplication::primaryScreen() == nullptr)
        {
            GTEST_SKIP() << "这个环境没有屏幕可枚举，贴边的几何无从谈起";
        }
        const auto available = QGuiApplication::primaryScreen()->availableGeometry();

        auto &window = makeWindow();
        window.move(available.left() + 5, available.top() + 150);

        const auto dockActionOf = [](QWidget &target)
        {
            // 菜单与它的动作同生共死：借用之后要让菜单活到用完（动作归菜单持有）
            return std::unique_ptr<QMenu>{static_cast<Pin::PinWindow &>(target).buildContextMenu()};
        };
        const auto triggerDock = [](QMenu &menu)
        {
            for (QAction *action: menu.actions())
            {
                if (action->text() == QString::fromUtf8("贴边隐藏"))
                {
                    action->trigger();
                    return true;
                }
            }
            return false;
        };

        auto menu = dockActionOf(window);
        ASSERT_TRUE(triggerDock(*menu)) << "右键菜单里没有「贴边隐藏」这一项";
        EXPECT_EQ(window.edgeDock(), Pin::ScreenEdge::Left) << "菜单点了之后没贴边";
        EXPECT_EQ(window.geometry().right(), Pin::kEdgeStripWidth - 1) << "菜单点的这一档该当场收起来";

        menu = dockActionOf(window);
        ASSERT_TRUE(triggerDock(*menu));
        EXPECT_EQ(window.edgeDock(), Pin::ScreenEdge::None) << "再点一次该取消贴边";
        EXPECT_EQ(window.x(), available.left()) << "取消贴边后没整张摆回屏上";

        // 贴上去再拖走：拖离屏边要自动脱开。收起状态下能按到的只有露在屏上的那一条，
        // 所以先悬停把它展开，再按窗口正中（避开手柄）
        menu = dockActionOf(window);
        ASSERT_TRUE(triggerDock(*menu));
        ASSERT_EQ(window.edgeDock(), Pin::ScreenEdge::Left);
        QEnterEvent hover{QPointF{1.0, 1.0}, QPointF{1.0, 1.0}, QPointF{1.0, 1.0}};
        QApplication::sendEvent(&window, &hover);
        ASSERT_EQ(window.x(), available.left()) << "悬停没展开，拖走的判据就落不到实处";

        const QPointF grip = QRectF(QPointF(0, 0), QSizeF(window.size())).center();
        QMouseEvent   enterPress{QEvent::MouseButtonPress, grip, grip, Qt::LeftButton, Qt::LeftButton, Qt::NoModifier};
        QApplication::sendEvent(&window, &enterPress);
        QMouseEvent dragAway{QEvent::MouseMove, grip + QPointF{420.0, 330.0}, grip + QPointF{420.0, 330.0}, Qt::NoButton, Qt::LeftButton, Qt::NoModifier};
        QApplication::sendEvent(&window, &dragAway);
        QMouseEvent dragRelease{QEvent::MouseButtonRelease, grip + QPointF{420.0, 330.0}, grip + QPointF{420.0, 330.0}, Qt::LeftButton, Qt::NoButton, Qt::NoModifier};
        QApplication::sendEvent(&window, &dragRelease);
        EXPECT_EQ(window.edgeDock(), Pin::ScreenEdge::None) << "拖到屏中间松手还贴着边";
        EXPECT_GT(window.x(), available.left()) << "松手之后没停在拖到的位置";
    }

    /**
     * @brief 钉住动图真的在播：贴上去之后手里那张图跟着帧换，不是停在首帧
     * @details 判据走剪贴板：它读的就是窗口手里那张图（绘制、复制与保存共用同一份），
     *          换帧没接上时第二次复制拿到的还是第一帧的颜色。素材是一段手写的两帧 GIF。
     */
    TEST_F(PinWindowFixture, AnimatedSourceKeepsPlayingFrames)
    {
        const auto gifPath = PinTestAssets::writeTwoFrameGif(std::filesystem::temp_directory_path() / "GyanisScribe-PinAnim-two-frames.gif");
        ASSERT_FALSE(gifPath.isEmpty()) << "素材没落到临时文件上";

        auto &window = makeWindow();
        EXPECT_FALSE(window.isAnimated()) << "刚开的贴图不该是动图";

        window.setAnimatedSource(gifPath);
        ASSERT_TRUE(window.isAnimated()) << "两帧的 GIF 没被当成动图接上";
        EXPECT_EQ(window.state().sourceFile, gifPath) << "动图来源没进布局记录，重启后就接不上了";

        auto *movie = window.findChild<QMovie *>();
        ASSERT_NE(movie, nullptr) << "动图播放器没挂在这张窗口上";
        ASSERT_GE(movie->frameCount(), 2) << "素材没被读出两帧，判据无从谈起";

        EXPECT_EQ(window.image().pixelColor(0, 0), QColor(255, 0, 0)) << "首帧该是红色";

        ASSERT_TRUE(movie->jumpToNextFrame()) << "跳不到下一帧";
        EXPECT_EQ(window.image().pixelColor(0, 0), QColor(0, 0, 255)) << "换帧没接上：手里那张图还是第一帧";

        // 静图文件接不上动画这一档
        const auto staticPath = std::filesystem::temp_directory_path() / "GyanisScribe-PinAnim-still.png";
        ASSERT_TRUE(makeImage().save(QString::fromStdString(staticPath.string())));
        auto &other = makeWindow();
        other.setAnimatedSource(QString::fromStdString(staticPath.string()));
        EXPECT_FALSE(other.isAnimated()) << "单帧图也被当成动图了";
        EXPECT_TRUE(other.state().sourceFile.isEmpty()) << "静图不该在布局里记一个动图来源";
    }

    namespace
    {
        /// 在贴图窗的右键菜单里找一枚动作：找不到返回空
        [[nodiscard]] QAction *findMenuAction(QMenu &menu, const QString &text)
        {
            for (auto *action: menu.actions())
            {
                if (action->text() == text)
                {
                    return action;
                }
            }
            return nullptr;
        }

        /// 往标注视图上投一次拖拽（按下—行进—抬起）：坐标是控件坐标，1:1 的贴图上与画布坐标相等
        void dragOnView(QWidget &view, const QPoint &from, const QPoint &mid, const QPoint &to)
        {
            for (const auto &[type, position]: {std::pair{QEvent::MouseButtonPress, from}, std::pair{QEvent::MouseMove, mid}, std::pair{QEvent::MouseButtonRelease, to}})
            {
                QMouseEvent event{type, QPointF{position}, QPointF{position}, Qt::LeftButton, Qt::LeftButton, Qt::NoModifier};
                QApplication::sendEvent(&view, &event);
            }
        }
    } // namespace

    /**
     * @brief 钉住右键菜单里的「标注」：静图可点，点了就把标注视图铺到贴图上
     * @details 判据分两层：菜单里真的有这一项（发现性），点了之后整窗被标注视图盖住（接线）。
     */
    TEST_F(PinWindowFixture, MenuOffersDirectAnnotationAndItOpensTheView)
    {
        auto &window = makeWindow();

        QMenu *menu     = window.buildContextMenu();
        auto  *annotate = findMenuAction(*menu, QString::fromUtf8("标注"));
        ASSERT_NE(annotate, nullptr) << "右键菜单里没有「标注」这一项";
        EXPECT_TRUE(annotate->isEnabled()) << "静图的「标注」该是可点的";
        annotate->trigger();
        delete menu;

        EXPECT_TRUE(window.isAnnotating()) << "点了「标注」没进标注模式";
        auto *view = window.findChild<Annotation::AnnotationView *>();
        ASSERT_NE(view, nullptr) << "标注视图没挂到贴图窗上";
        EXPECT_EQ(view->geometry(), window.rect()) << "标注视图该铺满整窗";
    }

    /**
     * @brief 钉住直接标注的核心语义：画到收工才烘，Esc 是「收工保留」而不是「丢弃并关闭」
     * @details 贴图本来就存在，标注只是给它添一笔；Esc 若走关闭那一路，用户丢的是整张贴图。
     *          判据取「收工前后贴图手里那张图」：没收工不带笔迹，收工之后笔迹在、内里仍是底图。
     */
    TEST_F(PinWindowFixture, AnnotationPaintsAndBakesOnEscapeWithoutClosingThePin)
    {
        auto &window = makeWindow();
        auto  closed = false;
        QObject::connect(&window, &Pin::PinWindow::closedByUser, [&closed] { closed = true; });

        window.startAnnotation();
        auto *view = window.findChild<Annotation::AnnotationView *>();
        ASSERT_NE(view, nullptr) << "标注视图没挂上";
        ASSERT_TRUE(window.isAnnotating());

        dragOnView(*view, QPoint{5, 5}, QPoint{17, 17}, QPoint{30, 30});

        // 还没收工：贴图手里那份图不该带上笔迹（画布上的图形还归引擎的撤销栈管）
        EXPECT_EQ(window.image().pixelColor(5, 20), QColor(200, 60, 60)) << "没收工就把笔迹烘进了贴图";

        pressKey(window, Qt::Key_Escape);
        EXPECT_FALSE(window.isAnnotating()) << "Esc 没退出标注模式";
        EXPECT_FALSE(closed) << "标注里的 Esc 把贴图关掉了：那一击该是「收工」而不是「丢弃」";

        // 读数直接取贴图手里的那张图：经系统剪贴板读回会把这条判据搭在桌面上别的进程身上
        const auto baked = window.image();
        EXPECT_NE(baked.pixelColor(5, 20), QColor(200, 60, 60)) << "收工没把笔迹烘进贴图";
        EXPECT_EQ(baked.pixelColor(17, 17), QColor(200, 60, 60)) << "空心矩形的内里不该有笔迹";

        // 收工之后的 Esc 才回到贴图自己的含义：关掉这张
        pressKey(window, Qt::Key_Escape);
        EXPECT_TRUE(closed) << "收工后 Esc 该照旧关闭贴图";
    }

    /**
     * @brief 钉住进入标注先把贴图转正、收工转回原角度
     * @details 标注视图是轴对齐的，斜着的画布铺不平；成品记的是图片自己的像素，
     *          转回去之后批注自然跟着图一起转。两段各判一次，漏任一段都会让用户看到一张歪回去的图。
     */
    TEST_F(PinWindowFixture, AnnotationRestoresRotationAfterFinishing)
    {
        auto &window = makeWindow();
        window.setRotationDegrees(90.0);
        ASSERT_TRUE(nearEqual(window.rotationDegrees(), 90.0, kDegreesTolerance));

        window.startAnnotation();
        EXPECT_TRUE(nearEqual(window.rotationDegrees(), 0.0, kDegreesTolerance)) << "进标注没把画布转正";

        window.finishAnnotation();
        EXPECT_FALSE(window.isAnnotating());
        EXPECT_TRUE(nearEqual(window.rotationDegrees(), 90.0, kDegreesTolerance)) << "收工没把旋转转回去";
    }

    /**
     * @brief 钉住标注模式吞键：贴图自己的 T/R/0 不许在标注中生效，收工之后照旧
     * @details 按 T 把正在标的图缩成小方块、按 R 把画布转走，都是「键盘漏到了另一套语义」的形状。
     */
    TEST_F(PinWindowFixture, AnnotationKeysDoNotLeakToThePin)
    {
        auto &window = makeWindow();
        window.startAnnotation();

        pressKey(window, Qt::Key_T);
        EXPECT_FALSE(window.isThumbnailMode()) << "按 T 把正在标的贴图缩成了小方块";
        pressKey(window, Qt::Key_R);
        EXPECT_TRUE(nearEqual(window.rotationDegrees(), 0.0, kDegreesTolerance)) << "按 R 动的是贴图角度，标注中该归工具键";
        pressKey(window, Qt::Key_0);
        EXPECT_TRUE(nearEqual(window.scale(), 1.0, kScaleTolerance)) << "按 0 在标注中不该动缩放";

        window.finishAnnotation();
        pressKey(window, Qt::Key_T);
        EXPECT_TRUE(window.isThumbnailMode()) << "收工之后 T 该还是缩略图开关";
    }

    /**
     * @brief 钉住标注中滚轮不缩不转：收工之后恢复
     * @details 滚轮缩一下会把标注视图铺着的坐标面改掉，画上去的点就与像素对不上。
     */
    TEST_F(PinWindowFixture, WheelAndDragAreInertWhileAnnotating)
    {
        auto &window = makeWindow();
        window.startAnnotation();

        QWheelEvent zoomIn{QPointF{20.0, 20.0}, QPointF{20.0, 20.0}, QPoint{}, QPoint{0, 120}, Qt::NoButton, Qt::NoModifier, Qt::NoScrollPhase, false};
        QApplication::sendEvent(&window, &zoomIn);
        EXPECT_TRUE(nearEqual(window.scale(), 1.0, kScaleTolerance)) << "标注中滚轮把贴图缩了";

        // 拖窗也不该动：鼠标归标注视图，窗口这一层要按住不动
        const auto  before = window.pos();
        QMouseEvent press{QEvent::MouseButtonPress, QPointF{20.0, 20.0}, QPointF{20.0, 20.0}, Qt::LeftButton, Qt::LeftButton, Qt::NoModifier};
        QApplication::sendEvent(&window, &press);
        EXPECT_EQ(window.pos(), before) << "标注中按下把贴图挪走了";

        window.finishAnnotation();
        QApplication::sendEvent(&window, &zoomIn);
        EXPECT_FALSE(nearEqual(window.scale(), 1.0, kScaleTolerance)) << "收工之后滚轮缩放该恢复";
    }

    /**
     * @brief 钉住动图不进直接标注：菜单灰掉并写明出路，绕过菜单直接开也不进
     * @details 标注画在一帧上，下一帧就把它盖掉了；收下这一轮改动等于收下一场注定丢掉的编辑。
     */
    TEST_F(PinWindowFixture, AnimatedPinsDoNotOfferDirectAnnotation)
    {
        const auto gifPath = PinTestAssets::writeTwoFrameGif(std::filesystem::temp_directory_path() / "GyanisScribe-PinAnnotate-anim.gif");
        ASSERT_FALSE(gifPath.isEmpty()) << "素材没落到临时文件上";

        auto &window = makeWindow();
        window.setAnimatedSource(gifPath);
        ASSERT_TRUE(window.isAnimated());

        QMenu *menu     = window.buildContextMenu();
        auto  *annotate = findMenuAction(*menu, QString::fromUtf8("标注"));
        ASSERT_NE(annotate, nullptr);
        EXPECT_FALSE(annotate->isEnabled()) << "动图的「标注」该灰掉：标注会被下一帧盖掉";
        EXPECT_FALSE(annotate->toolTip().isEmpty()) << "灰掉也要写明出路，不把「点了没反应」留给人猜";
        delete menu;

        window.startAnnotation();
        EXPECT_FALSE(window.isAnnotating()) << "绕过菜单直接开也进了标注：动图进标注是注定的数据丢失";
    }

    /**
     * @brief 钉住标注里的「贴到屏幕上」＝把成品另贴一张：发信号、收工、不动这一张
     */
    TEST_F(PinWindowFixture, AnnotationPinAnotherRequestsANewPin)
    {
        auto                 &window = makeWindow();
        std::optional<QImage> another;
        QObject::connect(&window, &Pin::PinWindow::pinAnotherRequested, [&another](const QImage &image) { another = image; });

        window.startAnnotation();
        pressKey(window, Qt::Key_D, Qt::ControlModifier);

        ASSERT_TRUE(another.has_value()) << "Ctrl+D 没发出「另贴一张」的请求";
        EXPECT_FALSE(window.isAnnotating()) << "「另贴一张」之后该收工";
        EXPECT_EQ(another->size(), makeImage().size()) << "交出去的该是这张贴图的成品尺寸";
    }

    /**
     * @brief 钉住标注中的字母工具键：切到画笔再画，图形按新工具落笔
     * @details 画一条斜线：画笔下 (16,14) 在中途、(24,8) 离笔迹远；若工具没切（还是矩形），
     *          外接框的上边会把 (24,8) 也画上——两处探针一起判「换的是画笔还是矩形」。
     */
    TEST_F(PinWindowFixture, AnnotationToolKeySwitchesTheDrawingTool)
    {
        auto &window = makeWindow();
        window.startAnnotation();
        auto *view = window.findChild<Annotation::AnnotationView *>();
        ASSERT_NE(view, nullptr);

        pressKey(window, Qt::Key_P);
        dragOnView(*view, QPoint{8, 8}, QPoint{16, 14}, QPoint{24, 20});
        window.finishAnnotation();

        const auto baked = window.image();
        EXPECT_NE(baked.pixelColor(16, 14), QColor(200, 60, 60)) << "画笔的笔迹没落上";
        EXPECT_EQ(baked.pixelColor(24, 8), QColor(200, 60, 60)) << "(24,8) 被画上了：工具没切成画笔，落的是矩形";
    }

    /**
     * @brief 钉住拖放落下：窗口把载荷与换算回全局的落点一起交出去
     * @details 落点是新贴图的位置，用窗口内坐标交出去就会错到屏角上；
     *          窗口挪到一个已知位置，两处坐标手算得出。
     *          必须先投一次被收下的 DragEnter：QApplication::notify 只把 Drop 投给
     *          收下过 DragEnter 的那个窗口（QDragManager 的 currentTarget），
     *          裸投 Drop 会在分发层被静静丢掉，判据到不了 dropEvent。
     */
    TEST_F(PinWindowFixture, DropRequestedCarriesThePayloadAndTheGlobalPoint)
    {
        auto &window = makeWindow();
        EXPECT_TRUE(window.acceptDrops()) << "贴图窗没开拖放";
        window.move(300, 200);

        QMimeData mime;
        mime.setText(QStringLiteral("hello"));

        QDragEnterEvent enter{QPoint{10, 12}, Qt::CopyAction, &mime, Qt::LeftButton, Qt::NoModifier};
        QApplication::sendEvent(&window, &enter);
        ASSERT_TRUE(enter.isAccepted()) << "拖动进入没收下，后面的落下无从谈起";

        const QMimeData      *seen = nullptr;
        std::optional<QPoint> at;
        QObject::connect(&window, &Pin::PinWindow::dropRequested,
                         [&](const QMimeData *data, const QPoint &globalTopLeft)
                         {
                             seen = data;
                             at   = globalTopLeft;
                         });

        QDropEvent drop{QPointF{10.0, 12.0}, Qt::CopyAction, &mime, Qt::LeftButton, Qt::NoModifier};
        QApplication::sendEvent(&window, &drop);

        ASSERT_NE(seen, nullptr) << "落下没发出「有东西被拖进来」";
        EXPECT_EQ(seen, &mime) << "交出去的不是拖放里那一份载荷";
        ASSERT_TRUE(at.has_value());
        EXPECT_EQ(*at, QPoint(310, 212)) << "落点没换算回全局坐标，新贴图会错到别处";
    }

    /**
     * @brief 钉住拖动的收与不收：带内容的收、空载荷不收、标注中一律不收
     * @details 标注中收下拖放会把载荷落在正画着的这张画布上——那一阵子这张图不是「贴图」而是画布。
     *          落下那一半要先把一次正常的拖动收下（currentTarget 指向本窗口）再进标注模式，
     *          否则裸投的 Drop 在分发层就被丢掉了，判据验不到 dropEvent 里的守卫。
     */
    TEST_F(PinWindowFixture, DragEnterAcceptsPinnablePayloadsAndRefusesWhileAnnotating)
    {
        auto &window = makeWindow();

        QMimeData       empty;
        QDragEnterEvent refused{QPoint{5, 5}, Qt::CopyAction, &empty, Qt::LeftButton, Qt::NoModifier};
        QApplication::sendEvent(&window, &refused);
        EXPECT_FALSE(refused.isAccepted()) << "空载荷也收下了";

        QMimeData text;
        text.setText(QStringLiteral("x"));
        QDragEnterEvent accepted{QPoint{5, 5}, Qt::CopyAction, &text, Qt::LeftButton, Qt::NoModifier};
        QApplication::sendEvent(&window, &accepted);
        EXPECT_TRUE(accepted.isAccepted()) << "带文本的拖动没收下";

        auto dropped = false;
        QObject::connect(&window, &Pin::PinWindow::dropRequested, [&dropped](const QMimeData *, const QPoint &) { dropped = true; });

        window.startAnnotation();
        QDragEnterEvent during{QPoint{5, 5}, Qt::CopyAction, &text, Qt::LeftButton, Qt::NoModifier};
        QApplication::sendEvent(&window, &during);
        EXPECT_FALSE(during.isAccepted()) << "标注中把拖动也收下了";

        QDropEvent drop{QPointF{5.0, 5.0}, Qt::CopyAction, &text, Qt::LeftButton, Qt::NoModifier};
        QApplication::sendEvent(&window, &drop);
        EXPECT_FALSE(dropped) << "标注中把载荷也放下了";
    }

    /**
     * @brief 钉住右键菜单的「复制为文件」：剪贴板里交出去的是一条本地文件路径（粘进资源管理器是一份 .png）
     * @details 与「复制」是两条出口：那一条交像素（粘进文档是一张图），这一条交文件。
     *          判据取剪贴板里那条 URL 指着的文件真在、且像素对得上。
     */
    TEST_F(PinWindowFixture, CopyAsFilePutsALocalFileOnTheClipboard)
    {
        auto  &window   = makeWindow();
        QMenu *menu     = window.buildContextMenu();
        auto  *copyFile = findMenuAction(*menu, QString::fromUtf8("复制为文件"));
        ASSERT_NE(copyFile, nullptr) << "右键菜单里没有「复制为文件」这一项";
        copyFile->trigger();
        delete menu;

        const QMimeData *mime = QApplication::clipboard()->mimeData();
        ASSERT_NE(mime, nullptr) << "剪贴板里什么都没有";
        ASSERT_TRUE(mime->hasUrls()) << "交出去的不是文件 URL，粘进资源管理器只会得到像素";
        ASSERT_EQ(mime->urls().size(), 1);
        const auto filePath = mime->urls().front().toLocalFile();
        EXPECT_TRUE(QFileInfo::exists(filePath)) << "剪贴板里指的那份文件不在";
        const QImage readBack{filePath};
        ASSERT_FALSE(readBack.isNull()) << "指过去的那份文件读不成图";
        EXPECT_EQ(readBack.size(), window.image().size()) << "交出去的文件不是这张贴图";
    }

    /**
     * @brief 左半不透明、右半透明的图：背景色这一档的判据全靠「透明处露出来的是什么」
     */
    QImage makeHalfTransparentImage()
    {
        QImage image{QSize{40, 20}, QImage::Format_ARGB32};
        image.fill(Qt::transparent);
        for (int y = 0; y < 20; ++y)
        {
            for (int x = 0; x < 20; ++x)
            {
                image.setPixelColor(x, y, QColor(200, 60, 60));
            }
        }
        image.setDevicePixelRatio(1.0);
        return image;
    }

    /**
     * @brief 钉住背景色（白板）：垫上之后透明像素处露出来的是它，不透明处与关掉之后都不受影响
     * @details 渲染到一张透明画布上：没垫背景时右半边读出来是透明的，垫了白之后是不透明白——
     *          这一条同时钉住「垫在图的后面」与「只改看、不改像素」（贴图手里那份图始终是原样）。
     */
    TEST_F(PinWindowFixture, BackgroundColorFillsBehindTransparentPixels)
    {
        auto &window = makeWindow(makeHalfTransparentImage());

        const auto painted = [&window]
        {
            QImage canvas{window.size(), QImage::Format_ARGB32};
            canvas.fill(Qt::transparent);
            window.render(&canvas);
            return canvas;
        };

        // 这张窗口不是透明背景的：不垫背景色时，透明像素处露出来的是主题的窗口底色
        EXPECT_EQ(painted().pixelColor(30, 10), window.palette().color(QPalette::Window)) << "不垫背景时透明处该露主题底色，后面的对照没有基准";
        EXPECT_EQ(painted().pixelColor(10, 10), QColor(200, 60, 60)) << "不透明处该是图自己的色";

        window.setBackgroundColor(QColor(255, 255, 255));
        EXPECT_EQ(painted().pixelColor(30, 10), QColor(255, 255, 255)) << "垫了白，透明处没露出来";
        EXPECT_EQ(painted().pixelColor(10, 10), QColor(200, 60, 60)) << "垫背景把不透明处也盖了";

        window.setBackgroundColor(QColor{});
        EXPECT_EQ(painted().pixelColor(30, 10), window.palette().color(QPalette::Window)) << "撤掉背景之后没回到主题底色";

        // 贴图手里那份像素从头到尾没被动过：白板只改「看」
        EXPECT_EQ(window.image().pixelColor(30, 10).alpha(), 0) << "白板改了贴图手里的像素";
    }

    /**
     * @brief 钉住背景色的菜单：四项预设加自定义，勾选态就是当前那一档
     */
    TEST_F(PinWindowFixture, BackgroundMenuOffersPresetsAndMarksTheCurrentOne)
    {
        auto &window = makeWindow();

        const auto backgroundMenuOf = [&window]
        {
            auto *menu = window.buildContextMenu();
            for (auto *action: menu->actions())
            {
                if (action->menu() != nullptr && action->text() == QString::fromUtf8("背景色"))
                {
                    return std::make_pair(menu, action->menu());
                }
            }
            delete menu;
            return std::make_pair(static_cast<QMenu *>(nullptr), static_cast<QMenu *>(nullptr));
        };

        const auto pick = [](QMenu &submenu, const QString &label)
        {
            for (auto *action: submenu.actions())
            {
                if (action->text() == label)
                {
                    action->trigger();
                    return true;
                }
            }
            return false;
        };

        {
            auto [menu, submenu] = backgroundMenuOf();
            ASSERT_NE(submenu, nullptr) << "右键菜单里没有「背景色」这一项";
            // 只查在不在：不去点「自定义…」——那一项会弹模态取色框，用例点下去就挂在那儿等人关
            const auto labels = [submenu]
            {
                QStringList found;
                for (auto *action: submenu->actions())
                {
                    found << action->text();
                }
                return found;
            }();
            for (const auto &label: {"跟随主题", "白色", "黑色", "灰色", "自定义…"})
            {
                EXPECT_TRUE(labels.contains(QString::fromUtf8(label))) << "背景色里少一项：" << label;
            }
            ASSERT_TRUE(pick(*submenu, QString::fromUtf8("灰色")));
            delete menu;
        }
        EXPECT_EQ(window.backgroundColor(), QColor(128, 128, 128)) << "点了「灰色」没落到背景色上";

        // 重开菜单：勾选态跟着当前这一档走
        auto [menu, submenu] = backgroundMenuOf();
        ASSERT_NE(submenu, nullptr);
        for (auto *action: submenu->actions())
        {
            if (action->text() == QString::fromUtf8("灰色"))
            {
                EXPECT_TRUE(action->isCheckable()) << "背景色各项该是勾选项";
                EXPECT_TRUE(action->isChecked()) << "当前是灰色，菜单里没勾上";
            } else if (action->text() == QString::fromUtf8("白色"))
            {
                EXPECT_FALSE(action->isChecked()) << "不是当前档的那一项也勾上了";
            }
        }
        ASSERT_TRUE(pick(*submenu, QString::fromUtf8("跟随主题")));
        delete menu;
        EXPECT_FALSE(window.backgroundColor().isValid()) << "「跟随主题」没把背景撤掉";
    }

    /**
     * @brief 钉住「只显示这一张」的菜单项：勾选项、点一下把开关意图发出去（谁在屏上由管理器算）
     */
    TEST_F(PinWindowFixture, SoloMenuEntryRequestsTheToggle)
    {
        auto &window = makeWindow();

        std::optional<bool> requested;
        QObject::connect(&window, &Pin::PinWindow::soloRequested, [&requested](bool solo) { requested = solo; });

        QMenu *menu  = window.buildContextMenu();
        auto  *entry = findMenuAction(*menu, QString::fromUtf8("只显示这一张"));
        ASSERT_NE(entry, nullptr) << "右键菜单里没有「只显示这一张」这一项";
        EXPECT_TRUE(entry->isCheckable()) << "该是勾选项";
        EXPECT_FALSE(entry->isChecked()) << "起点该是没开";
        entry->trigger();
        delete menu;

        ASSERT_TRUE(requested.has_value()) << "点了没把开关意图发出去";
        EXPECT_TRUE(*requested) << "起点没开时点一下该是「开」";

        // 管理器把主角态推下来之后，勾选态与发出的值都跟着走
        window.setSoloState(true);
        QMenu *again = window.buildContextMenu();
        auto  *item  = findMenuAction(*again, QString::fromUtf8("只显示这一张"));
        ASSERT_NE(item, nullptr);
        EXPECT_TRUE(item->isChecked()) << "主角态推下来之后勾选态没跟上";
        requested.reset();
        item->trigger();
        delete again;
        ASSERT_TRUE(requested.has_value());
        EXPECT_FALSE(*requested) << "已经是主角时点一下该是「关」";
    }

    /**
     * @brief 钉住选中态那一圈：选进多选集的贴图在边缘画一圈强调色（没选中、没悬停时边缘是图自己的色）
     */
    TEST_F(PinWindowFixture, SelectedPinPaintsAnAccentBorder)
    {
        auto &window = makeWindow();

        const auto edgeColor = [&window]
        {
            QImage canvas{window.size(), QImage::Format_ARGB32};
            canvas.fill(Qt::transparent);
            window.render(&canvas);
            return canvas.pixelColor(0, window.height() / 2);
        };

        EXPECT_EQ(edgeColor(), QColor(200, 60, 60)) << "没选中也没悬停时边缘该是图自己的色，后面的对照没有基准";
        window.setSelected(true);
        EXPECT_NE(edgeColor(), QColor(200, 60, 60)) << "选中之后边缘还是原色：选中圈没画出来";
        window.setSelected(false);
        EXPECT_EQ(edgeColor(), QColor(200, 60, 60)) << "取消选中之后圈还留着";
    }

    /**
     * @brief 2×2 的四格图：左上红、右上绿、左下蓝、右下黄——翻转与滤镜在屏上都能一眼读数
     */
    QImage makeQuadImage()
    {
        QImage image{QSize{40, 40}, QImage::Format_ARGB32};
        image.fill(QColor(255, 0, 0));
        for (int y = 0; y < 20; ++y)
        {
            for (int x = 20; x < 40; ++x)
            {
                image.setPixelColor(x, y, QColor(0, 255, 0));
            }
        }
        for (int y = 20; y < 40; ++y)
        {
            for (int x = 0; x < 20; ++x)
            {
                image.setPixelColor(x, y, QColor(0, 0, 255));
            }
            for (int x = 20; x < 40; ++x)
            {
                image.setPixelColor(x, y, QColor(255, 255, 0));
            }
        }
        image.setDevicePixelRatio(1.0);
        return image;
    }

    /**
     * @brief 钉住 `H` / `V` 翻转：屏上那一份左右/上下镜像，再按一次翻回来
     * @details 判据取「渲染出来的像素」——渲染用的是 render() 而不是 grab()：
     *          grab() 在 125% 缩放的屏上会按屏的倍比出图，探点坐标就与窗口逻辑坐标对不上了。
     */
    TEST_F(PinWindowFixture, FlipKeysMirrorThePaintedImage)
    {
        auto &window = makeWindow(makeQuadImage());

        const auto painted = [&window]
        {
            QImage canvas{window.size(), QImage::Format_ARGB32};
            canvas.fill(Qt::transparent);
            window.render(&canvas);
            return canvas;
        };

        const QImage before = painted();
        EXPECT_EQ(before.size(), QSize(40, 40)) << "窗口尺寸不是图片尺寸，下面的探点没有基准";
        EXPECT_EQ(before.pixelColor(10, 10), QColor(255, 0, 0)) << "左上该是红";
        EXPECT_EQ(before.pixelColor(30, 10), QColor(0, 255, 0)) << "右上该是绿";

        pressKey(window, Qt::Key_H);
        EXPECT_TRUE(window.mirroredHorizontally());
        const QImage flippedH = painted();
        EXPECT_EQ(flippedH.pixelColor(10, 10), QColor(0, 255, 0)) << "左右翻转后左上该是原来右上的绿";
        EXPECT_EQ(flippedH.pixelColor(30, 10), QColor(255, 0, 0)) << "左右翻转后右上该是原来左上的红";

        pressKey(window, Qt::Key_H);
        EXPECT_FALSE(window.mirroredHorizontally()) << "再按一次该翻回来";
        EXPECT_EQ(painted().pixelColor(10, 10), QColor(255, 0, 0));

        pressKey(window, Qt::Key_V);
        EXPECT_TRUE(window.mirroredVertically());
        const QImage flippedV = painted();
        EXPECT_EQ(flippedV.pixelColor(10, 10), QColor(0, 0, 255)) << "上下翻转后左上该是原来左下的蓝";
        EXPECT_EQ(flippedV.pixelColor(10, 30), QColor(255, 0, 0)) << "上下翻转后左下该是原来左上的红";
    }

    /**
     * @brief 钉住 `G` 灰度与 `I` 反色：屏上那一份按 luma 折灰、按 255-c 反相，两样可叠
     * @details 手算锚点：纯红的 luma = (255×11)/32 = 87；叠上反色成 168（先灰度、后反色）。
     */
    TEST_F(PinWindowFixture, GrayAndInvertKeysChangeThePaintedPixels)
    {
        auto &window = makeWindow(makeQuadImage());

        const auto painted = [&window]
        {
            QImage canvas{window.size(), QImage::Format_ARGB32};
            canvas.fill(Qt::transparent);
            window.render(&canvas);
            return canvas;
        };

        pressKey(window, Qt::Key_G);
        EXPECT_TRUE(window.grayscale());
        EXPECT_EQ(painted().pixelColor(10, 10), QColor(87, 87, 87, 255)) << "纯红的灰度不是手算的 87";

        pressKey(window, Qt::Key_I);
        EXPECT_TRUE(window.inverted());
        EXPECT_EQ(painted().pixelColor(10, 10), QColor(168, 168, 168, 255)) << "灰度叠反色的次序或取值不对";

        pressKey(window, Qt::Key_G);
        pressKey(window, Qt::Key_I);
        EXPECT_FALSE(window.grayscale());
        EXPECT_FALSE(window.inverted());
        EXPECT_EQ(painted().pixelColor(10, 10), QColor(255, 0, 0)) << "两档关掉之后屏上不是原色";

        // 贴图手里那份像素从头到尾没被视图滤镜动过：复制/保存拿到的仍是原图
        EXPECT_EQ(window.image().pixelColor(10, 10), QColor(255, 0, 0)) << "视图滤镜改了贴图手里的像素";
    }

    /**
     * @brief 钉住四档视图变换的菜单项：勾选态就是当前读数，点一下翻转状态
     */
    TEST_F(PinWindowFixture, ViewTransformMenuItemsShowAndToggleState)
    {
        auto &window = makeWindow();

        {
            auto menu = std::unique_ptr<QMenu>{window.buildContextMenu()};
            for (const auto &label: {"左右翻转", "上下翻转", "灰度显示", "反色"})
            {
                auto *action = findMenuAction(*menu, QString::fromUtf8(label));
                ASSERT_NE(action, nullptr) << "右键菜单里没有「" << label << "」这一项";
                EXPECT_TRUE(action->isCheckable()) << label << " 该是勾选项";
                EXPECT_FALSE(action->isChecked()) << label << " 起点该是没开";
            }
            findMenuAction(*menu, QString::fromUtf8("灰度显示"))->trigger();
            findMenuAction(*menu, QString::fromUtf8("上下翻转"))->trigger();
        }
        EXPECT_TRUE(window.grayscale()) << "菜单点了灰度没生效";
        EXPECT_TRUE(window.mirroredVertically()) << "菜单点了上下翻转没生效";

        auto menu = std::unique_ptr<QMenu>{window.buildContextMenu()};
        EXPECT_TRUE(findMenuAction(*menu, QString::fromUtf8("灰度显示"))->isChecked()) << "重开菜单勾选态没跟上";
        EXPECT_TRUE(findMenuAction(*menu, QString::fromUtf8("上下翻转"))->isChecked());
        EXPECT_FALSE(findMenuAction(*menu, QString::fromUtf8("左右翻转"))->isChecked());
    }

    /**
     * @brief 钉住进标注时翻转先摆正、退出后原样恢复（与旋转同一条规矩）
     * @details 标注视图是轴对齐且不镜像的：翻着的画布铺不平，所以进去先归位；
     *          退出收工之后用户看到的那一份还得是他翻好的样子。
     */
    TEST_F(PinWindowFixture, EnteringAnnotationClearsMirrorsAndRestoresThem)
    {
        auto &window = makeWindow();
        window.setMirroredHorizontally(true);
        window.setMirroredVertically(true);

        window.startAnnotation();
        EXPECT_TRUE(window.isAnnotating());
        EXPECT_FALSE(window.mirroredHorizontally()) << "进标注没摆正：标注画布不镜像";
        EXPECT_FALSE(window.mirroredVertically()) << "进标注没摆正：标注画布不镜像";

        pressKey(window, Qt::Key_Escape);
        EXPECT_FALSE(window.isAnnotating());
        EXPECT_TRUE(window.mirroredHorizontally()) << "退出标注没把左右翻转恢复";
        EXPECT_TRUE(window.mirroredVertically()) << "退出标注没把上下翻转恢复";
    }

    /**
     * @brief 钉住裁剪：拖一个框、回车 → 只留框里那一块，窗口跟着缩到新尺寸
     * @details 四格图 40×40，(10,10) 拖到 (20,30) 该裁出 20×20——拿两个角去问点命中会多算一列
     *          （框拖到 20..30 会裁出 21 像素宽），这条判据顺带把那一处钉死。
     */
    TEST_F(PinWindowFixture, CropDragThenEnterKeepsOnlyTheBox)
    {
        auto &window = makeWindow(makeQuadImage());
        window.startCropping();
        ASSERT_TRUE(window.isCropping());

        for (const auto &[type, position]:
             {std::pair{QEvent::MouseButtonPress, QPoint{10, 10}}, std::pair{QEvent::MouseMove, QPoint{20, 20}}, std::pair{QEvent::MouseButtonRelease, QPoint{30, 30}}})
        {
            QMouseEvent event{type, QPointF{position}, QPointF{position}, Qt::LeftButton, Qt::LeftButton, Qt::NoModifier};
            QApplication::sendEvent(&window, &event);
        }
        pressKey(window, Qt::Key_Return);

        EXPECT_FALSE(window.isCropping());
        const auto cropped = window.image();
        EXPECT_EQ(cropped.size(), QSize(20, 20)) << "裁出来的不是框里那一块（右/下边界多半多算了一格）";
        EXPECT_EQ(cropped.pixelColor(1, 1), QColor(255, 0, 0)) << "框左上角该是原来的红";
        EXPECT_EQ(cropped.pixelColor(18, 1), QColor(0, 255, 0)) << "框右上角该是原来的绿";
        EXPECT_EQ(cropped.pixelColor(1, 18), QColor(0, 0, 255)) << "框左下角该是原来的蓝";
        EXPECT_EQ(cropped.pixelColor(18, 18), QColor(255, 255, 0)) << "框右下角该是原来的黄";
        EXPECT_EQ(window.size(), QSize(20, 20)) << "窗口没跟着裁后的尺寸缩";
    }

    /**
     * @brief 钉住裁剪的取消与摆正：旋转与翻转在裁剪期间清掉、Esc 原样退出并恢复
     */
    TEST_F(PinWindowFixture, CropEscapeLeavesTheImageAndRestoresTheViewTransform)
    {
        auto &window = makeWindow(makeQuadImage());
        window.setRotationDegrees(90.0);
        window.setMirroredHorizontally(true);

        window.startCropping();
        ASSERT_TRUE(window.isCropping());
        EXPECT_TRUE(nearEqual(window.rotationDegrees(), 0.0, kDegreesTolerance)) << "进裁剪没摆正：裁剪框是轴对齐的";
        EXPECT_FALSE(window.mirroredHorizontally()) << "进裁剪没摆正：裁剪框是轴对齐的";

        for (const auto &[type, position]:
             {std::pair{QEvent::MouseButtonPress, QPoint{10, 10}}, std::pair{QEvent::MouseMove, QPoint{20, 20}}, std::pair{QEvent::MouseButtonRelease, QPoint{30, 30}}})
        {
            QMouseEvent event{type, QPointF{position}, QPointF{position}, Qt::LeftButton, Qt::LeftButton, Qt::NoModifier};
            QApplication::sendEvent(&window, &event);
        }
        pressKey(window, Qt::Key_Escape);

        EXPECT_FALSE(window.isCropping());
        EXPECT_EQ(window.image().size(), QSize(40, 40)) << "Esc 退出把图裁了";
        EXPECT_TRUE(nearEqual(window.rotationDegrees(), 90.0, kDegreesTolerance)) << "退出裁剪没恢复旋转";
        EXPECT_TRUE(window.mirroredHorizontally()) << "退出裁剪没恢复翻转";
    }

    /**
     * @brief 钉住没拖出像样的框就回车：图原样不动（手抖点一下不该把图裁成 1×1）
     * @details 两种形状各判一次：没拖（框退化成一个点）与只拖了 1 像素（够折出 1×1 的框，
     *          由最小边长的守卫拦下）。少了后一种，那道守卫就没有用例罩着。
     */
    TEST_F(PinWindowFixture, CropCommitWithoutAMeaningfulBoxLeavesTheImageAlone)
    {
        auto &window = makeWindow(makeQuadImage());
        window.startCropping();
        ASSERT_TRUE(window.isCropping());

        pressKey(window, Qt::Key_Return);
        EXPECT_FALSE(window.isCropping());
        EXPECT_EQ(window.image().size(), QSize(40, 40)) << "没拖出框也把图裁了";

        window.startCropping();
        ASSERT_TRUE(window.isCropping());
        for (const auto &[type, position]: {std::pair{QEvent::MouseButtonPress, QPoint{10, 10}}, std::pair{QEvent::MouseButtonRelease, QPoint{11, 11}}})
        {
            QMouseEvent event{type, QPointF{position}, QPointF{position}, Qt::LeftButton, Qt::LeftButton, Qt::NoModifier};
            QApplication::sendEvent(&window, &event);
        }
        pressKey(window, Qt::Key_Return);
        EXPECT_EQ(window.image().size(), QSize(40, 40)) << "1×1 的框也裁了：最小边长那道守卫没起作用";
    }

    /**
     * @brief 钉住动图不能裁剪：菜单项灰掉并写明出路，绕过菜单直接开也不进（与「标注」同一条口径）
     */
    TEST_F(PinWindowFixture, AnimatedPinsDoNotOfferCropping)
    {
        const auto gifPath = PinTestAssets::writeTwoFrameGif(std::filesystem::temp_directory_path() / "GyanisScribe-PinCrop-anim.gif");
        ASSERT_FALSE(gifPath.isEmpty()) << "素材没落到临时文件上";

        auto &window = makeWindow();
        window.setAnimatedSource(gifPath);
        ASSERT_TRUE(window.isAnimated());

        QMenu *menu = window.buildContextMenu();
        auto  *crop = findMenuAction(*menu, QString::fromUtf8("裁剪"));
        ASSERT_NE(crop, nullptr) << "右键菜单里没有「裁剪」这一项";
        EXPECT_FALSE(crop->isEnabled()) << "动图的「裁剪」该灰掉：裁完会被下一帧盖回原尺寸";
        EXPECT_FALSE(crop->toolTip().isEmpty()) << "灰掉也要写明出路，不把「点了没反应」留给人猜";
        delete menu;

        window.startCropping();
        EXPECT_FALSE(window.isCropping()) << "绕过菜单直接开也进了裁剪";
    }

    /**
     * @brief 钉住「自定义尺寸」：按宽或按高设精确尺寸、保持长宽比、越界夹在缩放区间里
     * @details 只查菜单项在不在、不点它：点了会弹输入对话框（模态），用例会挂在那儿——
     *          与背景色那条「不点自定义…」同一套纪律。40×40 的方图：120 宽 → 3.0 倍、窗口 120×120。
     */
    TEST_F(PinWindowFixture, CustomSizeSetsAnExactWidthAndKeepsTheRatio)
    {
        auto  &window = makeWindow();
        QMenu *menu   = window.buildContextMenu();
        auto  *exact  = findMenuAction(*menu, QString::fromUtf8("自定义尺寸…"));
        ASSERT_NE(exact, nullptr) << "右键菜单里没有「自定义尺寸…」这一项";
        delete menu;

        window.setExactSize(QSize{120, 0});
        EXPECT_TRUE(nearEqual(window.scale(), 3.0, kScaleTolerance)) << "按宽设的精确值没落到缩放上";
        EXPECT_EQ(window.size(), QSize(120, 120)) << "窗口没跟着缩放到指定的显示尺寸";

        window.setExactSize(QSize{0, 80});
        EXPECT_TRUE(nearEqual(window.scale(), 2.0, kScaleTolerance)) << "宽没给时该按高算";

        const auto before = window.scale();
        window.setExactSize(QSize{-5, -5});
        EXPECT_TRUE(nearEqual(window.scale(), before, kScaleTolerance)) << "两维都没给时不该动";

        window.setExactSize(QSize{100000, 0});
        EXPECT_TRUE(nearEqual(window.scale(), 20.0, kScaleTolerance)) << "超界的精确值该夹在缩放上限上（夹不住就是第二套坐标口径）";
    }

    /**
     * @brief 钉住窗口阴影：窗口比内容每边大一圈、内容居中不动、投影贴着内容一圈圈淡出去
     * @details 判据取渲染出来的像素（render 而不是 grab，见翻转那条的说明）。40×40 的四色块：
     *          留边 8 之后窗口 56×56，色块探点整体偏移 8；(6,28) 是贴内容最深的一圈、
     *          (2,28) 是更外更淡的一圈——两处的深浅差就是「有投影」这个事实。
     */
    TEST_F(PinWindowFixture, ShadowPadsTheWindowAndPaintsAGradedHalo)
    {
        auto &window = makeWindow(makeQuadImage());

        window.setShadowEnabled(true);
        EXPECT_TRUE(window.shadowEnabled());
        EXPECT_EQ(window.size(), QSize(40 + 2 * Pin::kShadowMargin, 40 + 2 * Pin::kShadowMargin)) << "窗口没比内容每边大一圈";

        QImage canvas{window.size(), QImage::Format_ARGB32};
        canvas.fill(Qt::transparent);
        window.render(&canvas);

        // 内容在窗口里居中：留边出现之后四个色块的相对位置与颜色都不变
        EXPECT_EQ(canvas.pixelColor(Pin::kShadowMargin + 10, Pin::kShadowMargin + 10), QColor(255, 0, 0)) << "留边把内容挤偏了";
        EXPECT_EQ(canvas.pixelColor(Pin::kShadowMargin + 30, Pin::kShadowMargin + 10), QColor(0, 255, 0));

        // 带子里有投影：贴内容那一圈比更外的一圈深（亮度小）
        const auto near = canvas.pixelColor(Pin::kShadowMargin - 2, Pin::kShadowMargin + 20).lightness();
        const auto far  = canvas.pixelColor(2, Pin::kShadowMargin + 20).lightness();
        EXPECT_LT(near, far) << "带子里没有「贴内容深、往外淡」的投影";

        window.setShadowEnabled(false);
        EXPECT_EQ(window.size(), QSize(40, 40)) << "关掉之后窗口没回到内容大小";
    }

    /**
     * @brief 钉住「窗口阴影」菜单项：勾选态就是当前读数，点一下即开即关
     */
    TEST_F(PinWindowFixture, ShadowMenuToggleFlipsTheState)
    {
        auto  &window = makeWindow();
        QMenu *menu   = window.buildContextMenu();
        auto  *shadow = findMenuAction(*menu, QString::fromUtf8("窗口阴影"));
        ASSERT_NE(shadow, nullptr) << "右键菜单里没有「窗口阴影」这一项";
        EXPECT_FALSE(shadow->isChecked()) << "默认该是关：已有的贴图不该凭空多出一圈";
        shadow->trigger();
        EXPECT_TRUE(window.shadowEnabled()) << "点了菜单没开";
        EXPECT_TRUE(shadow->isChecked());

        shadow->trigger();
        EXPECT_FALSE(window.shadowEnabled()) << "再点一次没关";
        EXPECT_FALSE(shadow->isChecked());
        delete menu;
    }

} // namespace GyanisScribe
