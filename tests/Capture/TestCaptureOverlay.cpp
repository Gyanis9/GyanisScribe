// 遮罩的「悬停认出整扇窗口」：候选按这块屏的原点换成局部坐标、命中取压在最上面的那一个、
// 原地放开（没拖出选区）时采纳此刻亮着的那一块；另钉放大镜的读数换算与翻边摆放。
// 事件用 sendEvent 直接投给控件，判据不依赖「鼠标恰好在这个时刻移动到位」。
#include "Capture/CaptureOverlay.h"

#include "Pick/ColorFormat.h"
#include "Pick/PickGeometry.h"

#include <QApplication>
#include <QImage>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QPoint>
#include <QRect>

#include <gtest/gtest.h>

#include <memory>
#include <vector>

using GyanisScribe::Capture::CaptureOverlay;
using GyanisScribe::Pick::ColorFormat;

namespace
{
    std::unique_ptr<QApplication> application;

    // 这块「屏」故意不从 (0,0) 开始：局部换算少减一次原点，在主屏上看不出来，在副屏上就是错位
    const QRect kScreenGeometry{1920, 0, 800, 600};

    class CaptureOverlayFixture : public ::testing::Test
    {
    protected:
        static void SetUpTestSuite()
        {
            // argv 与 argc 的缓冲要有静态存储期：Qt 只存指针不拷贝
            static int   argumentCount  = 1;
            static char  argumentZero[] = "TestCaptureOverlay";
            static char *arguments[]    = {argumentZero, nullptr};
            application                 = std::make_unique<QApplication>(argumentCount, arguments);
        }

        static void TearDownTestSuite()
        {
            application.reset();
        }

        [[nodiscard]] std::unique_ptr<CaptureOverlay> makeOverlay() const
        {
            QImage background(kScreenGeometry.size(), QImage::Format_ARGB32_Premultiplied);
            background.fill(Qt::white);
            return std::make_unique<CaptureOverlay>(background, kScreenGeometry, 1.0);
        }

        /// 用给定的冻帧与缩放因子造遮罩：放大镜的两笔换算都要在这条路上验
        [[nodiscard]] std::unique_ptr<CaptureOverlay> makeOverlay(const QImage &background, qreal devicePixelRatio) const
        {
            return std::make_unique<CaptureOverlay>(background, kScreenGeometry, devicePixelRatio);
        }
    };

    /// 事件的局部坐标给控件自己读（position()），全局那一份按这块屏的原点推出去
    [[nodiscard]] QPointF globalOf(const QPoint &local)
    {
        return QPointF{kScreenGeometry.topLeft() + local};
    }

    void moveOver(CaptureOverlay &overlay, const QPoint &local)
    {
        QMouseEvent event{QEvent::MouseMove, QPointF{local}, globalOf(local), Qt::NoButton, Qt::NoButton, Qt::KeyboardModifiers{}};
        QApplication::sendEvent(&overlay, &event);
    }

    void buttonAt(CaptureOverlay &overlay, const QEvent::Type type, const QPoint &local)
    {
        const auto  buttons = type == QEvent::MouseButtonPress ? Qt::LeftButton : Qt::NoButton;
        QMouseEvent event{type, QPointF{local}, globalOf(local), Qt::LeftButton, buttons, Qt::KeyboardModifiers{}};
        QApplication::sendEvent(&overlay, &event);
    }

    /// 造一份认得出自己位置的冻帧：底色全黑，被点名的格子各涂一种
    [[nodiscard]] QImage makeFrame(const QSize &size)
    {
        QImage frame{size, QImage::Format_ARGB32};
        frame.fill(Qt::black);
        return frame;
    }

    /// 两扇叠在一起的窗口：第一份是压在最上面的那个小对话框，第二份是被它挡住的大窗口
    const std::vector<QRect> kCandidates{QRect{2100, 150, 100, 80}, QRect{2020, 100, 300, 200}};
} // namespace

/**
 * @brief 钉住悬停命中取最上面那一个，并且交回的是这块屏的局部坐标
 * @details 全局原点 (1920,0) 的屏上，全局 (2100,150) 的窗口在局部是 (180,150)。
 *          少减一次原点，遮罩就会把框画到偏左 1920 格的地方——用户看得见，代码里却不出错。
 */
