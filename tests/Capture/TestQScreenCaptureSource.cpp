// 真机上的指针形状检查：只在拿得出形状的那台机器上跑，其余环境按能力跳过。
// 这一格值得单独钉：GDI 那一段（尺寸面、两张位平面的行距、热点）在容器与离屏平台上都走不到，
// 只有真桌面给得出答案。掩码的合成规则不在这里验——那是 CursorPixels 的三条用例，
// 它们用自造的位平面跑，两边都编得过。

#include "Capture/QScreenCaptureSource.h"

#include "Capture/ScreenCaptureSourceFactory.h"
#include "Capture/ScreenUtils.h"
#include "Base/Types.h"

#ifdef Q_OS_WIN
#include "Capture/DxgiCaptureSource.h"
#endif

#include <QApplication>
#include <QElapsedTimer>
#include <QGuiApplication>
#include <QImage>
#include <QPoint>
#include <QScreen>
#include <QThread>
#include <QWidget>

#include <gtest/gtest.h>

#include <algorithm>
#include <iostream>
#include <memory>
#include <vector>

using GyanisScribe::Capture::CursorSprite;
using GyanisScribe::Capture::drawCursorInto;
using GyanisScribe::Capture::IScreenCaptureSource;
using GyanisScribe::Capture::QScreenCaptureSource;
using GyanisScribe::Base::CapturedImage;

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

/**
 * @brief 量两套抓帧原语在同一块屏上的中位耗时（读数用例，不断言）
 * @details 结论要进 README 的「性能实测」，所以量法必须写死在这一处：同一台机器、同一次运行、
 *          两块屏都抓同一块，每轮先把桌面搅动一下（挪一个临时小窗）保证拿到的是「新帧」那一格——
 *          桌面没变时 DXGI 交回缓存帧，量出来近乎 0，那个数字不反映录屏时真实要付的拷贝成本。
 *          它不断言时间：跑在什么机器上就报什么数，红了也只是因为抓不到帧。
 */
TEST_F(QScreenCaptureSourceFixture, ReportsMedianGrabCostOfBothPrimitives)
{
    if (QGuiApplication::screens().isEmpty())
    {
        GTEST_SKIP() << "这个环境没有屏幕可枚举，抓帧读数无从量起";
    }

    // 桌面搅动器：每轮挪一格并显示，制造一次桌面提交
    QWidget stirrer;
    stirrer.setFixedSize(80, 80);

    const auto measure = [&stirrer](IScreenCaptureSource &source, const int rounds)
    {
        std::vector<double> costs;
        for (int round = 0; round < rounds; ++round)
        {
            stirrer.move(100 + (round % 5) * 37, 100 + (round % 3) * 41);
            stirrer.show();
            QApplication::processEvents();
            QThread::msleep(30); // 让合成器把这一格提交上去

            QElapsedTimer timer;
            timer.start();
            const auto frame   = source.grabScreen(QString());
            const auto elapsed = static_cast<double>(timer.nsecsElapsed()) / 1'000'000.0;
            if (!frame)
            {
                continue;
            }
            costs.push_back(elapsed);
        }
        if (costs.empty())
        {
            return -1.0;
        }
        std::ranges::sort(costs);
        return costs[costs.size() / 2];
    };

    GyanisScribe::Capture::QScreenCaptureSource qtSource;
    const auto                                  qtCost = measure(qtSource, 12);

    auto       picked     = GyanisScribe::Capture::createScreenCaptureSource();
    const auto pickedCost = measure(*picked, 12);

    std::cout << "[读数] 抓一帧中位耗时：Qt grabWindow " << qtCost << "ms，当前选中的原语 " << pickedCost << "ms（各 12 轮，每轮搅动一次桌面）" << std::endl;
    EXPECT_GE(qtCost, 0.0) << "Qt 那一份一次都没抓成";
    EXPECT_GE(pickedCost, 0.0) << "当前选中的原语一次都没抓成";
}

/**
 * @brief 钉住工厂的选型：这套原语在这台机器上可用时就要选它
 * @details 「用的哪套原语」直接决定录屏帧率那一格，而选错了不会报错——只会在性能上悄悄
 *          退回 Qt 那一档。这一刻建不起来（远程会话、驱动限制）时按环境门控跳过：
 *          那是机器的现场，不是契约；跳过的那一份由对话框那边的回落分支自己兜。
 */
TEST_F(QScreenCaptureSourceFixture, FactoryPicksDxgiWheneverItIsUsable)
{
    const auto source = GyanisScribe::Capture::createScreenCaptureSource();
    ASSERT_NE(source, nullptr) << "工厂交回了空指针：下游每一步都要解引用它";

#ifdef Q_OS_WIN
    const GyanisScribe::Capture::DxgiCaptureSource probe;
    if (!probe.isUsable())
    {
        GTEST_SKIP() << "这台机器的 D3D11 设备建不起来，工厂按设计回落 Qt";
    }

    EXPECT_NE(dynamic_cast<GyanisScribe::Capture::DxgiCaptureSource *>(source.get()), nullptr) << "这套原语在这台机器上可用，工厂却没选它：录屏的抓帧成本会悄悄回到 Qt 那一档";

    if (QGuiApplication::screens().isEmpty())
    {
        GTEST_SKIP() << "没有屏幕可枚举，抓帧这一格验证不了";
    }

    const auto *dxgi = dynamic_cast<GyanisScribe::Capture::DxgiCaptureSource *>(source.get());
    ASSERT_NE(dxgi, nullptr);

    const auto frame = source->grabScreen(QString());
    ASSERT_TRUE(frame.has_value()) << "选中的是 DXGI 却抓不到帧：" << frame.error().message;
    EXPECT_GE(dxgi->framesFromDxgi(), 1) << "这一帧是从 Qt 的回落路径拿到的：DXGI 的认屏或取帧没真正跑通，选型等于白选";
    ASSERT_FALSE(frame->image.isNull());
    EXPECT_GT(frame->image.width(), 0);
    EXPECT_GT(frame->image.height(), 0);
    EXPECT_GT(frame->devicePixelRatio, 0.0);
    // 物理像素与逻辑尺寸之比就是缩放因子：两者对不上时，下游的裁图与标注会整体错位
    EXPECT_NEAR(static_cast<double>(frame->image.width()) / frame->logicalSize.width(), frame->devicePixelRatio, 0.01) << "帧的物理像素与逻辑尺寸的比值与缩放因子对不上";
    EXPECT_EQ(frame->image.devicePixelRatio(), 1.0) << "帧里的图必须按物理像素交回（dpr 标回 1），否则下游会既乘又除";
#endif
}
