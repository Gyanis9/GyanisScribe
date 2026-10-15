// 窗口几何换算的直测：两套读数互指同一个点，取整规则不许在某一边单独改。
// 期望值全部手算写死，不叫被测函数自己算期望（那条路会让规范怎么改都绿）。
#include "Platform/WindowGeometry.h"

#include <QPoint>
#include <QRect>
#include <QString>

#include <gtest/gtest.h>

#include <vector>

using GyanisScribe::Platform::logicalPointToPhysicalGlobal;
using GyanisScribe::Platform::logicalToPhysicalGlobal;
using GyanisScribe::Platform::monitorIndexForPoint;
using GyanisScribe::Platform::MonitorMapping;
using GyanisScribe::Platform::physicalToLogicalGlobal;

namespace
{
    /// 主屏：150% 缩放，物理 2880×1620 对逻辑 1920×1080，原点都在 (0,0)
    const MonitorMapping primaryAtOneFifty{QStringLiteral(R"(\\.\DISPLAY1)"), QRect{0, 0, 2880, 1620}, QRect{0, 0, 1920, 1080}, 1.5};

    /// 副屏：100% 缩放，物理原点接在主屏右边，逻辑原点接在逻辑右边——两边不同倍数正是最容易串位的那一档
    const MonitorMapping secondAtOneHundred{QStringLiteral(R"(\\.\DISPLAY2)"), QRect{2880, 0, 1920, 1080}, QRect{1920, 0, 1920, 1080}, 1.0};

    /// 主屏上的一扇窗口：逻辑 (100,50) 起，400×200
    const QRect windowOnPrimary{100, 50, 400, 200};
} // namespace

/**
 * @brief 钉住逻辑→物理这一边的手算值（含非零屏原点的那一块）
 * @details 150% 下 100 逻辑格是 150 物理格；副屏的窗口位置要同时减掉这块屏自己的物理原点再加回来，
 *          少任何一步都会把窗口摆到别的屏上。
 */
TEST(WindowGeometry, MapsLogicalOntoPhysicalAtBothMonitors)
{
    const auto onPrimary = logicalToPhysicalGlobal(windowOnPrimary, primaryAtOneFifty);
    ASSERT_TRUE(onPrimary.has_value()) << onPrimary.error().message;
    EXPECT_EQ(*onPrimary, QRect(150, 75, 600, 300));

    const QRect windowOnSecond{2000, 100, 300, 150};
    const auto  onSecond = logicalToPhysicalGlobal(windowOnSecond, secondAtOneHundred);
    ASSERT_TRUE(onSecond.has_value()) << onSecond.error().message;
    EXPECT_EQ(*onSecond, QRect(2960, 100, 300, 150));
}

/**
 * @brief 钉住物理→逻辑这一边的手算值
 * @details 与上一条互指：同一条窗口边界在两套坐标里必须落到同一个矩形，
 *          只改一边的取整就会得到「高亮框与像素错位」这种没人报的缺陷。
 */
TEST(WindowGeometry, MapsPhysicalBackOntoLogical)
{
    const auto onPrimary = physicalToLogicalGlobal(QRect{150, 75, 600, 300}, primaryAtOneFifty);
    ASSERT_TRUE(onPrimary.has_value()) << onPrimary.error().message;
    EXPECT_EQ(*onPrimary, windowOnPrimary);

    const auto onSecond = physicalToLogicalGlobal(QRect{2960, 100, 300, 150}, secondAtOneHundred);
    ASSERT_TRUE(onSecond.has_value()) << onSecond.error().message;
    EXPECT_EQ(*onSecond, QRect(2000, 100, 300, 150));
}

/**
 * @brief 钉住取整方向：起点向下、终点向上，换算回来只会包住原物不会切掉一行
 * @details 151 物理格在 150% 下是 100.67 逻辑格：起点取 100、终点取 168（上整），
 *          两头都向下取整会得到 67 格而不是 68 格，窗口最后那一列像素就丢了。
 */
TEST(WindowGeometry, RoundsOutwardSoNoPixelIsLost)
{
    const auto logical = physicalToLogicalGlobal(QRect{151, 75, 101, 51}, primaryAtOneFifty);
    ASSERT_TRUE(logical.has_value()) << logical.error().message;
    EXPECT_EQ(*logical, QRect(100, 50, 68, 34));

    const auto back = logicalToPhysicalGlobal(*logical, primaryAtOneFifty);
    ASSERT_TRUE(back.has_value()) << back.error().message;
    EXPECT_TRUE(back->contains(QRect{151, 75, 101, 51})) << "换算回来包不住原矩形：拿到的是 " << back->x() << ',' << back->y() << ' ' << back->width() << '×' << back->height();
}

/**
 * @brief 钉住 200% 缩放的整数倍那一路
 */
TEST(WindowGeometry, MapsAtTwoHundredPercent)
{
    const MonitorMapping scaled{QStringLiteral(R"(\\.\DISPLAY1)"), QRect{0, 0, 3840, 2160}, QRect{0, 0, 1920, 1080}, 2.0};

    const auto physical = logicalToPhysicalGlobal(QRect{10, 20, 30, 40}, scaled);
    ASSERT_TRUE(physical.has_value()) << physical.error().message;
    EXPECT_EQ(*physical, QRect(20, 40, 60, 80));

    // 21 物理格是 10.5 逻辑格：起点向下取 10，终点 (21+31)/2=26 向上取整仍是 26
    const auto logical = physicalToLogicalGlobal(QRect{21, 41, 31, 41}, scaled);
    ASSERT_TRUE(logical.has_value()) << logical.error().message;
    EXPECT_EQ(*logical, QRect(10, 20, 16, 21));
}

