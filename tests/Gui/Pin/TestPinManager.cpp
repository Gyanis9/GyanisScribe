// 贴图分组的直测：哪张在哪一组、切组时谁显谁隐、活动组怎么落账。
// 判据只走公开读数（isConcealed、activeGroup、groupsInUse 与写下的布局文本），不读私有成员。

#include "Core/Pin/IPinWindow.h"
#include "Gui/Pin/PinWindow.h"

#include "Base/Paths.h"
#include "Base/Text.h"
#include "PinTestAssets.h"
#include "Core/Pin/PinLayout.h"
#include "Gui/Pin/PinManager.h"

#include "Platform/IPlatformIntegration.h"
#include "Core/Settings/SettingsKeys.h"
#include "Core/Settings/SettingsStore.h"

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
#ifdef Q_OS_WIN
#include <windows.h>
#include <psapi.h>
#endif
#include <QVariant>

#include <filesystem>
#include <format>
#include <fstream>
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


        /// 本进程当前的工作集（字节）；报不出来时返回 0，调用方据此跳过（非 Windows 只能这样）
        std::size_t currentWorkingSetBytes()
        {
#ifdef Q_OS_WIN
            PROCESS_MEMORY_COUNTERS counters{};
            if (K32GetProcessMemoryInfo(GetCurrentProcess(), &counters, sizeof(counters)))
            {
                return static_cast<std::size_t>(counters.WorkingSetSize);
            }
#endif
            return 0;
        }

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

                m_settings    = std::make_unique<Core::SettingsStore>(m_iniFile);
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

            [[nodiscard]] Gui::PinManager &makeManager()
            {
                m_pins = std::make_unique<Gui::PinManager>(*m_settings, *m_integration);
                return *m_pins;
            }

            [[nodiscard]] Core::ISettingsStore &settings() const
            {
                return *m_settings;
            }

            [[nodiscard]] Platform::IPlatformIntegration &integration() const
            {
                return *m_integration;
            }

        private:
            std::filesystem::path                           m_iniFile;     ///< 本用例专用的设置文件
            std::unique_ptr<Core::SettingsStore>            m_settings;    ///< 布局的读写处
            std::unique_ptr<Platform::IPlatformIntegration> m_integration; ///< 贴图窗要的平台能力
            std::unique_ptr<Gui::PinManager>                m_pins;        ///< 被测管理器
        };

        /// 这张贴图此刻在桌上吗：显隐只有 setConcealed 一个出口，判据就直接读它
        [[nodiscard]] bool isOnDesk(const Core::IPinWindow *pin)
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
        EXPECT_EQ(switched.error().code, Base::ErrorCode::InvalidArgument);
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
        const auto text   = settings().stringValue(Core::kPinLayout, QString());
        const auto states = Core::parseLayout(text.toStdString());
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

        auto *pin = dynamic_cast<Gui::PinWindow *>(reopened.pinAt(0));
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
        ASSERT_TRUE(settings().setValue(Core::kPinActiveGroup, 7).has_value());
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
        auto *window = dynamic_cast<Gui::PinWindow *>(pins.pinAt(0));
        ASSERT_NE(window, nullptr) << "管理器里的贴图不是 PinWindow，这一条判据接不上";

        std::optional<QPoint> at;
        QObject::connect(&pins, &Gui::PinManager::pinAnotherRequested, [&at](const QImage &, const QPoint &globalTopLeft) { at = globalTopLeft; });

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
        auto *window = dynamic_cast<Gui::PinWindow *>(pins.pinAt(0));
        ASSERT_NE(window, nullptr) << "管理器里的贴图不是 PinWindow，这一条判据接不上";
        window->move(50, 60);

        QMimeData mime;
        mime.setText(QStringLiteral("dropped text"));

        const QMimeData      *seen = nullptr;
        std::optional<QPoint> at;
        QObject::connect(&pins, &Gui::PinManager::pinDropRequested,
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
        auto &first = static_cast<Gui::PinWindow &>(pins.addPin(makeImage(), QPoint(10, 10)));
        auto &other = static_cast<Gui::PinWindow &>(pins.addPin(makeImage(), QPoint(80, 80)));

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

        pins.togglePinSelection(static_cast<Gui::PinWindow &>(*first));
        pins.togglePinSelection(static_cast<Gui::PinWindow &>(*other));
        ASSERT_EQ(pins.selectedPinCount(), 2);

        const QPoint otherBefore = other->globalRect().topLeft();
        const QPoint firstBefore = first->globalRect().topLeft();

        // 从窗口正中起手：角与边中点上摆着缩放手柄，按在那里会变成「改大小」而不是「挪位置」
        auto       *target = static_cast<Gui::PinWindow *>(first);
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

        pins.togglePinSelection(static_cast<Gui::PinWindow &>(*first));
        pins.togglePinSelection(static_cast<Gui::PinWindow &>(*other));

        QWheelEvent wheel{QPointF{5.0, 5.0}, QPointF{5.0, 5.0}, QPoint{0, 0}, QPoint{0, 120}, Qt::NoButton, Qt::NoModifier, Qt::NoScrollPhase, false};
        QApplication::sendEvent(static_cast<Gui::PinWindow *>(first), &wheel);

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

        pins.togglePinSelection(static_cast<Gui::PinWindow &>(*first));
        pins.togglePinSelection(static_cast<Gui::PinWindow &>(*other));
        ASSERT_TRUE(first->isAlwaysOnTop());

        auto *menu      = static_cast<Gui::PinWindow *>(first)->buildContextMenu();
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

        pins.togglePinSelection(static_cast<Gui::PinWindow &>(*first));
        pins.togglePinSelection(static_cast<Gui::PinWindow &>(*other));

        auto *menu      = static_cast<Gui::PinWindow *>(first)->buildContextMenu();
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

        pins.togglePinSelection(static_cast<Gui::PinWindow &>(*first));
        pins.togglePinSelection(static_cast<Gui::PinWindow &>(*other));

        const auto escape = [](QWidget &target)
        {
            QKeyEvent event{QEvent::KeyPress, Qt::Key_Escape, Qt::NoModifier};
            QApplication::sendEvent(&target, &event);
        };

        escape(*static_cast<Gui::PinWindow *>(first));
        EXPECT_EQ(pins.selectedPinCount(), 0) << "第一次 Esc 没清选择";
        EXPECT_EQ(pins.count(), 2) << "第一次 Esc 把贴图也关了：关闭是一组一起的动作，得再按一次";

        escape(*static_cast<Gui::PinWindow *>(first));
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

        pins.togglePinSelection(static_cast<Gui::PinWindow &>(*pins.pinAt(0)));
        pins.togglePinSelection(static_cast<Gui::PinWindow &>(*pins.pinAt(1)));
        ASSERT_EQ(pins.selectedPinCount(), 2);

        // 建第二组再切过去：选中集该清空
        ASSERT_TRUE(pins.movePinToGroup(1, 2).has_value());
        ASSERT_TRUE(pins.switchToNextGroup().has_value());
        EXPECT_EQ(pins.selectedPinCount(), 0) << "切组之后选中集还留着：那几张已经不在屏上了";

        pins.togglePinSelection(static_cast<Gui::PinWindow &>(*pins.pinAt(0)));
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

        pins.setPinSolo(static_cast<Gui::PinWindow &>(*first), true);
        EXPECT_TRUE(pins.pinIsSolo(static_cast<const Gui::PinWindow &>(*first)));
        EXPECT_TRUE(static_cast<Gui::PinWindow *>(pins.pinAt(0))->isSolo()) << "主角态没推给窗口（菜单项的勾选态照它走）";
        EXPECT_FALSE(static_cast<Gui::PinWindow *>(pins.pinAt(1))->isSolo()) << "非主角被推成了主角";
        EXPECT_TRUE(isOnDesk(pins.pinAt(0))) << "主角自己该留在屏上";
        EXPECT_FALSE(isOnDesk(pins.pinAt(1))) << "其余的没收起来，Solo 没生效";
        EXPECT_FALSE(isOnDesk(pins.pinAt(2)));

        pins.setPinSolo(static_cast<Gui::PinWindow &>(*first), false);
        EXPECT_FALSE(pins.pinIsSolo(static_cast<const Gui::PinWindow &>(*first)));
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
        pins.setPinSolo(static_cast<Gui::PinWindow &>(*solo), true);
        ASSERT_FALSE(isOnDesk(pins.pinAt(1)));

        static_cast<Gui::PinWindow *>(solo)->close();
        emit static_cast<Gui::PinWindow *>(solo)->closedByUser();

        EXPECT_FALSE(pins.pinIsSolo(static_cast<const Gui::PinWindow &>(*pins.pinAt(0)))) << "主角没了 Solo 还挂着";
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
        pins.setPinSolo(static_cast<Gui::PinWindow &>(*pins.pinAt(0)), true);
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
        pins.setPinSolo(static_cast<Gui::PinWindow &>(*pins.pinAt(0)), true);
        ASSERT_TRUE(pins.movePinToGroup(1, 2).has_value());
        ASSERT_TRUE(pins.switchToNextGroup().has_value());
        EXPECT_FALSE(pins.pinIsSolo(static_cast<const Gui::PinWindow &>(*pins.pinAt(0)))) << "切组之后 Solo 还挂着";
    }

    /**
     * @brief 钉住 Solo 与选中集互不打架：开 Solo 会把选中集清掉（其余几张已经不在屏上）
     */
    TEST_F(PinManagerFixture, SoloClearsTheSelection)
    {
        auto &pins = makeManager();
        pins.addPin(makeImage(), QPoint(10, 10));
        pins.addPin(makeImage(), QPoint(80, 80));
        pins.togglePinSelection(static_cast<Gui::PinWindow &>(*pins.pinAt(0)));
        pins.togglePinSelection(static_cast<Gui::PinWindow &>(*pins.pinAt(1)));
        ASSERT_EQ(pins.selectedPinCount(), 2);

        pins.setPinSolo(static_cast<Gui::PinWindow &>(*pins.pinAt(0)), true);
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
        auto *pin = dynamic_cast<Gui::PinWindow *>(pins.pinAt(0));
        ASSERT_NE(pin, nullptr) << "管理器里的贴图不是 PinWindow，动图这一判据接不上";
        ASSERT_TRUE(pin->isAnimated()) << "前提不对：这一张没被当成动图接上";

        QImage replacement{7, 3, QImage::Format_ARGB32};
        replacement.fill(QColor{10, 200, 30});

        const auto refused = pins.replacePinContent(0, replacement);
        ASSERT_FALSE(refused.has_value()) << "动图被换掉了：下一帧一到就把它盖回去";
        EXPECT_EQ(refused.error().code, Base::ErrorCode::InvalidArgument);
        EXPECT_NE(refused.error().message.find("动图"), std::string::npos) << "拒绝文案要点名动图，实际：" << refused.error().message;
        EXPECT_NE(refused.error().message.find("静态图"), std::string::npos) << "拒绝文案要给出出路（先存成静态图），实际：" << refused.error().message;
        const QSizeF originalSize{kImageWidth, kImageHeight};
        EXPECT_EQ(pin->state().logicalSize, originalSize) << "被拒的那一次不该留下半份替换";

        const auto missing = pins.replacePinContent(pins.count(), replacement);
        ASSERT_FALSE(missing.has_value()) << "不在册的索引被放过了";
        EXPECT_EQ(missing.error().code, Base::ErrorCode::InvalidArgument);
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

        auto *first     = dynamic_cast<Gui::PinWindow *>(pins.pinAt(0));
        auto *concealed = dynamic_cast<Gui::PinWindow *>(pins.pinAt(2));
        ASSERT_NE(first, nullptr);
        ASSERT_NE(concealed, nullptr);
        EXPECT_TRUE(first->isSelected()) << "在桌上的那张没被选进集";
        EXPECT_FALSE(concealed->isSelected()) << "收着的那张被选进了集：整组动作会落到看不见的图上";

        pins.clearPinSelection();
        EXPECT_EQ(pins.selectedPinCount(), 0) << "全选之后清不干净，后面的整组操作就没有出口";
    }

    /**
     * @brief 钉住关闭留下可恢复的一份、销毁不留，以及取完要报「没有了」
     * @details 两条路都会把窗口从在册表摘掉，区别只在那份像素抄没抄——用户按的就是这一格差别。
     */
    TEST_F(PinManagerFixture, ClosedPinsAreRecoverableAndDestroyedOnesAreNot)
    {
        auto &pins = makeManager();
        // 两张给成不同颜色：只数张数认不出「抄走的是哪一份」——把两档反过来照样数得对
        QImage closedImage{QSize{8, 8}, QImage::Format_ARGB32};
        closedImage.fill(QColor{200, 30, 30});
        QImage destroyedImage{QSize{8, 8}, QImage::Format_ARGB32};
        destroyedImage.fill(QColor{30, 30, 200});
        pins.addPin(closedImage, QPoint(10, 10));
        pins.addPin(destroyedImage, QPoint(60, 60));
        auto *first  = dynamic_cast<Gui::PinWindow *>(pins.pinAt(0));
        auto *second = dynamic_cast<Gui::PinWindow *>(pins.pinAt(1));
        ASSERT_NE(first, nullptr);
        ASSERT_NE(second, nullptr);

        emit first->closedByUser();
        emit second->destroyRequested();

        EXPECT_EQ(pins.count(), 0) << "两条路都得把这一张从在册表里摘掉";
        EXPECT_EQ(pins.closedPinCount(), 1) << "关闭的那张该留下一份可按回来的，销毁的那张不该留";

        const auto taken = pins.takeClosedPin();
        ASSERT_TRUE(taken.has_value()) << "关掉的那张该按得回来";
        const QColor closedCorner = taken->image.pixelColor(0, 0);
        EXPECT_EQ(closedCorner, QColor(200, 30, 30)) << "留下的必须是被关闭那一张的像素：抄错人等于两张都回不来";
        EXPECT_FALSE(taken->image.isNull()) << "抄下来的那份不能是空图：空图按出来是一张看不见的贴图";
        EXPECT_EQ(pins.closedPinCount(), 0);
        EXPECT_FALSE(pins.takeClosedPin().has_value()) << "取完就该说没有了，别让调用方去猜册子空没空";
    }

    /**
     * @brief 钉住可恢复那份册子只留最新五条
     * @details 上限之内的像素是「手滑关错了」的代价，超出就变成一份没人认领的内存：这条判据钉的是它真的会挤掉。
     */
    TEST_F(PinManagerFixture, ClosedPinHistoryKeepsOnlyTheNewestFive)
    {
        auto &pins = makeManager();
        for (auto index = 0; index < 7; ++index)
        {
            pins.addPin(makeImage(), QPoint(10 * index, 10 * index));
        }
        ASSERT_EQ(pins.count(), 7);

        while (pins.count() > 0)
        {
            auto *pin = dynamic_cast<Gui::PinWindow *>(pins.pinAt(0));
            ASSERT_NE(pin, nullptr);
            emit pin->closedByUser();
        }

        EXPECT_EQ(pins.closedPinCount(), static_cast<int>(Gui::kClosedPinHistorySize)) << "关掉七张只该留得回五张";
    }

    /**
     * @brief 钉住整组「批量保存」逐张落盘：选中两张写两份，没选的那张不参与
     * @details 同一次里几张的模板名会撞在同一个时间戳上，让位去重那一档正是这种时候才走；
     *          判据取目录里的文件数，不只看报出去的那个计数——两者不同解就算实现坏了。
     */
    TEST_F(PinManagerFixture, SavingSelectionWritesOneFilePerSelectedPin)
    {
        const auto target = std::filesystem::temp_directory_path() / std::format("GyanisScribe-PinManager-{}", ::testing::UnitTest::GetInstance()->current_test_info()->name());
        std::error_code ignored;
        std::filesystem::remove_all(target, ignored);
        ASSERT_TRUE(settings().setValue(GyanisScribe::Core::kPathScreenshot, QVariant(Base::Paths::toDisplayText(target))).has_value());
        ASSERT_TRUE(settings().setValue(GyanisScribe::Core::kSaveImageFormat, QVariant(QStringLiteral("Png"))).has_value());
        ASSERT_TRUE(settings().setValue(GyanisScribe::Core::kSaveNameTemplate, QVariant(QStringLiteral("pin_%date%"))).has_value());

        auto &pins = makeManager();
        pins.addPin(makeImage(), QPoint(10, 10));
        pins.addPin(makeImage(), QPoint(60, 10));
        pins.addPin(makeImage(), QPoint(110, 10));
        auto *first  = dynamic_cast<Gui::PinWindow *>(pins.pinAt(0));
        auto *second = dynamic_cast<Gui::PinWindow *>(pins.pinAt(1));
        ASSERT_NE(first, nullptr);
        ASSERT_NE(second, nullptr);
        pins.togglePinSelection(*first);
        pins.togglePinSelection(*second);

        const auto report = pins.saveSelectedPins(*first);
        EXPECT_EQ(report.saved, 2) << "选中两张就该写成两份文件，没选的那张不该跟着存";
        EXPECT_EQ(report.failed, 0) << "这一档不该有失败，原因：" << report.reason.toStdString();

        int files = 0;
        for (const auto &entry: std::filesystem::directory_iterator(target, ignored))
        {
            EXPECT_EQ(entry.path().extension().string(), ".png") << "后缀没跟着设置里那一档格式：" << entry.path().filename().string();
            ++files;
        }
        EXPECT_EQ(files, 2) << "目录里的文件数与报出去的张数不同解";

        std::filesystem::remove_all(target, ignored);
    }

    /**
     * @brief 钉住批量保存把失败原因带出来：目录那一格被一个普通文件占着
     * @details 整组通常撞同一个坑，「一张都没写成」这一档必须给一句还能做的事（换一个目录），
     *          只报个数等于让人对着一个不动的目录猜。
     */
    TEST_F(PinManagerFixture, SavingSelectionReportsTheReasonWhenTheDirectoryIsUnusable)
    {
        const auto blocked = std::filesystem::temp_directory_path() / std::format("GyanisScribe-PinManager-{}", ::testing::UnitTest::GetInstance()->current_test_info()->name());
        std::error_code ignored;
        std::filesystem::remove(blocked, ignored);
        {
            std::ofstream occupant{blocked};
            occupant << "占位的不是一个目录";
        }
        ASSERT_TRUE(settings().setValue(GyanisScribe::Core::kPathScreenshot, QVariant(Base::Paths::toDisplayText(blocked))).has_value());
        ASSERT_TRUE(settings().setValue(GyanisScribe::Core::kSaveImageFormat, QVariant(QStringLiteral("Png"))).has_value());

        auto &pins = makeManager();
        pins.addPin(makeImage(), QPoint(10, 10));
        auto *first = dynamic_cast<Gui::PinWindow *>(pins.pinAt(0));
        ASSERT_NE(first, nullptr);

        const auto report = pins.saveSelectedPins(*first);
        EXPECT_EQ(report.saved, 0) << "目录写不出来却报了写成的张数";
        EXPECT_EQ(report.failed, 1) << "没写成那一张也要入账，不然提示会说「全存好了」";
        EXPECT_FALSE(report.reason.isEmpty()) << "失败要带原因：换目录是这里唯一还能做的事";

        std::filesystem::remove(blocked, ignored);
    }

    /**
     * @brief 钉住分组名字过了记录一圈还在，而且首尾空白被剪掉
     * @details 名字表住在配置里那一格，是「哪组叫什么」的唯一凭据；重启读不回来就等于一整桌的标签丢了。
     */
    TEST_F(PinManagerFixture, GroupNamesSurviveARestart)
    {
        {
            auto &pins = makeManager();
            pins.addPin(makeImage(), QPoint(20, 20));
            pins.addPin(makeImage(), QPoint(200, 40));
            ASSERT_TRUE(pins.movePinToGroup(1, 2).has_value());
            ASSERT_TRUE(pins.renameGroup(2, QStringLiteral("  参照对比  ")).has_value()) << "改名被拒";
            EXPECT_EQ(pins.groupName(2), QStringLiteral("参照对比")) << "首尾空白没剪：菜单里带空格的名字像没对齐";
            ASSERT_TRUE(pins.saveLayout().has_value()) << "布局没写下去";
        }

        const auto stored = Core::loadGroupNames(settings());
        ASSERT_TRUE(stored.has_value()) << stored.error().message;
        EXPECT_EQ(stored->value(2), QStringLiteral("参照对比")) << "名字没落进配置";

        auto      &reopened = makeManager();
        const auto restored = reopened.restoreLayout();
        ASSERT_TRUE(restored.has_value()) << restored.error().message;
        EXPECT_EQ(reopened.groupName(2), QStringLiteral("参照对比")) << "名字没过夜：重启后一整桌又变回「分组 N」";
    }

    /**
     * @brief 钉住取消命名回到「分组 N」那一档：表里不留空值
     */
    TEST_F(PinManagerFixture, ClearingAGroupNameRemovesTheEntry)
    {
        auto &pins = makeManager();
        pins.addPin(makeImage(), QPoint(20, 20));
        pins.addPin(makeImage(), QPoint(200, 40));
        ASSERT_TRUE(pins.movePinToGroup(1, 2).has_value());
        ASSERT_TRUE(pins.renameGroup(2, QStringLiteral("参照对比")).has_value());

        ASSERT_TRUE(pins.renameGroup(2, QStringLiteral("   ")).has_value());
        EXPECT_TRUE(pins.groupName(2).isEmpty()) << "留了一串空白的名字，菜单里就会多出一个看不见的尾巴";
    }

    /**
     * @brief 钉住删一组把成员并回第一组，名字跟着清
     * @details 删的是「这一组」这个标签，不是上面摆着的图：图要回到落点并重新摆在桌上。
     */
    TEST_F(PinManagerFixture, DeletingAGroupMovesItsMembersBackToTheFirstGroup)
    {
        auto &pins = makeManager();
        pins.addPin(makeImage(), QPoint(20, 20));
        pins.addPin(makeImage(), QPoint(200, 40));
        pins.addPin(makeImage(), QPoint(400, 40));
        ASSERT_TRUE(pins.movePinToGroup(1, 2).has_value());
        ASSERT_TRUE(pins.movePinToGroup(2, 2).has_value());
        ASSERT_TRUE(pins.renameGroup(2, QStringLiteral("参照对比")).has_value());
        ASSERT_EQ(pins.groupsInUse(), (QVector<int>{1, 2})) << "前提不对：没形成两组";

        const auto deleted = pins.deleteGroup(2);
        ASSERT_TRUE(deleted.has_value()) << deleted.error().message;
        EXPECT_EQ(*deleted, 2) << "并回去的张数不对：少了就是有人被扔在看不见的一组";
        EXPECT_EQ(pins.groupsInUse().size(), 1) << "组还没了：切组会在一个空壳上停一站";
        EXPECT_TRUE(pins.groupName(2).isEmpty()) << "组没了名字还挂着";
        for (auto index = 0; index < pins.count(); ++index)
        {
            EXPECT_TRUE(isOnDesk(pins.pinAt(index))) << "并回第一组之后这几张该重新摆在桌上";
        }
    }

    /**
     * @brief 钉住第一组删不掉，且拒绝的理由说清它是落点
     * @details 这一档的拒绝面比成功面更要紧：删掉落点之后，别组再删就无处可并。
     */
    TEST_F(PinManagerFixture, DeletingTheFirstGroupIsRefusedWithTheLandingPlaceReason)
    {
        auto &pins = makeManager();
        pins.addPin(makeImage(), QPoint(20, 20));

        const auto refused = pins.deleteGroup(Core::kFirstPinGroup);
        ASSERT_FALSE(refused.has_value()) << "第一组被删掉了，后面删别组就没有并回去的地方";
        EXPECT_EQ(refused.error().code, GyanisScribe::Base::ErrorCode::InvalidArgument);
        EXPECT_NE(refused.error().message.find("落点"), std::string::npos) << "拒绝要说明为什么删不掉，实际：" << refused.error().message;
        EXPECT_EQ(pins.count(), 1) << "拒删归拒删，那张图不该跟着没";
    }

    /**
     * @brief 钉住一组的最后一张关掉之后，它的名字也跟着没
     * @details 不留空壳组是切组那一条的规矩：名字若活得比组长，下次切组就停在一个看不见的站上。
     */
    TEST_F(PinManagerFixture, GroupNameGoesAwayWithItsLastPin)
    {
        auto &pins = makeManager();
        pins.addPin(makeImage(), QPoint(20, 20));
        pins.addPin(makeImage(), QPoint(200, 40));
        ASSERT_TRUE(pins.movePinToGroup(1, 2).has_value());
        ASSERT_TRUE(pins.renameGroup(2, QStringLiteral("待办")).has_value());
        ASSERT_EQ(pins.groupName(2), QStringLiteral("待办"));

        auto *second = dynamic_cast<Gui::PinWindow *>(pins.pinAt(1));
        ASSERT_NE(second, nullptr);
        emit second->closedByUser();

        EXPECT_TRUE(pins.groupName(2).isEmpty()) << "组里最后一张关掉了，名字还挂在册子上";
        ASSERT_TRUE(pins.saveLayout().has_value()) << "布局没写下去";
        const auto stored = Core::loadGroupNames(settings());
        ASSERT_TRUE(stored.has_value()) << stored.error().message;
        EXPECT_TRUE(stored->isEmpty()) << "内存里清了名字而记录里还留着：下次启动那个空壳组又带着名字回来";
    }

    /**
     * @brief 钉住切组走的是设置里那一圈，而不是组号升序
     * @details 三组各摆一张，顺序填「9, 1, 3, 2」：9 早已不在册要跳过，剩下的照名单排成 1,3,2。
     *          从第一组按下去该到第三组——升序的话这里是第二组，两条判据分得开；走完一圈回第一组。
     */
    TEST_F(PinManagerFixture, GroupRotationOrderDrivesWhichGroupComesNext)
    {
        auto &pins = makeManager();
        pins.addPin(makeImage(), QPoint(10, 10));
        pins.addPin(makeImage(), QPoint(60, 60));
        pins.addPin(makeImage(), QPoint(120, 120));
        ASSERT_TRUE(pins.movePinToGroup(1, 3).has_value());
        ASSERT_TRUE(pins.movePinToGroup(2, 2).has_value());
        ASSERT_EQ(pins.groupsInUse(), (QVector<int>{1, 2, 3})) << "三组各一张的前提没摆出来";

        ASSERT_TRUE(settings()
                            .setValue(GyanisScribe::Core::kPinGroupRotationOrder, QStringList{QStringLiteral("9"), QStringLiteral("1"), QStringLiteral("3"), QStringLiteral("2")})
                            .has_value());

        const auto first = pins.switchToNextGroup();
        ASSERT_TRUE(first.has_value()) << first.error().message;
        EXPECT_EQ(*first, 3) << "按 1,3,2 这一圈，第一组的下一组是第三组，不是升序里的第二组";

        const auto second = pins.switchToNextGroup();
        ASSERT_TRUE(second.has_value()) << second.error().message;
        EXPECT_EQ(*second, 2) << "第三组之后该是第二组";

        const auto third = pins.switchToNextGroup();
        ASSERT_TRUE(third.has_value()) << third.error().message;
        EXPECT_EQ(*third, 1) << "走完一圈该回第一组";
    }

    /**
     * @brief 钉住名单填歪时切组照旧能用，退回升序
     * @details 一格填歪不该把键盘上的切组整个卡死：报一句原因，按组号升序继续走。
     *          「2, 2」是重复组号那一类坏值——它看上去能用，但那一组在一圈里出现两次。
     */
    TEST_F(PinManagerFixture, BrokenRotationOrderFallsBackToAscending)
    {
        auto &pins = makeManager();
        pins.addPin(makeImage(), QPoint(10, 10));
        pins.addPin(makeImage(), QPoint(60, 60));
        pins.addPin(makeImage(), QPoint(120, 120));
        ASSERT_TRUE(pins.movePinToGroup(1, 3).has_value());
        ASSERT_TRUE(pins.movePinToGroup(2, 2).has_value());

        ASSERT_TRUE(settings().setValue(GyanisScribe::Core::kPinGroupRotationOrder, QStringList{QStringLiteral("2"), QStringLiteral("2")}).has_value());

        const auto switched = pins.switchToNextGroup();
        ASSERT_TRUE(switched.has_value()) << "名单坏了切组就该退回升序，而不是拒掉这一次：" << switched.error().message;
        EXPECT_EQ(*switched, 2) << "升序里第一组的下一组是第二组";
    }


    /**
     * @brief 钉住「刷新这一张」要重抓的那块矩形就是图本身占的那块
     * @details 判据取窗口自己的
     * globalRect：刷新抓的是「图此刻盖住的屏幕」，

     * * 拿窗口的外接框去抓，斜着的一张会把邻居窗口的像素也框进来。
     */
    TEST_F(PinManagerFixture, RefreshTargetIsTheRectTheImageCovers)
    {
        auto &pins  = makeManager();
        auto &first = pins.addPin(makeImage(), QPoint(120, 80));

        const auto target = pins.pinRefreshTarget(0);
        ASSERT_TRUE(target.has_value()) << target.error().message;
        EXPECT_EQ(*target, first.globalRect()) << "静图、没旋转时，重抓的矩形就该是这张图占的那块";
    }

    /**
     * @brief 钉住不许刷新的那两档与不在册的索引，各自把出路说清
     * @details
     * 缩略图那块方块的面积不是这张图的面积；动图换上去的静态像素会被下一帧盖掉——

     * * 两档都该拒绝并报出原因，而不是抓一块错的画面糊上去。
     */
    TEST_F(PinManagerFixture, RefreshTargetRefusesThumbnailsAnimatedPinsAndUnknownIndexes)
    {
        auto &pins  = makeManager();
        auto &first = pins.addPin(makeImage(), QPoint(20, 20));

        const auto gifPath = PinTestAssets::writeTwoFrameGif(std::filesystem::temp_directory_path() / "GyanisScribe-PinManager-refresh-refuses-animated.gif");
        ASSERT_FALSE(gifPath.isEmpty()) << "素材没落到临时文件上";
        auto &animated = pins.addPin(makeImage(), QPoint(200, 20), gifPath);
        ASSERT_TRUE(static_cast<Gui::PinWindow *>(&animated)->isAnimated()) << "前提不对：这一张没被当成动图接上";

        const auto refusedAnimated = pins.pinRefreshTarget(1);
        ASSERT_FALSE(refusedAnimated.has_value());
        EXPECT_NE(refusedAnimated.error().message.find("动图"), std::string::npos) << refusedAnimated.error().message;

        static_cast<Gui::PinWindow *>(&first)->setThumbnailMode(true);
        const auto refusedThumbnail = pins.pinRefreshTarget(0);
        ASSERT_FALSE(refusedThumbnail.has_value());
        EXPECT_NE(refusedThumbnail.error().message.find("小图"), std::string::npos) << refusedThumbnail.error().message;

        const auto missing = pins.pinRefreshTarget(7);
        ASSERT_FALSE(missing.has_value());
        EXPECT_NE(missing.error().message.find("不在册"), std::string::npos) << missing.error().message;
    }

    /**
     * @brief 钉住「有人按了刷新」报的是那一张的在册序号
     * @details
     * 管理器只把像素交回界面层，认人靠的就是这个序号：报错一张刷成另一张，

     * * 用户看到的是「我按的这张没动，别的那张跳了」。
     */
    TEST_F(PinManagerFixture, RefreshRequestCarriesTheIndexOfThePinThatAsked)
    {
        auto &pins = makeManager();
        pins.addPin(makeImage(), QPoint(20, 20));
        auto &second = pins.addPin(makeImage(), QPoint(200, 20));

        std::vector<int> seen;
        QObject::connect(&pins, &Gui::PinManager::pinRefreshRequested, [&seen](const int index) { seen.push_back(index); });

        QKeyEvent press{QEvent::KeyPress, Qt::Key_F5, Qt::NoModifier};
        QApplication::sendEvent(static_cast<Gui::PinWindow *>(&second), &press);

        ASSERT_EQ(seen.size(), 1u);
        EXPECT_EQ(seen.front(), 1) << "按在第二张上，报出的却是别的序号";
    }

    /**
     * @brief 钉住「抓屏那一刻把这一张藏起来」的两半：抓的时候它不在桌上，抓完一定摆回
     * @details
     * 贴图是置顶窗：不藏就会抓到贴图的自己，第二次刷新起画面冻在第一帧。

     * *          摆回要覆盖抓取失败那一路——只藏不回等于把用户的一张图刷没了。
     */
    TEST_F(PinManagerFixture, GrabWithPinHiddenHidesItDuringTheGrabAndAlwaysBringsItBack)
    {
        auto &pins   = makeManager();
        auto &first  = pins.addPin(makeImage(), QPoint(60, 60));
        auto *window = static_cast<Gui::PinWindow *>(&first);
        window->setAttribute(Qt::WA_DontShowOnScreen);
        window->show();
        ASSERT_TRUE(window->isVisible()) << "前提不对：这张该在桌上";

        std::vector<bool> visibleDuringGrab;
        const auto        grab = [&visibleDuringGrab, window]() -> Base::Expected<Base::CapturedImage>
        {
            visibleDuringGrab.push_back(window->isVisible());
            return Base::CapturedImage{};
        };
        const auto ok = pins.grabWithPinHidden(0, grab);
        ASSERT_TRUE(ok.has_value());
        ASSERT_EQ(visibleDuringGrab.size(), 1u);
        EXPECT_FALSE(visibleDuringGrab.front()) << "抓的时候没把这一张藏起来：抓回来的会是贴图的自己";
        EXPECT_TRUE(window->isVisible()) << "抓完没摆回：那张图从此不在桌上";

        // 抓取失败那一路同样要摆回
        visibleDuringGrab.clear();
        const auto failingGrab = [&visibleDuringGrab, window]() -> Base::Expected<Base::CapturedImage>
        {
            visibleDuringGrab.push_back(window->isVisible());
            return std::unexpected(Base::Error{Base::ErrorCode::ScreenUnavailable, GyanisScribe::Base::uiTextUtf8("抓不到这块屏幕")});
        };
        const auto failed = pins.grabWithPinHidden(0, failingGrab);
        ASSERT_FALSE(failed.has_value());
        EXPECT_FALSE(visibleDuringGrab.front()) << "失败那一路也得先藏起来";
        EXPECT_TRUE(window->isVisible()) << "抓取失败就把这一张留在桌上不见了";
    }


    /**
     * @brief 钉住「换内容之后的显示尺寸 = 像素 ÷ 那份像素自带的 dpr」
     * @details 抓屏那一路交回的是物理像素（图像的 dpr 被清成 1），贴图刷新那一路要把这块屏的
     * dpr
     * 带回去，
     *          否则换一次内容就把图放大 dpr 倍。这一格折算是 replaceContent 唯一的尺寸来源，先把它钉住：
     *          丢 dpr 的那一版在这里当场红（80×40
     * 当成 80×40 逻辑像素摆出来）。
     */
    TEST_F(PinManagerFixture, ReplacingContentDerivesTheDisplayedSizeFromTheImageDpr)
    {
        // 宏里的花括号会被逗号拆开，两份期望尺寸先落名字
        const QSize startSize{32, 24};
        const QSize refreshedSize{40, 20};

        auto &pins = makeManager();
        auto &pin  = pins.addPin(makeImage(), QPoint(40, 40));
        ASSERT_EQ(pin.globalRect().size(), startSize) << "前提不对：起点该是按原图折出来的那块";

        QImage physical{QSize{80, 40}, QImage::Format_ARGB32};
        physical.fill(QColor{20, 80, 160});
        physical.setDevicePixelRatio(2.0);
        ASSERT_TRUE(pins.replacePinContent(0, physical).has_value()) << "换内容被拒";

        EXPECT_EQ(pin.globalRect().size(), refreshedSize) << "显示尺寸没按 dpr 折算：这张图被摆成了物理像素那么大";
    }

    /**
     * @brief 钉住色卡身份从「贴出来」与「换内容」这两条路进来、并在换成别的内容时被摘干净
     * @details 界面层把颜色认出来之后是走 addPin / replacePinContent 进来的：漏掉一处，
     *          贴出来的色卡就改不了色，或换走之后那张照片上还挂着「改这一张的颜色…」。
     */
    TEST_F(PinManagerFixture, ColorCardIdentityFollowsTheContentOnBothRoutes)
    {
        auto        &pins = makeManager();
        const QColor orange{255, 136, 0};

        pins.addPin(makeImage(), QPoint(100, 100), {}, orange);
        auto *card = pins.pinAt(0);
        ASSERT_NE(card, nullptr);
        ASSERT_TRUE(card->colorCardColor().has_value()) << "贴出来时交下去的原色没进窗口";
        EXPECT_EQ(*card->colorCardColor(), orange);

        pins.addPin(makeImage(), QPoint(400, 100));
        auto *photo = pins.pinAt(1);
        ASSERT_NE(photo, nullptr);
        EXPECT_FALSE(photo->colorCardColor().has_value()) << "普通贴图凭空多出一个原色";

        // 换成另一个颜色写法：身份与记下的色一起换
        const QColor teal{0x12, 0x34, 0x56};
        ASSERT_TRUE(static_cast<bool>(pins.replacePinContent(1, makeImage(), teal))) << "替换被拒了";
        ASSERT_TRUE(photo->colorCardColor().has_value()) << "换成颜色写法之后没记下新色";
        EXPECT_EQ(*photo->colorCardColor(), teal);

        // 换成一张照片（没有颜色交进来）：这一档要摘掉，否则那张照片上还挂着改色这一项
        ASSERT_TRUE(static_cast<bool>(pins.replacePinContent(0, makeImage()))) << "替换被拒了";
        EXPECT_FALSE(card->colorCardColor().has_value()) << "换成照片之后还认得出原色，菜单里那一项会点坏这张图";
    }

    /**
     * @brief 读数用例：贴 20 张 4K 图之后进程工作集涨了多少（不断言数字）
     * @details 结论要进 README 的「性能实测」：贴图是这类工具的常驻大头，一张 4K 图的像素就是 33MB 这个量级。
     *          量法写死在这一处：先记一次基线，再贴 20 张 3840×2160、底色各不相同的图（真的建 20 扇贴图窗），
     *          再记一次，报「涨了多少、平均每张多少」。它不断言数字：跑在什么机器上就报什么数。
     */
    TEST_F(PinManagerFixture, ReportsTheWorkingSetAfterTwentyUltraHdPins)
    {
        constexpr int kWide  = 3840;
        constexpr int kHigh  = 2160;
        constexpr int kCount = 20;

        const auto baseline = currentWorkingSetBytes();
        if (baseline == 0)
        {
            GTEST_SKIP() << "这台机器报不出工作集，这一格只能跳过";
        }

        auto &pins = makeManager();
        for (int index = 0; index < kCount; ++index)
        {
            QImage image{kWide, kHigh, QImage::Format_ARGB32};
            // 底色各不相同：内容完全一样时 Qt 可能在内部共享同一份像素，量出来就不是「20 张」的成本
            image.fill(QColor{index * 11 % 256, index * 29 % 256, index * 47 % 256});
            pins.addPin(image, QPoint(20 + index * 6, 20 + index * 6));
        }

        const auto loaded = currentWorkingSetBytes();
        const auto growth = loaded > baseline ? loaded - baseline : 0;
        std::printf("[读数] 贴 %d 张 %d×%d 的图后进程工作集：基线 %.0f MiB → %.0f MiB（涨 %.0f MiB，每张约 %.1f MiB）%s", kCount, kWide, kHigh,
                    static_cast<double>(baseline) / (1024.0 * 1024.0), static_cast<double>(loaded) / (1024.0 * 1024.0), static_cast<double>(growth) / (1024.0 * 1024.0),
                    static_cast<double>(growth) / (1024.0 * 1024.0) / kCount, "\n");
    }
} // namespace GyanisScribe
