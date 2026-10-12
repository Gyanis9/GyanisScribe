// 贴图分组的直测：哪张在哪一组、切组时谁显谁隐、活动组怎么落账。
// 判据只走公开读数（isConcealed、activeGroup、groupsInUse 与写下的布局文本），不读私有成员。

#include "Pin/IPinWindow.h"
#include "Pin/PinWindow.h"

#include "PinTestAssets.h"
#include "Pin/PinLayout.h"
#include "Pin/PinManager.h"

#include "Platform/IPlatformIntegration.h"
#include "Settings/SettingsKeys.h"
#include "Settings/SettingsStore.h"

#include <gtest/gtest.h>

#include <QApplication>
#include <QColor>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QImage>
#include <QKeyEvent>
#include <QMimeData>
#include <QPoint>

#include <filesystem>
#include <format>
#include <memory>
#include <optional>
#include <string>

namespace GyanisScribe
{
    namespace
    {
        std::unique_ptr<QApplication> application;

        constexpr int kImageWidth  = 32;
        constexpr int kImageHeight = 24;

        /// 小图一张：分组判据不看内容，只看谁在桌上
        QImage makeImage()
        {
            QImage image{kImageWidth, kImageHeight, QImage::Format_ARGB32};
            image.fill(QColor{90, 140, 210});
            return image;
        }

        class PinManagerFixture : public ::testing::Test
        {
        protected:
            static void SetUpTestSuite()
            {
                // argv 与 argc 的缓冲要有静态存储期：Qt 只存指针不拷贝
                static int   argumentCount  = 1;
                static char  argumentZero[] = "TestPinManager";
                static char *arguments[]    = {argumentZero, nullptr};
                application                 = std::make_unique<QApplication>(argumentCount, arguments);
            }

            static void TearDownTestSuite()
            {
                application.reset();
            }

            void SetUp() override
            {
                // 每条用例一份 ini：ctest 并行时同名的键会互相盖掉
                const auto caseName = std::string_view(::testing::UnitTest::GetInstance()->current_test_info()->name());
                m_iniFile           = std::filesystem::temp_directory_path() / std::format("GyanisScribe-PinManager-{}.ini", caseName);
                std::error_code ignored;
                std::filesystem::remove(m_iniFile, ignored);

                m_settings    = std::make_unique<Settings::SettingsStore>(m_iniFile);
                m_integration = Platform::createPlatformIntegration();
            }

            void TearDown() override
            {
                m_pins.reset();
                m_integration.reset();
                m_settings.reset();
                std::error_code ignored;
                std::filesystem::remove(m_iniFile, ignored);
            }

            [[nodiscard]] Pin::PinManager &makeManager()
            {
                m_pins = std::make_unique<Pin::PinManager>(*m_settings, *m_integration);
                return *m_pins;
            }

            [[nodiscard]] Settings::ISettingsStore &settings() const
            {
                return *m_settings;
            }

            [[nodiscard]] Platform::IPlatformIntegration &integration() const
            {
                return *m_integration;
            }

        private:
            std::filesystem::path                           m_iniFile;     ///< 本用例专用的设置文件
            std::unique_ptr<Settings::SettingsStore>        m_settings;    ///< 布局的读写处
            std::unique_ptr<Platform::IPlatformIntegration> m_integration; ///< 贴图窗要的平台能力
            std::unique_ptr<Pin::PinManager>                m_pins;        ///< 被测管理器
        };

        /// 这张贴图此刻在桌上吗：显隐只有 setConcealed 一个出口，判据就直接读它
        [[nodiscard]] bool isOnDesk(const Pin::IPinWindow *pin)
        {
            return pin != nullptr && !pin->isConcealed();
        }
    } // namespace