TEST_F(CaptureOverlayFixture, HoverAdoptsTopmostCandidateInLocalCoordinates)
{
    auto overlay = makeOverlay();
    overlay->setWindowCandidates(kCandidates);

    // (200,160) 同时落在两扇上：取压在上面那一份
    moveOver(*overlay, QPoint{200, 160});
    ASSERT_TRUE(overlay->hoveredRegion().has_value());
    EXPECT_EQ(*overlay->hoveredRegion(), QRect(180, 150, 100, 80));

    // 只落在下面那一扇的位置：认出的就是它，局部坐标同样减过屏原点
    moveOver(*overlay, QPoint{250, 250});
    ASSERT_TRUE(overlay->hoveredRegion().has_value());
    EXPECT_EQ(*overlay->hoveredRegion(), QRect(100, 100, 300, 200));

    // 哪一扇都不在：不许亮一个凭空的框
    moveOver(*overlay, QPoint{700, 500});
    EXPECT_FALSE(overlay->hoveredRegion().has_value()) << "光标在没有候选的地方也亮了一个框";
}

/**
 * @brief 钉住「按下又原地放开」采纳悬停的那一扇，而原地放开过去什么都不产出
 * @details 这是框选里最常用的一下：点一下就要这一扇窗口。采纳之后选区与窗口边界要逐位相同，
 *          差一格就会把邻窗的一列像素带进成品。
 */
TEST_F(CaptureOverlayFixture, ClickWithoutDragAdoptsTheHoveredWindow)
{
    auto overlay = makeOverlay();
    overlay->setWindowCandidates(kCandidates);

    moveOver(*overlay, QPoint{200, 160});
    buttonAt(*overlay, QEvent::MouseButtonPress, QPoint{200, 160});
    buttonAt(*overlay, QEvent::MouseButtonRelease, QPoint{200, 160});

    EXPECT_EQ(overlay->selectedRegion(), QRect(180, 150, 100, 80)) << "点一下没有交出压在光标下那扇窗";
    EXPECT_FALSE(overlay->hoveredRegion().has_value()) << "已经落成的选区不该再留一个预览框";
}

/**
 * @brief 钉住手动框选没被这条新路抢走：拖出来的仍然是拖的那一块
 */
TEST_F(CaptureOverlayFixture, DraggedRegionStillWinsOverHover)
{
    auto overlay = makeOverlay();
    overlay->setWindowCandidates(kCandidates);

    moveOver(*overlay, QPoint{110, 160});
    buttonAt(*overlay, QEvent::MouseButtonPress, QPoint{400, 300});
    moveOver(*overlay, QPoint{460, 360});
    buttonAt(*overlay, QEvent::MouseButtonRelease, QPoint{460, 360});

    EXPECT_EQ(overlay->selectedRegion(), QRect(400, 300, 61, 61)) << "拖出来的选区被悬停顶掉了";
    // 选区已成，之后移动不再亮预览：两块同时亮着会让人分不清交出去的是哪一个
    moveOver(*overlay, QPoint{110, 160});
    EXPECT_FALSE(overlay->hoveredRegion().has_value());
}

/**
 * @brief 拒绝面：平台没给候选时，遮罩退回纯框选，点一下什么也不产出
 * @details 这条钉住「空候选表」那一档：少了它，一次平台降级就会变成点一下就截一屏的怪行为。
 */
TEST_F(CaptureOverlayFixture, WithoutCandidatesClickProducesNothing)
{
    auto overlay = makeOverlay();

    moveOver(*overlay, QPoint{110, 160});
    EXPECT_FALSE(overlay->hoveredRegion().has_value());

    buttonAt(*overlay, QEvent::MouseButtonPress, QPoint{110, 160});
    buttonAt(*overlay, QEvent::MouseButtonRelease, QPoint{110, 160});
    EXPECT_TRUE(overlay->selectedRegion().isNull()) << "没有候选时点一下也该产出选区的话，框选就多了条隐式规则";
}

/**
 * @brief 钉住「没有按下就没有选区」：光标划过的轨迹不许编出一块框
 * @details 这条是补的旧账。遮罩开了鼠标跟踪，而 mouseMoveEvent 把光标位置直接写进选区的当前端点，
 *          于是鼠标一划就出现一块从原点量到光标的大框，按 Enter 还会把它当成用户框好的交出去。
 */
