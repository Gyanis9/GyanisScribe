#include "Gui/Theme/Motion.h"

#include <gtest/gtest.h>

#include <algorithm>

using GyanisScribe::Gui::cubicBezier;
using GyanisScribe::Gui::kPopSpring;
using GyanisScribe::Gui::kSnapSpring;
using GyanisScribe::Gui::springProgress;
using GyanisScribe::Gui::springSettleMs;
using GyanisScribe::Gui::SpringValue;

TEST(Motion, OverDampedSnapNeverOvershoots)
{
    qreal peak = 0.0;
    for (int ms = 0; ms <= 1000; ++ms)
    {
        peak = std::max(peak, springProgress(kSnapSpring, ms / 1000.0));
    }
    EXPECT_LE(peak, 1.0001);
    EXPECT_GE(springSettleMs(kSnapSpring), 200);
    EXPECT_LE(springSettleMs(kSnapSpring), 320);
}

TEST(Motion, UnderDampedPopOvershootsThenSettles)
{
    qreal peak = 0.0;
    for (int ms = 0; ms <= 1000; ++ms)
    {
        peak = std::max(peak, springProgress(kPopSpring, ms / 1000.0));
    }
    EXPECT_GT(peak, 1.10);
    EXPECT_LT(peak, 1.25);
    EXPECT_GE(springSettleMs(kPopSpring), 400);
    EXPECT_LE(springSettleMs(kPopSpring), 520);
}

TEST(Motion, CubicBezierHitsItsEndpointsAndStaysMonotonic)
{
    qreal previous = -1.0;
    for (int step = 0; step <= 100; ++step)
    {
        const qreal t = step / 100.0;
        const qreal y = cubicBezier(0.215, 0.61, 0.355, 1.0, t);
        EXPECT_GE(y, previous);
        previous = y;
    }
    EXPECT_NEAR(cubicBezier(0.215, 0.61, 0.355, 1.0, 0.0), 0.0, 1e-6);
    EXPECT_NEAR(cubicBezier(0.215, 0.61, 0.355, 1.0, 1.0), 1.0, 1e-6);
}

TEST(Motion, SpringValueCanBeSteppedWithoutARealClock)
{
    SpringValue spring;
    spring.setSpec(kPopSpring);
    spring.setImmediate(0.0);
    spring.animateTo(1.0);
    spring.advanceBy(springSettleMs(kPopSpring));
    EXPECT_DOUBLE_EQ(spring.value(), 1.0);
    EXPECT_FALSE(spring.isRunning());
}

TEST(Motion, DisabledAnimationsJumpStraightToTheTarget)
{
    SpringValue spring;
    spring.setAnimationsEnabled(false);
    spring.setImmediate(0.2);
    spring.animateTo(1.0);
    EXPECT_DOUBLE_EQ(spring.value(), 1.0);
    EXPECT_FALSE(spring.isRunning());
}