/**
 * @brief 拒绝面：缩放因子非正一律拒，不按 1 猜
 * @details 按 1 猜会把 150% 的屏算成 2/3 大小，而调用方收到的仍是一个「算出来了」的矩形，错处没人知道。
 */
TEST(WindowGeometry, RejectsNonPositiveScaleInsteadOfGuessing)
{
    for (const auto ratio: {0.0, -1.5})
    {
        MonitorMapping broken{QStringLiteral(R"(\\.\DISPLAY1)"), QRect{0, 0, 2880, 1620}, QRect{0, 0, 1920, 1080}, ratio};

        const auto forward = logicalToPhysicalGlobal(windowOnPrimary, broken);
        ASSERT_FALSE(forward.has_value()) << "缩放因子 " << ratio << " 被当成能算";
        EXPECT_EQ(forward.error().code, GyanisScribe::Base::ErrorCode::InvalidArgument);
        EXPECT_NE(forward.error().message.find("缩放因子"), std::string::npos) << forward.error().message;

        const auto backward = physicalToLogicalGlobal(QRect{150, 75, 600, 300}, broken);
        ASSERT_FALSE(backward.has_value()) << "缩放因子 " << ratio << " 被当成能算（反方向）";
        EXPECT_EQ(backward.error().code, GyanisScribe::Base::ErrorCode::InvalidArgument);
    }
}

/**
 * @brief 钉住逻辑点→物理点的手算值：按矩形起点那条规则向下取整，两块屏各验一次
 * @details 150% 下逻辑 (101,51) 是物理 151.5,76.5：起点规则取 (151,76)。若图省事四舍五入，
 *          同一个点在这条换算里是 (152,77)、在矩形换算里是 (151,76)，边界那一格两边各说各话。
 *          副屏那一路同时钉住「先减这块屏的逻辑原点再加回它的物理原点」，少一步就会串到主屏上。
 */
TEST(WindowGeometry, MapsLogicalPointOntoPhysicalWithTheStartCornerRule)
{
    const auto onPrimary = logicalPointToPhysicalGlobal(QPoint{101, 51}, primaryAtOneFifty);
    ASSERT_TRUE(onPrimary.has_value()) << onPrimary.error().message;
    EXPECT_EQ(*onPrimary, QPoint(151, 76)) << "起点取整的方向不对：应该向下取，不能四舍五入";

    // 与矩形换算互指：点当成 1×1 矩形的起点，两条路必须落到同一个像素
    const auto asRect = logicalToPhysicalGlobal(QRect(QPoint{101, 51}, QSize{1, 1}), primaryAtOneFifty);
    ASSERT_TRUE(asRect.has_value());
    EXPECT_EQ(*onPrimary, asRect->topLeft()) << "点那条与矩形那条算出了两个起点，边界上的命中会两边各说各话";

    const auto onSecond = logicalPointToPhysicalGlobal(QPoint{2000, 100}, secondAtOneHundred);
    ASSERT_TRUE(onSecond.has_value()) << onSecond.error().message;
    EXPECT_EQ(*onSecond, QPoint(2960, 100)) << "副屏没减自己的逻辑原点，点被算到了主屏的坐标里";
}

/**
 * @brief 拒绝面：点换算和矩形换算同一条闸，缩放因子非正一律拒
 */
TEST(WindowGeometry, RejectsPointConversionWithNonPositiveScale)
{
    MonitorMapping broken{QStringLiteral(R"(\\.\DISPLAY1)"), QRect{0, 0, 2880, 1620}, QRect{0, 0, 1920, 1080}, 0.0};

    const auto rejected = logicalPointToPhysicalGlobal(QPoint{10, 10}, broken);
    ASSERT_FALSE(rejected.has_value()) << "缩放因子 0 被当成能算";
    EXPECT_EQ(rejected.error().code, GyanisScribe::Base::ErrorCode::InvalidArgument);
}

/**
 * @brief 钉住跨屏窗口按中心点认主，以及接缝与拔屏那两种出口
 * @details 接缝上的点按声明顺序取第一块：两处规则不一致时，同一个窗口两次枚举会落到不同的屏上。
 */
TEST(WindowGeometry, PicksTheMonitorHoldingTheWindowCenter)
{
    const std::vector<MonitorMapping> monitors{primaryAtOneFifty, secondAtOneHundred};

    EXPECT_EQ(monitorIndexForPoint(QPoint{100, 100}, monitors), 0U);
    EXPECT_EQ(monitorIndexForPoint(QPoint{3000, 500}, monitors), 1U);
    // 拔屏之后的陈旧读数：一块都不含这个点
    EXPECT_EQ(monitorIndexForPoint(QPoint{-5, 100}, monitors), monitors.size());

    // 重叠那一种（读数来历不明或虚拟位置重排时会给出来）：按声明顺序取第一块，
    // 规则必须唯一，否则同一个窗口两次枚举会落到不同的屏上、算出两个位置
    const MonitorMapping              overlapping{QStringLiteral(R"(\\.\DISPLAY3)"), QRect{2000, 0, 1920, 1080}, QRect{1333, 0, 1280, 720}, 1.5};
    const std::vector<MonitorMapping> pair{primaryAtOneFifty, overlapping};
    EXPECT_EQ(monitorIndexForPoint(QPoint{2100, 500}, pair), 0U);
}
