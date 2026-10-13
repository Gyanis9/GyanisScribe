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
#include <QAction>
#include <QKeyEvent>
#include <QMenu>
#include <QMouseEvent>
#include <QWheelEvent>
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
        /// 按文案找菜单项：用文案定位比记序号稳
        [[nodiscard]] QAction *findAction(QMenu &menu, const QString &text)
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

    /**
     * @brief 钉住 Ctrl+点选：切进/切出选中集，张数读数推给每一张
     */
    TEST_F(PinManagerFixture, ControllerClickTogglesTheSelectionAndPublishesTheCount)
    {
        auto &pins  = makeManager();
        auto &first = static_cast<Pin::PinWindow &>(pins.addPin(makeImage(), QPoint(10, 10)));
        auto &other = static_cast<Pin::PinWindow &>(pins.addPin(makeImage(), QPoint(80, 80)));

        const auto controlClick = [](QWidget &target)
        {
            QMouseEvent press{QEvent::MouseButtonPress, QPointF{5.0, 5.0}, QPointF{5.0, 5.0}, Qt::LeftButton, Qt::LeftButton, Qt::ControlModifier};
            QApplication::sendEvent(&target, &press);
        };

        EXPECT_EQ(pins.selectedPinCount(), 0);
        controlClick(first);
        EXPECT_TRUE(first.isSelected()) << "Ctrl+点一下没把它切进选中集";
        EXPECT_EQ(pins.selectedPinCount(), 1);
        EXPECT_EQ(first.selectionCount(), 1) << "张数读数没推给自己";

        controlClick(other);
        EXPECT_EQ(pins.selectedPinCount(), 2);
        EXPECT_EQ(first.selectionCount(), 2) << "张数读数没推给先前选中的那一张";

        controlClick(first);
        EXPECT_FALSE(first.isSelected()) << "再点一次该切出去";
        EXPECT_TRUE(other.isSelected());
        EXPECT_EQ(pins.selectedPinCount(), 1);
    }

    /**
     * @brief 钉住整组拖动：拖着选中集里的任意一张，其余几张按同一位移跟着挪
     */
    TEST_F(PinManagerFixture, DraggingASelectedPinCarriesTheWholeSelection)
    {
        auto &pins = makeManager();
        pins.addPin(makeImage(), QPoint(10, 10));
        pins.addPin(makeImage(), QPoint(80, 80));
        auto *first = pins.pinAt(0);
        auto *other = pins.pinAt(1);
        ASSERT_NE(first, nullptr);
        ASSERT_NE(other, nullptr);

        pins.togglePinSelection(static_cast<Pin::PinWindow &>(*first));
        pins.togglePinSelection(static_cast<Pin::PinWindow &>(*other));
        ASSERT_EQ(pins.selectedPinCount(), 2);

        const QPoint otherBefore = other->globalRect().topLeft();
        const QPoint firstBefore = first->globalRect().topLeft();

        // 从窗口正中起手：角与边中点上摆着缩放手柄，按在那里会变成「改大小」而不是「挪位置」
        auto       *target = static_cast<Pin::PinWindow *>(first);
        QMouseEvent press{QEvent::MouseButtonPress, QPointF{16.0, 16.0}, QPointF{16.0, 16.0}, Qt::LeftButton, Qt::LeftButton, Qt::NoModifier};
        QApplication::sendEvent(target, &press);
        QMouseEvent move{QEvent::MouseMove, QPointF{36.0, 26.0}, QPointF{36.0, 26.0}, Qt::NoButton, Qt::LeftButton, Qt::NoModifier};
        QApplication::sendEvent(target, &move);
        QMouseEvent release{QEvent::MouseButtonRelease, QPointF{36.0, 26.0}, QPointF{36.0, 26.0}, Qt::LeftButton, Qt::NoButton, Qt::NoModifier};
        QApplication::sendEvent(target, &release);

        const QPoint moved = first->globalRect().topLeft() - firstBefore;
        ASSERT_NE(moved, QPoint(0, 0)) << "这一拖本身没挪动，后面的对照没有基准";
        EXPECT_EQ(other->globalRect().topLeft(), otherBefore + moved) << "选中集里的另一张没按同一位移跟着挪";
    }

    /**
     * @brief 钉住整组缩放：在一张上滚一格，选中集里其余的按同一倍率跟着缩
     */
    TEST_F(PinManagerFixture, WheelScaleFollowsTheSelection)
    {
        auto &pins = makeManager();
        pins.addPin(makeImage(), QPoint(10, 10));
        pins.addPin(makeImage(), QPoint(80, 80));
        auto *first = pins.pinAt(0);
        auto *other = pins.pinAt(1);
        ASSERT_NE(first, nullptr);
        ASSERT_NE(other, nullptr);

        pins.togglePinSelection(static_cast<Pin::PinWindow &>(*first));
        pins.togglePinSelection(static_cast<Pin::PinWindow &>(*other));

        QWheelEvent wheel{QPointF{5.0, 5.0}, QPointF{5.0, 5.0}, QPoint{0, 0}, QPoint{0, 120}, Qt::NoButton, Qt::NoModifier, Qt::NoScrollPhase, false};
        QApplication::sendEvent(static_cast<Pin::PinWindow *>(first), &wheel);

        EXPECT_GT(first->scale(), 1.0) << "在这一张上滚一格它自己没缩";
        EXPECT_NEAR(other->scale(), first->scale(), 0.0001) << "选中集里的另一张没按同一倍率跟着缩";
    }

    /**
     * @brief 钉住菜单里的状态开关对整组生效：多选时点「保持置顶」，整组一起翻
     */
    TEST_F(PinManagerFixture, BatchMenuAppliesToTheWholeSelection)
    {
        auto &pins = makeManager();
        pins.addPin(makeImage(), QPoint(10, 10));
        pins.addPin(makeImage(), QPoint(80, 80));
        auto *first = pins.pinAt(0);
        auto *other = pins.pinAt(1);
        ASSERT_NE(first, nullptr);
        ASSERT_NE(other, nullptr);

        pins.togglePinSelection(static_cast<Pin::PinWindow &>(*first));
        pins.togglePinSelection(static_cast<Pin::PinWindow &>(*other));
        ASSERT_TRUE(first->isAlwaysOnTop());

        auto *menu      = static_cast<Pin::PinWindow *>(first)->buildContextMenu();
        auto *pinAction = findAction(*menu, QString::fromUtf8("保持置顶"));
        ASSERT_NE(pinAction, nullptr) << "菜单里没有「保持置顶」这一项";
        pinAction->trigger();
        delete menu;

        EXPECT_FALSE(first->isAlwaysOnTop()) << "发起的那一张没跟着翻";
        EXPECT_FALSE(other->isAlwaysOnTop()) << "选中集里的另一张没跟着翻";
    }

    /**
     * @brief 钉住整组关闭：多选时按关闭＝整组一起关
     */
    TEST_F(PinManagerFixture, BatchCloseClosesTheWholeSelection)
    {
        auto &pins = makeManager();
        pins.addPin(makeImage(), QPoint(10, 10));
        pins.addPin(makeImage(), QPoint(80, 80));
        auto *first = pins.pinAt(0);
        auto *other = pins.pinAt(1);
        ASSERT_NE(first, nullptr);
        ASSERT_NE(other, nullptr);

        pins.togglePinSelection(static_cast<Pin::PinWindow &>(*first));
        pins.togglePinSelection(static_cast<Pin::PinWindow &>(*other));

        auto *menu      = static_cast<Pin::PinWindow *>(first)->buildContextMenu();
        auto *closeItem = findAction(*menu, QString::fromUtf8("关闭"));
        ASSERT_NE(closeItem, nullptr);
        // 菜单是那一张的子对象：整组关闭把宿主也析构了，菜单跟着一起没——这里不再自己 delete
        closeItem->trigger();

        EXPECT_EQ(pins.count(), 0) << "多选里按关闭只关掉了这一张";
    }

    /**
     * @brief 钉住 Esc 的两段含义：多选里先清选择，再按一次才关这一张
     */
    TEST_F(PinManagerFixture, EscapeClearsTheSelectionBeforeClosing)
    {
        auto &pins = makeManager();
        pins.addPin(makeImage(), QPoint(10, 10));
        pins.addPin(makeImage(), QPoint(80, 80));
        auto *first = pins.pinAt(0);
        auto *other = pins.pinAt(1);
        ASSERT_NE(first, nullptr);
        ASSERT_NE(other, nullptr);

        pins.togglePinSelection(static_cast<Pin::PinWindow &>(*first));
        pins.togglePinSelection(static_cast<Pin::PinWindow &>(*other));

        const auto escape = [](QWidget &target)
        {
            QKeyEvent event{QEvent::KeyPress, Qt::Key_Escape, Qt::NoModifier};
            QApplication::sendEvent(&target, &event);
        };

        escape(*static_cast<Pin::PinWindow *>(first));
        EXPECT_EQ(pins.selectedPinCount(), 0) << "第一次 Esc 没清选择";
        EXPECT_EQ(pins.count(), 2) << "第一次 Esc 把贴图也关了：关闭是一组一起的动作，得再按一次";

        escape(*static_cast<Pin::PinWindow *>(first));
        EXPECT_EQ(pins.count(), 1) << "第二次 Esc 该关掉这一张";
    }

    /**
     * @brief 钉住两处会清选择的时机：切组与整桌收起——留着的选中集会指向看不见的那几张
     */
    TEST_F(PinManagerFixture, SwitchingGroupsOrConcealingAllClearsTheSelection)
    {
        auto &pins = makeManager();
        pins.addPin(makeImage(), QPoint(10, 10));
        pins.addPin(makeImage(), QPoint(80, 80));

        pins.togglePinSelection(static_cast<Pin::PinWindow &>(*pins.pinAt(0)));
        pins.togglePinSelection(static_cast<Pin::PinWindow &>(*pins.pinAt(1)));
        ASSERT_EQ(pins.selectedPinCount(), 2);

        // 建第二组再切过去：选中集该清空
        ASSERT_TRUE(pins.movePinToGroup(1, 2).has_value());
        ASSERT_TRUE(pins.switchToNextGroup().has_value());
        EXPECT_EQ(pins.selectedPinCount(), 0) << "切组之后选中集还留着：那几张已经不在屏上了";

        pins.togglePinSelection(static_cast<Pin::PinWindow &>(*pins.pinAt(0)));
        ASSERT_EQ(pins.selectedPinCount(), 1);
        static_cast<void>(pins.setAllPinsConcealed(true));
        EXPECT_EQ(pins.selectedPinCount(), 0) << "整桌收起之后选中集还留着";
    }

    /**
     * @brief 钉住 Solo 的本义：开着时只有主角在屏上，关掉其余几张原样回来
     */
    TEST_F(PinManagerFixture, SoloHidesTheOthersAndTheToggleBringsThemBack)
    {
        auto &pins = makeManager();
        pins.addPin(makeImage(), QPoint(10, 10));
        pins.addPin(makeImage(), QPoint(80, 80));
        pins.addPin(makeImage(), QPoint(150, 150));
        auto *first = pins.pinAt(0);
        ASSERT_NE(first, nullptr);

        pins.setPinSolo(static_cast<Pin::PinWindow &>(*first), true);
        EXPECT_TRUE(pins.pinIsSolo(static_cast<const Pin::PinWindow &>(*first)));
        EXPECT_TRUE(static_cast<Pin::PinWindow *>(pins.pinAt(0))->isSolo()) << "主角态没推给窗口（菜单项的勾选态照它走）";
        EXPECT_FALSE(static_cast<Pin::PinWindow *>(pins.pinAt(1))->isSolo()) << "非主角被推成了主角";
        EXPECT_TRUE(isOnDesk(pins.pinAt(0))) << "主角自己该留在屏上";
        EXPECT_FALSE(isOnDesk(pins.pinAt(1))) << "其余的没收起来，Solo 没生效";
        EXPECT_FALSE(isOnDesk(pins.pinAt(2)));

        pins.setPinSolo(static_cast<Pin::PinWindow &>(*first), false);
        EXPECT_FALSE(pins.pinIsSolo(static_cast<const Pin::PinWindow &>(*first)));
        EXPECT_TRUE(isOnDesk(pins.pinAt(0)) && isOnDesk(pins.pinAt(1)) && isOnDesk(pins.pinAt(2))) << "关掉 Solo 之后其余几张没回来";
    }

    /**
     * @brief 钉住主角被关掉时 Solo 自动落掉：其余几张该回来
     */
    TEST_F(PinManagerFixture, SoloEndsWhenItsPinIsClosed)
    {
        auto &pins = makeManager();
        pins.addPin(makeImage(), QPoint(10, 10));
        pins.addPin(makeImage(), QPoint(80, 80));
        auto *solo = pins.pinAt(0);
        ASSERT_NE(solo, nullptr);
        pins.setPinSolo(static_cast<Pin::PinWindow &>(*solo), true);
        ASSERT_FALSE(isOnDesk(pins.pinAt(1)));

        static_cast<Pin::PinWindow *>(solo)->close();
        emit static_cast<Pin::PinWindow *>(solo)->closedByUser();

        EXPECT_FALSE(pins.pinIsSolo(static_cast<const Pin::PinWindow &>(*pins.pinAt(0)))) << "主角没了 Solo 还挂着";
        EXPECT_TRUE(isOnDesk(pins.pinAt(0))) << "主角被关掉之后其余几张没回来";
    }

    /**
     * @brief 钉住新贴一张时 Solo 自动落掉：新贴的自己该看得见
     */
    TEST_F(PinManagerFixture, SoloEndsWhenANewPinArrives)
    {
        auto &pins = makeManager();
        pins.addPin(makeImage(), QPoint(10, 10));
        pins.addPin(makeImage(), QPoint(80, 80));
        pins.setPinSolo(static_cast<Pin::PinWindow &>(*pins.pinAt(0)), true);
        pins.addPin(makeImage(), QPoint(150, 150));

        EXPECT_TRUE(isOnDesk(pins.pinAt(2))) << "新贴的一张被 Solo 挡在屏外";
        EXPECT_TRUE(isOnDesk(pins.pinAt(1))) << "新贴一张之后其余几张没回来";
    }

    /**
     * @brief 钉住切组时 Solo 自动落掉：换的是一桌人
     */
    TEST_F(PinManagerFixture, SoloEndsOnGroupSwitch)
    {
        auto &pins = makeManager();
        pins.addPin(makeImage(), QPoint(10, 10));
        pins.addPin(makeImage(), QPoint(80, 80));
        pins.setPinSolo(static_cast<Pin::PinWindow &>(*pins.pinAt(0)), true);
        ASSERT_TRUE(pins.movePinToGroup(1, 2).has_value());
        ASSERT_TRUE(pins.switchToNextGroup().has_value());
        EXPECT_FALSE(pins.pinIsSolo(static_cast<const Pin::PinWindow &>(*pins.pinAt(0)))) << "切组之后 Solo 还挂着";
    }

    /**
     * @brief 钉住 Solo 与选中集互不打架：开 Solo 会把选中集清掉（其余几张已经不在屏上）
     */
    TEST_F(PinManagerFixture, SoloClearsTheSelection)
    {
        auto &pins = makeManager();
        pins.addPin(makeImage(), QPoint(10, 10));
        pins.addPin(makeImage(), QPoint(80, 80));
        pins.togglePinSelection(static_cast<Pin::PinWindow &>(*pins.pinAt(0)));
        pins.togglePinSelection(static_cast<Pin::PinWindow &>(*pins.pinAt(1)));
        ASSERT_EQ(pins.selectedPinCount(), 2);

        pins.setPinSolo(static_cast<Pin::PinWindow &>(*pins.pinAt(0)), true);
        EXPECT_EQ(pins.selectedPinCount(), 0) << "开 Solo 之后选中集还留着：那几张已经不在屏上了";
    }

    /**
     * @brief 钉住替换内容只换像素：逻辑尺寸跟着新图走，倍率与摆的中心都不动
     * @details 按 `Ctrl+V` 要的是「这张图换了」，不是「这张贴图挪了位、又回到 1:1」——
     *          尺寸、倍率、中心三样里只有尺寸该动。
     */
    TEST_F(PinManagerFixture, ReplacingContentSwapsTheImageAndKeepsWhereItSits)
    {
        auto &pins = makeManager();
        pins.addPin(makeImage(), QPoint(40, 40));
        auto *pin = pins.pinAt(0);
        ASSERT_NE(pin, nullptr);
        pin->setScale(2.0);
        const auto  centerBefore = pin->globalRect().center();
        const QSize replacementSize{7, 3};
        ASSERT_GT(pin->state().logicalSize.width(), replacementSize.width()) << "前提不对：原图得比替换那张宽，不然判不出换没换";

        QImage replacement{replacementSize, QImage::Format_ARGB32};
        replacement.fill(QColor{10, 200, 30});
        const auto replaced = pins.replacePinContent(0, replacement);
        ASSERT_TRUE(replaced.has_value()) << replaced.error().message;

        EXPECT_EQ(pin->state().logicalSize, QSizeF{replacementSize}) << "逻辑尺寸该跟着新图走";
        EXPECT_NEAR(pin->state().scale, 2.0, 1e-9) << "换内容不该动缩放倍率";
        // 尺寸变了而中心不动：applyGeometry 就是按中心重算矩形的，取整会带来一像素以内的差
        EXPECT_NEAR(pin->globalRect().center().x(), centerBefore.x(), 1.0) << "换内容不该把贴图挪走";
        EXPECT_NEAR(pin->globalRect().center().y(), centerBefore.y(), 1.0) << "换内容不该把贴图挪走";
    }

    /**
     * @brief 钉住替换的两道拒绝：动图不换（下一帧会盖回来），不在册的索引直接拒
     * @details 两道都回中文原因与出路，且原来那份像素一律不动——「替换失败」不该把贴图刷白。
     */
    TEST_F(PinManagerFixture, ReplacingContentRefusesAnimatedPinsAndUnknownIndexes)
    {
        // 素材文件名按用例名给：ctest 把每条用例当独立进程起，共用一个路径会互相盖掉
        const auto gifPath = PinTestAssets::writeTwoFrameGif(std::filesystem::temp_directory_path() / "GyanisScribe-PinManager-replace-refuses-animated.gif");
        ASSERT_FALSE(gifPath.isEmpty()) << "素材没落到临时文件上";

        auto &pins = makeManager();
        pins.addPin(makeImage(), QPoint(30, 30), gifPath);
        auto *pin = dynamic_cast<Pin::PinWindow *>(pins.pinAt(0));
        ASSERT_NE(pin, nullptr) << "管理器里的贴图不是 PinWindow，动图这一判据接不上";
        ASSERT_TRUE(pin->isAnimated()) << "前提不对：这一张没被当成动图接上";

        QImage replacement{7, 3, QImage::Format_ARGB32};
        replacement.fill(QColor{10, 200, 30});

        const auto refused = pins.replacePinContent(0, replacement);
        ASSERT_FALSE(refused.has_value()) << "动图被换掉了：下一帧一到就把它盖回去";
        EXPECT_EQ(refused.error().code, Core::ErrorCode::InvalidArgument);
        EXPECT_NE(refused.error().message.find("动图"), std::string::npos) << "拒绝文案要点名动图，实际：" << refused.error().message;
        EXPECT_NE(refused.error().message.find("静态图"), std::string::npos) << "拒绝文案要给出出路（先存成静态图），实际：" << refused.error().message;
        const QSizeF originalSize{kImageWidth, kImageHeight};
        EXPECT_EQ(pin->state().logicalSize, originalSize) << "被拒的那一次不该留下半份替换";

        const auto missing = pins.replacePinContent(pins.count(), replacement);
        ASSERT_FALSE(missing.has_value()) << "不在册的索引被放过了";
        EXPECT_EQ(missing.error().code, Core::ErrorCode::InvalidArgument);
    }

    /**
     * @brief 钉住 `Ctrl+A` 那一路只选在桌上的那几张
     * @details 收着的与在别组的那几张不在屏上，整组动作落在它们身上没人看得见，等于凭空改状态。
     */
    TEST_F(PinManagerFixture, SelectAllVisiblePinsPicksOnlyTheOnesOnTheDesk)
    {
        auto &pins = makeManager();
        pins.addPin(makeImage(), QPoint(10, 10));
        pins.addPin(makeImage(), QPoint(60, 60));
        pins.addPin(makeImage(), QPoint(120, 120));
        ASSERT_TRUE(pins.movePinToGroup(2, 2).has_value()) << "把第三张移到别组没成功";
        ASSERT_TRUE(isOnDesk(pins.pinAt(0)) && isOnDesk(pins.pinAt(1))) << "前两张该还在桌上";
        ASSERT_FALSE(isOnDesk(pins.pinAt(2))) << "移走的那张该从桌上收起";

        pins.selectAllVisiblePins();
        EXPECT_EQ(pins.selectedPinCount(), 2) << "全选只该挑在桌上的那两张";

        auto *first     = dynamic_cast<Pin::PinWindow *>(pins.pinAt(0));
        auto *concealed = dynamic_cast<Pin::PinWindow *>(pins.pinAt(2));
        ASSERT_NE(first, nullptr);
        ASSERT_NE(concealed, nullptr);
        EXPECT_TRUE(first->isSelected()) << "在桌上的那张没被选进集";
        EXPECT_FALSE(concealed->isSelected()) << "收着的那张被选进了集：整组动作会落到看不见的图上";

        pins.clearPinSelection();
        EXPECT_EQ(pins.selectedPinCount(), 0) << "全选之后清不干净，后面的整组操作就没有出口";
    }

} // namespace GyanisScribe