    /**
     * @brief 钉住分组的核心语义：移到别的组＝从这一组收起来，切过去才看得见
     * @details 若「移过去」不收起，那张图会继续挡在当前组的画面上，用户以为菜单没生效；
     *          若「切组」不重算显隐，两组混在一起，「分组」就只是个没有效果的标签。
     */
    TEST_F(PinManagerFixture, MovingAndSwitchingTradesWhichPinsAreOnTheDesk)
    {
        auto &pins = makeManager();
        pins.addPin(makeImage(), QPoint(10, 10));
        pins.addPin(makeImage(), QPoint(60, 60));
        pins.addPin(makeImage(), QPoint(120, 120));

        EXPECT_EQ(pins.activeGroup(), 1) << "新贴图都落在第一组，活动组却不是它";
        EXPECT_TRUE(isOnDesk(pins.pinAt(0)) && isOnDesk(pins.pinAt(1)) && isOnDesk(pins.pinAt(2))) << "三张都该在桌上";

        const auto moved = pins.movePinToGroup(2, 2);
        ASSERT_TRUE(moved.has_value()) << moved.error().message;
        EXPECT_TRUE(isOnDesk(pins.pinAt(0)));
        EXPECT_TRUE(isOnDesk(pins.pinAt(1)));
        EXPECT_FALSE(isOnDesk(pins.pinAt(2))) << "移出当前组的贴图没有收起来：它还会挡着这一组的画面";
        EXPECT_EQ(pins.groupsInUse(), (QVector<int>{1, 2})) << "分组清单不对，切换就会走错";

        const auto switched = pins.switchToNextGroup();
        ASSERT_TRUE(switched.has_value()) << switched.error().message;
        EXPECT_EQ(*switched, 2) << "只有两组时下一组就是第二组";
        EXPECT_FALSE(isOnDesk(pins.pinAt(0))) << "切组后上一组的贴图还在桌上";
        EXPECT_FALSE(isOnDesk(pins.pinAt(1)));
        EXPECT_TRUE(isOnDesk(pins.pinAt(2))) << "切到第二组后那张该露脸";

        const auto wrapped = pins.switchToNextGroup();
        ASSERT_TRUE(wrapped.has_value()) << wrapped.error().message;
        EXPECT_EQ(*wrapped, 1) << "两组之间该来回切，而不是在第二组上停住";
        EXPECT_TRUE(isOnDesk(pins.pinAt(0)));
        EXPECT_FALSE(isOnDesk(pins.pinAt(2)));
    }

    /**
     * @brief 钉住只有一组时的拒绝面：要说清怎么多出一组，而不是按下去毫无动静
     */
    TEST_F(PinManagerFixture, SingleGroupSwitchTellsHowToMakeAnother)
    {
        auto &pins = makeManager();
        pins.addPin(makeImage(), QPoint(0, 0));

        const auto switched = pins.switchToNextGroup();
        ASSERT_FALSE(switched.has_value());
        EXPECT_EQ(switched.error().code, Core::ErrorCode::InvalidArgument);
        EXPECT_NE(switched.error().message.find("新建分组"), std::string::npos) << "拒绝文案要给出多出一组的做法，实际：" << switched.error().message;

        EXPECT_EQ(pins.activeGroup(), 1) << "切不动时活动组被改掉了";
        EXPECT_TRUE(isOnDesk(pins.pinAt(0))) << "切不动的那一下把贴图收起来了";
    }

    /**
     * @brief 钉住全局收起与分组互不干扰：收起再放下，别组的贴图不会跟着露脸
     * @details 两条判据各盯一侧：全局收起着切组，切过去那一组也不许露脸（用户说的是「桌上什么都别留」）；
     *          摆回时只放当前组的，别组的不许跟着出来（那等于把用户分好的摊子搅乱）。
     */
    TEST_F(PinManagerFixture, GlobalConcealNeverRevealsOtherGroups)
    {
        auto &pins = makeManager();
        pins.addPin(makeImage(), QPoint(0, 0));
        pins.addPin(makeImage(), QPoint(50, 50));
        ASSERT_TRUE(pins.movePinToGroup(1, 2).has_value());

        EXPECT_EQ(pins.setAllPinsConcealed(true), 1) << "当前组里只有一张，该收起一张";
        EXPECT_FALSE(isOnDesk(pins.pinAt(0)));
        EXPECT_FALSE(isOnDesk(pins.pinAt(1)));

        const auto switched = pins.switchToNextGroup();
        ASSERT_TRUE(switched.has_value()) << switched.error().message;
        EXPECT_FALSE(isOnDesk(pins.pinAt(1))) << "全局收起着，切过去的那张却露了脸";

        EXPECT_EQ(pins.setAllPinsConcealed(false), 1) << "摆回该只影响当前组的那一张";
        EXPECT_TRUE(isOnDesk(pins.pinAt(1)));
        EXPECT_FALSE(isOnDesk(pins.pinAt(0))) << "摆回把别组的贴图也放了出来";
    }