TEST_F(CaptureOverlayFixture, MovingWithoutPressingYieldsNoSelection)
{
    auto overlay = makeOverlay();
    overlay->setWindowCandidates(kCandidates);

    moveOver(*overlay, QPoint{300, 200});
    EXPECT_TRUE(overlay->selectedRegion().isNull()) << "只是划过光标就编出了一块选区";
    EXPECT_TRUE(overlay->hoveredRegion().has_value()) << "划过时悬停该亮着，它不该顺带变成选区";

    int emitted = 0;
    QObject::connect(overlay.get(), &CaptureOverlay::regionSelected, overlay.get(), [&emitted](const QRect &) { ++emitted; });
    overlay->confirmSelection();
    EXPECT_EQ(emitted, 0) << "没框过东西的一次确认也产出了成品";
}

/**
 * @brief 钉住「没有鼠标移动就没有放大镜」：默认值 (0,0) 不是光标位置
 * @details 少了这一条，遮罩一铺出来就会在左上角摆一枚放大镜，而那里通常正是要框的东西。
 */
TEST_F(CaptureOverlayFixture, MagnifierAwaitsTheFirstMouseMove)
{
    auto overlay = makeOverlay();
    EXPECT_TRUE(overlay->magnifierRect().isNull()) << "还没收到鼠标移动就摆出了放大镜";
    EXPECT_FALSE(overlay->colorUnderCursor().isValid()) << "没有光标位置却报出了一个颜色";

    moveOver(*overlay, QPoint{110, 160});
    EXPECT_FALSE(overlay->magnifierRect().isNull());
}

/**
 * @brief 钉住放大镜跟着光标常驻，不限于拖框途中
 * @details 挑起始角、拉手柄、平移选区这三处都要像素级读数；只在拖框时显示的话，
 *          最需要它的「按下之前对准那一个像素」反而没有。
 */
TEST_F(CaptureOverlayFixture, MagnifierTravelsWithTheCursorBeyondDragging)
{
    auto overlay = makeOverlay();
    EXPECT_FALSE(overlay->isMagnifierVisible()) << "还没收到鼠标移动就显示，等于把 (0,0) 当成了光标";

    moveOver(*overlay, QPoint{110, 160});
    EXPECT_TRUE(overlay->isMagnifierVisible()) << "悬停挑窗口时不显示：挑起始角那一下看不到像素";

    // 拖出一块再松开：选区落定、抓点摆出来之后它仍该跟着光标
    buttonAt(*overlay, QEvent::MouseButtonPress, QPoint{100, 150});
    moveOver(*overlay, QPoint{200, 230});
    buttonAt(*overlay, QEvent::MouseButtonRelease, QPoint{200, 230});
    EXPECT_TRUE(overlay->isMagnifierVisible()) << "选区落定之后放大镜就没了：拉手柄对齐边线时没有读数";
}

/**
 * @brief 钉住读数取的是「这块屏的原点 + 缩放因子」折算后的那一帧像素
 * @details 遮罩记的是这块屏内的局部点，冻帧是物理像素。少减原点会在副屏上偏一整屏，
 *          少乘缩放会在高 DPI 上偏到左上角——两种都错得毫无声息，只能把颜色本身钉住。
 */
TEST_F(CaptureOverlayFixture, ReadoutFollowsTheFramePixelNotTheLocalPoint)
{
    QImage frame{QSize{kScreenGeometry.width() * 2, kScreenGeometry.height() * 2}, QImage::Format_ARGB32};
    frame.fill(Qt::black);
    const QPoint local{100, 150};
    frame.setPixelColor(QPoint{local.x() * 2, local.y() * 2}, QColor{10, 200, 20}); // 乘了缩放才是这一格
    frame.setPixelColor(local, QColor{200, 20, 20});                                // 只减不乘会读到这一格

    auto overlay = makeOverlay(frame, 2.0);
    moveOver(*overlay, local);

    const QColor expected{10, 200, 20};
    EXPECT_EQ(overlay->colorUnderCursor(), expected) << "读数没按缩放因子折到帧像素";
}

/**
 * @brief 钉住放大镜摆在光标右下、贴到屏角时翻到左上，并且整枚留在屏内
 * @details 摆在光标右下是为了不挡住正要看的像素；翻边判据失效时放大镜有一半跑到屏外，
 *          用户照着抄的格子根本不是光标那一片。
 */
