// 真机上的指针形状检查：只在拿得出形状的那台机器上跑，其余环境按能力跳过。
// 这一格值得单独钉：GDI 那一段（尺寸面、两张位平面的行距、热点）在容器与离屏平台上都走不到，
// 只有真桌面给得出答案。掩码的合成规则不在这里验——那是 CursorPixels 的三条用例，
// 它们用自造的位平面跑，两边都编得过。

#include "Capture/QScreenCaptureSource.h"

#include "Capture/ScreenUtils.h"
#include "Core/Types.h"

#include <QApplication>
#include <QGuiApplication>
#include <QImage>
#include <QPoint>
#include <QScreen>

#include <gtest/gtest.h>

#include <memory>

using GyanisScribe::Capture::CursorSprite;
using GyanisScribe::Capture::drawCursorInto;
using GyanisScribe::Capture::QScreenCaptureSource;
using GyanisScribe::Core::CapturedImage;

namespace
{
    std::unique_ptr<QApplication> application;

    class QScreenCaptureSourceFixture : public ::testing::Test
    {
    protected:
        static void SetUpTestSuite()
        {
            // argv 与 argc 的缓冲要有静态存储期：Qt 只存指针不拷贝
            static int   argumentCount  = 1;
            static char  argumentZero[] = "TestQScreenCaptureSource";
            static char *arguments[]    = {argumentZero, nullptr};
            application                 = std::make_unique<QApplication>(argumentCount, arguments);
        }

        static void TearDownTestSuite()
        {
            application.reset();
        }
    };
} // namespace

/**
 * @brief 钉住真桌面上取出来的指针形状自洽
 * @details 尺寸在可信范围内、热点落在形状之内：这两条任何一条歪了，画进截图的就是错位或放大。
 *          刻意不断言「形状里有看得见的像素」——系统此刻用的是空白指针时那本来就该全透明，
 *          把机器的现场当成契约就会造出一条会自己变红的用例。合成规则由 CursorPixels 那三条钉。
 */
TEST_F(QScreenCaptureSourceFixture, GrabbedCursorShapeIsSelfConsistent)
{
    if (QGuiApplication::screens().isEmpty())
    {
        GTEST_SKIP() << "这个环境没有屏幕可枚举，取不到系统指针的形状";
    }

    const QScreenCaptureSource source;
    const auto                 sprite = source.grabCursor(QString());
    if (!sprite)
    {
        GTEST_SKIP() << "本平台此刻给不出指针形状（指针隐藏、或不是 Windows 后端）";
    }

    ASSERT_FALSE(sprite->image.isNull());
    EXPECT_GT(sprite->image.width(), 0);
    EXPECT_GT(sprite->image.height(), 0);
    EXPECT_LE(sprite->image.width(), 256) << "读出来的指针宽度过大，尺寸面多半解错了行序";
    EXPECT_LE(sprite->image.height(), 256) << "读出来的指针高度过大，单色指针的两张平面可能没折半";
    EXPECT_GE(sprite->hotspot.x(), 0) << "热点跑到了形状外，画出来的位置会整体偏移";
    EXPECT_GE(sprite->hotspot.y(), 0);
    EXPECT_LT(sprite->hotspot.x(), sprite->image.width());
    EXPECT_LT(sprite->hotspot.y(), sprite->image.height());
}

/**
 * @brief 钉住真机上「抓帧 + 画指针」这一条完整链路走通
 * @details 落点、缩放与裁切由前面那批纯函数用例钉死，这一条只证明真实来源给出的形状能画进
 *          真实抓出来的一帧，并且不改动帧自己的坐标系读数——那三格一变，标注层就会跟着错位。
 */
TEST_F(QScreenCaptureSourceFixture, CursorDrawsIntoARealFrameWithoutBreakingItsGeometry)
{
    if (QGuiApplication::screens().isEmpty())
    {
        GTEST_SKIP() << "这个环境没有屏幕可枚举，抓不到真实的一帧";
    }

    const QScreenCaptureSource source;
    const auto                 frame = source.grabScreen(QString());
    ASSERT_TRUE(frame.has_value()) << "真实抓帧都没成，这条判据无从谈起";
    const auto sprite = source.grabCursor(frame->screenName);
    if (!sprite)
    {
        GTEST_SKIP() << "本平台此刻给不出指针形状";
    }

    auto drawn = *frame;
    drawCursorInto(drawn, *sprite);

    EXPECT_EQ(drawn.image.size(), frame->image.size()) << "画完指针把帧的尺寸改了，落点算到了帧外";
    EXPECT_EQ(drawn.devicePixelRatio, frame->devicePixelRatio);
    EXPECT_EQ(drawn.logicalSize, frame->logicalSize);
}