    /**
     * @brief 钉住「哪张在哪组」与「现在看哪一组」都进配置：换一个管理器实例也回得来
     * @details 不记分组，重启后所有贴图挤回第一组；不记活动组，则停在第一组，
     *          别的组的贴图看上去像丢了。
     */
    TEST_F(PinManagerFixture, GroupAndActiveGroupSurviveARestart)
    {
        {
            auto &pins = makeManager();
            pins.addPin(makeImage(), QPoint(20, 20));
            pins.addPin(makeImage(), QPoint(200, 40));
            ASSERT_TRUE(pins.movePinToGroup(1, 2).has_value());
            const auto switched = pins.switchToNextGroup();
            ASSERT_TRUE(switched.has_value()) << switched.error().message;
            ASSERT_EQ(pins.activeGroup(), 2);
            ASSERT_TRUE(pins.saveLayout().has_value()) << "布局没写下去";
        }

        // 布局文本是「哪张在哪组」的唯一凭据：从配置里现读现解一遍
        const auto text   = settings().stringValue(Settings::kPinLayout, QString());
        const auto states = Pin::parseLayout(text.toStdString());
        ASSERT_TRUE(states.has_value()) << "写下的布局读不回来：" << states.error().message;
        ASSERT_EQ(states->size(), 2);
        EXPECT_EQ((*states)[0].group, 1);
        EXPECT_EQ((*states)[1].group, 2) << "第二张的组号没写进布局";

        auto      &reopened = makeManager();
        const auto restored = reopened.restoreLayout();
        ASSERT_TRUE(restored.has_value()) << restored.error().message;
        ASSERT_EQ(*restored, 2);
        EXPECT_EQ(reopened.activeGroup(), 2) << "活动组没记：重启后停在第一组，第二组那张就不见了";
        EXPECT_TRUE(isOnDesk(reopened.pinAt(1))) << "第二组的那张在重启后没露脸";
        EXPECT_FALSE(isOnDesk(reopened.pinAt(0))) << "第一组的那张不该同时在桌上";
    }

    /**
     * @brief 钉住置顶、穿透与「收起全部」都跟着布局过夜
     * @details 这几个开关是用户按自己的桌面摆出来的：只记几何不记它们，重启后摆回来的是「另一个桌面」。
     *          穿透那一格按平台能力门控：没这能力的机器上恢复时会记一条日志并按不穿透继续，不能拿它判红。
     */
    TEST_F(PinManagerFixture, TopmostThroughAndConcealSurviveARestart)
    {
        const bool throughSupported = integration().capabilities().clickThrough;

        {
            auto &pins = makeManager();
            pins.addPin(makeImage(), QPoint(10, 10));
            pins.addPin(makeImage(), QPoint(60, 60));

            auto *first = pins.pinAt(0);
            ASSERT_NE(first, nullptr);
            first->setAlwaysOnTop(false);
            if (throughSupported)
            {
                ASSERT_TRUE(first->setMouseThrough(true).has_value()) << "前提没造出来：这台机器支持穿透";
            }
            ASSERT_EQ(pins.setAllPinsConcealed(true), 2) << "两张都该被收起";
            ASSERT_TRUE(pins.saveLayout().has_value()) << "布局没写下去";
        }

        auto      &reopened = makeManager();
        const auto restored = reopened.restoreLayout();
        ASSERT_TRUE(restored.has_value()) << restored.error().message;
        ASSERT_EQ(*restored, 2);

        EXPECT_FALSE(reopened.pinAt(0)->isAlwaysOnTop()) << "不置顶那一档没跟着布局回来";
        EXPECT_TRUE(reopened.pinAt(1)->isAlwaysOnTop()) << "默认置顶的那张被带成了不置顶";
        EXPECT_TRUE(reopened.pinAt(0)->isConcealed()) << "退出时收着的一桌，回来该还是收着（托盘那句「摆回全部贴图」是出口）";
        EXPECT_TRUE(reopened.pinAt(1)->isConcealed()) << "同一桌的两张该是同一档显隐";
        if (throughSupported)
        {
            EXPECT_TRUE(reopened.pinAt(0)->isMouseThrough()) << "穿透那一档没跟着布局回来";
            EXPECT_FALSE(reopened.pinAt(1)->isMouseThrough()) << "没设穿透的那张被带成了穿透";
        }
    }

    /**
     * @brief 钉住动图贴图过夜回来还是动图：来源文件跟着布局走，重启后重新接上
     * @details 只记像素是不够的——缓存里落下的那一张 PNG 只是一个瞬间，重启后要能接着播，
     *          靠的是布局里那条来源路径。
     */
    TEST_F(PinManagerFixture, AnimatedPinStaysAnimatedAfterARestart)
    {
        const auto gifPath = PinTestAssets::writeTwoFrameGif(std::filesystem::temp_directory_path() / "GyanisScribe-PinManager-two-frames.gif");
        ASSERT_FALSE(gifPath.isEmpty()) << "素材没落到临时文件上";

        {
            auto &pins = makeManager();
            pins.addPin(makeImage(), QPoint(30, 30), gifPath);
            ASSERT_TRUE(pins.saveLayout().has_value()) << "布局没写下去";
        }

        auto      &reopened = makeManager();
        const auto restored = reopened.restoreLayout();
        ASSERT_TRUE(restored.has_value()) << restored.error().message;
        ASSERT_EQ(reopened.count(), 1);

        auto *pin = dynamic_cast<Pin::PinWindow *>(reopened.pinAt(0));
        ASSERT_NE(pin, nullptr) << "管理器里的贴图不是 PinWindow，这一条判据接不上";
        EXPECT_TRUE(pin->isAnimated()) << "动图过夜回来变成了静图：布局里那条来源路径没用上";
        EXPECT_EQ(pin->state().sourceFile, gifPath) << "恢复之后来源路径又丢了";
    }