TEST_F(CaptureOverlayFixture, MagnifierFlipsAsideAtTheScreenCorner)
{
    auto         overlay = makeOverlay();
    const QPoint nearCorner{kScreenGeometry.width() - GyanisScribe::Pick::kMagnifierViewEdge - 40, kScreenGeometry.height() - GyanisScribe::Pick::kMagnifierViewEdge - 40};
    moveOver(*overlay, nearCorner);
    const auto roomy = overlay->magnifierRect();
    EXPECT_TRUE(roomy.left() > nearCorner.x()) << "摆得下时放大镜该在光标右下";
    EXPECT_TRUE(roomy.top() > nearCorner.y());

    const QPoint corner{kScreenGeometry.width() - 2, kScreenGeometry.height() - 2};
    moveOver(*overlay, corner);
    const auto flipped = overlay->magnifierRect();
    EXPECT_TRUE(flipped.right() < corner.x()) << "贴到屏角时没翻到光标左侧";
    EXPECT_TRUE(flipped.bottom() < corner.y()) << "贴到屏底时没翻到光标上方";

    const QRect widgetRect{QPoint{0, 0}, kScreenGeometry.size()};
    EXPECT_TRUE(widgetRect.contains(flipped)) << "放大镜整枚该留在这块屏内";
}

/// 投一条键盘事件给遮罩
void pressKey(CaptureOverlay &overlay, const Qt::Key key, const Qt::KeyboardModifiers modifiers = Qt::KeyboardModifier{})
{
    QKeyEvent event{QEvent::KeyPress, key, modifiers};
    QApplication::sendEvent(&overlay, &event);
}

/// 拖出一块选区：按下 anchor，拖到 current 后放开
void dragOut(CaptureOverlay &overlay, const QPoint &anchor, const QPoint &current)
{
    buttonAt(overlay, QEvent::MouseButtonPress, anchor);
    moveOver(overlay, current);
    buttonAt(overlay, QEvent::MouseButtonRelease, current);
}

/**
 * @brief 钉住裸按 C 轮换读数写法，带修饰键的不认
 * @details 只影响遮罩上这一枚读数，不写回配置：临时换一种写法看一眼，不该顺手改掉用户的默认档。
 */
TEST_F(CaptureOverlayFixture, ColorKeyCyclesTheReadoutFormat)
{
    auto overlay = makeOverlay();
    EXPECT_EQ(overlay->readoutFormat(), ColorFormat::Hex) << "默认档应当是 Hex";

    pressKey(*overlay, Qt::Key_C);
    EXPECT_EQ(overlay->readoutFormat(), ColorFormat::Rgb);
    pressKey(*overlay, Qt::Key_C);
    EXPECT_EQ(overlay->readoutFormat(), ColorFormat::Hsl);
    pressKey(*overlay, Qt::Key_C);
    EXPECT_EQ(overlay->readoutFormat(), ColorFormat::Hex) << "三档应当轮回来";

    pressKey(*overlay, Qt::Key_C, Qt::ShiftModifier);
    EXPECT_EQ(overlay->readoutFormat(), ColorFormat::Hex) << "Shift 已经归「挪右下角」，裸按才轮换";
}

/**
 * @brief 钉住方向键一格：单独按整块平移、按住 Shift 挪右下角，两者都不许改到另一件事
 * @details 松手之后想改一像素得重框一遍，这条键就是为了不必重框。平移改了大小、缩放挪了顶边，
 *          都是「看着按对了、交出去的却是另一块」。
 */
TEST_F(CaptureOverlayFixture, ArrowKeysNudgeTheLaidOutSelection)
{
    auto overlay = makeOverlay();
    dragOut(*overlay, QPoint{100, 100}, QPoint{140, 120});
    ASSERT_EQ(overlay->selectedRegion(), QRect(100, 100, 41, 21)) << "起点就不是这块";

    pressKey(*overlay, Qt::Key_Right);
    EXPECT_EQ(overlay->selectedRegion(), QRect(101, 100, 41, 21)) << "方向键没整块平移一格";

    pressKey(*overlay, Qt::Key_Down, Qt::ShiftModifier);
    EXPECT_EQ(overlay->selectedRegion(), QRect(101, 100, 41, 22)) << "Shift+方向键没把右下角往下扩一格";

    pressKey(*overlay, Qt::Key_Left, Qt::ShiftModifier);
    EXPECT_EQ(overlay->selectedRegion(), QRect(101, 100, 40, 22)) << "往里收一格该收窄而不是挪顶边";
}

/**
 * @brief 拒绝面：还没框出选区时方向键不许编出一块框
 * @details 与「划过光标不许产出选区」同一条规矩：键盘微调只对已经落成的那块动手。
 */
