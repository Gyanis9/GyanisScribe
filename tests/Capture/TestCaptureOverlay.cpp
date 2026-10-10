// 遮罩的「悬停认出整扇窗口」：候选按这块屏的原点换成局部坐标、命中取压在最上面的那一个、
// 原地放开（没拖出选区）时采纳此刻亮着的那一块。事件用 sendEvent 直接投给控件，
// 判据不依赖「鼠标恰好在这个时刻移动到位」。
#include "Capture/CaptureOverlay.h"

#include <QApplication>
#include <QImage>
#include <QMouseEvent>
#include <QPoint>
#include <QRect>

#include <gtest/gtest.h>

#include <memory>
#include <vector>

using GyanisScribe::Capture::CaptureOverlay;

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