    /**
     * @brief 钉住活动组指向一个不存在的分组时回落到第一组
     * @details 那个组号是上一次会话留下的（那一组的图被删了、布局被清过）：不回落的后果是
     *          桌上一张都看不见，而用户手上没有任何线索说明为什么。
     */
    TEST_F(PinManagerFixture, ActiveGroupWithoutPinsFallsBackToTheFirstGroup)
    {
        {
            auto &pins = makeManager();
            pins.addPin(makeImage(), QPoint(30, 30));
            ASSERT_TRUE(pins.saveLayout().has_value());
        }
        // 手工把活动组改成一个没有贴图的组号：模拟「上一次会话留下的那个组已经空了」
        ASSERT_TRUE(settings().setValue(Settings::kPinActiveGroup, 7).has_value());
        settings().sync();

        auto      &reopened = makeManager();
        const auto restored = reopened.restoreLayout();
        ASSERT_TRUE(restored.has_value()) << restored.error().message;
        EXPECT_EQ(reopened.activeGroup(), 1) << "活动组停在一个不存在的组上，桌上会一张都看不见";
        EXPECT_TRUE(isOnDesk(reopened.pinAt(0))) << "回落之后那张该露脸";
    }

    /**
     * @brief 钉住标注里的「另贴一张」在管理器这一层的落点：相对源贴图右下偏一格转发出去
     * @details 新贴图摆在哪是管理器算的账：完全叠在原件上时，用户看到的是「贴了没反应」。
     */
    TEST_F(PinManagerFixture, PinAnotherFromAnnotationForwardsWithAnOffset)
    {
        auto &pins = makeManager();
        pins.addPin(makeImage(), QPoint(100, 100));
        auto *window = dynamic_cast<Pin::PinWindow *>(pins.pinAt(0));
        ASSERT_NE(window, nullptr) << "管理器里的贴图不是 PinWindow，这一条判据接不上";

        std::optional<QPoint> at;
        QObject::connect(&pins, &Pin::PinManager::pinAnotherRequested, [&at](const QImage &, const QPoint &globalTopLeft) { at = globalTopLeft; });

        window->startAnnotation();
        QKeyEvent press{QEvent::KeyPress, Qt::Key_D, Qt::ControlModifier};
        QApplication::sendEvent(window, &press);

        ASSERT_TRUE(at.has_value()) << "「另贴一张」没从管理器转发出去";
        EXPECT_EQ(*at, window->globalRect().topLeft() + QPoint(24, 24)) << "新贴图该落在源贴图右下偏一格的位置";
    }

    /**
     * @brief 钉住拖到贴图上的内容从管理器转发出去：载荷原样、落点是全局坐标
     * @details 与「另贴一张」同一条形状：窗口、管理器都只报事实，渲染与建帖是界面层的事。
     */
    TEST_F(PinManagerFixture, DropOnAPinIsForwardedWithThePayloadAndPoint)
    {
        auto &pins = makeManager();
        pins.addPin(makeImage(), QPoint(50, 60));
        auto *window = dynamic_cast<Pin::PinWindow *>(pins.pinAt(0));
        ASSERT_NE(window, nullptr) << "管理器里的贴图不是 PinWindow，这一条判据接不上";
        window->move(50, 60);

        QMimeData mime;
        mime.setText(QStringLiteral("dropped text"));

        const QMimeData      *seen = nullptr;
        std::optional<QPoint> at;
        QObject::connect(&pins, &Pin::PinManager::pinDropRequested,
                         [&](const QMimeData *data, const QPoint &globalTopLeft)
                         {
                             seen = data;
                             at   = globalTopLeft;
                         });

        // 先收下 DragEnter：QApplication::notify 只把 Drop 投给收下过 DragEnter 的窗口
        QDragEnterEvent enter{QPoint{4, 6}, Qt::CopyAction, &mime, Qt::LeftButton, Qt::NoModifier};
        QApplication::sendEvent(window, &enter);
        ASSERT_TRUE(enter.isAccepted()) << "拖动进入没收下，后面的落下无从谈起";

        QDropEvent drop{QPointF{4.0, 6.0}, Qt::CopyAction, &mime, Qt::LeftButton, Qt::NoModifier};
        QApplication::sendEvent(window, &drop);

        EXPECT_EQ(seen, &mime) << "拖放载荷没转发给界面层，没人能替它渲染";
        ASSERT_TRUE(at.has_value());
        EXPECT_EQ(*at, QPoint(54, 66)) << "落点没换算回全局坐标";
    }

} // namespace GyanisScribe
