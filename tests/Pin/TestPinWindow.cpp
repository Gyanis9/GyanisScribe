// 贴图窗的交互直测：键盘与双击改的是同一批状态量，判据取公开读数（缩放/角度/透明度），
// 不读私有成员——将来把几何搬到别处，这几条仍然判的是用户看得见的那一件事。
// 单测几何数学的部分住在 TestPinTransform.cpp，重启恢复住在 TestPinLayout.cpp，这里只补窗口本身那一格。

#include "Pin/PinWindow.h"

#include "Core/Paths.h"
#include "Core/Types.h"
#include "Platform/IPlatformIntegration.h"
#include "Platform/PlatformCapabilities.h"
#include "Settings/SettingsKeys.h"
#include "Settings/SettingsStore.h"

#include <gtest/gtest.h>

#include <QAction>
#include <QApplication>
#include <QImage>
#include <QKeyEvent>
#include <QMenu>
#include <QMouseEvent>
#include <QPoint>
#include <QSize>

#include <filesystem>
#include <cmath>
#include <format>
#include <memory>
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

} // namespace GyanisScribe