TEST_F(CaptureOverlayFixture, ArrowKeysWithoutASelectionYieldNothing)
{
    auto overlay = makeOverlay();
    overlay->setWindowCandidates(kCandidates);
    moveOver(*overlay, QPoint{300, 200});

    pressKey(*overlay, Qt::Key_Right);
    pressKey(*overlay, Qt::Key_Down, Qt::ShiftModifier);
    EXPECT_TRUE(overlay->selectedRegion().isNull()) << "没有选区时方向键凭空造出了一块";
}

/**
 * @brief 钉住选区贴到屏边时方向键就地停下，而不是把它推出这块屏
 * @details 平移的夹取按整块算：贴边再按一次，选区一格都不动。越出去的那一半用户看不见，
 *          交出去的成品却少了那几条像素。
 */
TEST_F(CaptureOverlayFixture, ArrowKeysStopAtTheScreenEdge)
{
    auto       overlay   = makeOverlay();
    const auto rightEdge = kScreenGeometry.width() - 41;
    dragOut(*overlay, QPoint{rightEdge, 50}, QPoint{kScreenGeometry.width() - 1, 60});
    const auto flush = overlay->selectedRegion();
    ASSERT_EQ(flush.right(), kScreenGeometry.width() - 1) << "没先造出贴边的选区，后面的判据就是空的";

    pressKey(*overlay, Qt::Key_Right);
    EXPECT_EQ(overlay->selectedRegion(), flush) << "贴到屏右边界还继续往屏外走";

    auto flushed = makeOverlay();
    dragOut(*flushed, QPoint{10, kScreenGeometry.height() - 10}, QPoint{30, kScreenGeometry.height() - 1});
    const auto atBottom = flushed->selectedRegion();
    ASSERT_EQ(atBottom.bottom(), kScreenGeometry.height() - 1) << "没先造出贴底的选区，后面的判据就是空的";

    pressKey(*flushed, Qt::Key_Down, Qt::ShiftModifier);
    EXPECT_EQ(flushed->selectedRegion(), atBottom) << "右下角不许越出这块屏";
}

/**
 * @brief 钉住「拖抓点是改这一块，而不是重新框一块」
 * @details 松手之后框已经落定，此时按下右下角拖动只能走抓点这条支路；
 *
 * 把它当成新框的起点会把整块丢掉，而画面上的感觉只是「拖了一下没反应」。
 */
TEST_F(CaptureOverlayFixture, DraggingABottomRightGripReshapesTheSameSelection)
{
    auto overlay = makeOverlay();
    dragOut(*overlay, QPoint{100, 100}, QPoint{140, 120});
    const auto settled = overlay->selectedRegion();
    ASSERT_EQ(settled, QRect(100, 100, 41, 21));

    buttonAt(*overlay, QEvent::MouseButtonPress, QPoint{140, 120});
    moveOver(*overlay, QPoint{160, 130});
    buttonAt(*overlay, QEvent::MouseButtonRelease, QPoint{160, 130});

    EXPECT_EQ(overlay->selectedRegion(), QRect(100, 100, 61, 31)) << "拖右下角没把这块改大";
}

/**
 * @brief 钉住按住选区内部是整块平移，大小一格都不许变
 */
TEST_F(CaptureOverlayFixture, DraggingInsideMovesTheSelectionWithoutResizing)
{
    auto overlay = makeOverlay();
    dragOut(*overlay, QPoint{100, 100}, QPoint{140, 120});

    buttonAt(*overlay, QEvent::MouseButtonPress, QPoint{120, 110});
    moveOver(*overlay, QPoint{140, 115});
    buttonAt(*overlay, QEvent::MouseButtonRelease, QPoint{140, 115});

    EXPECT_EQ(overlay->selectedRegion(), QRect(120, 105, 41, 21)) << "拖内部没挪位置，或顺手改了大小";
}

/**
 * @brief 钉住离选区很远的按下仍然开一块新的
 * @details 这是原有的框选走法，抓点不能把它抢走：认不到抓点就该回到「重新框」。

 */
TEST_F(CaptureOverlayFixture, PressingFarAwayStartsAFreshSelection)
{
    auto overlay = makeOverlay();
    dragOut(*overlay, QPoint{100, 100}, QPoint{140, 120});

    buttonAt(*overlay, QEvent::MouseButtonPress, QPoint{400, 400});
    moveOver(*overlay, QPoint{500, 450});
    buttonAt(*overlay, QEvent::MouseButtonRelease, QPoint{500, 450});

    EXPECT_EQ(overlay->selectedRegion(), QRect(400, 400, 101, 51)) << "远处按下没开出新的一块";
}
