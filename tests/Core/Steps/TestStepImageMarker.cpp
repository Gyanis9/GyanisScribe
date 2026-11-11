#include "Core/Steps/StepImageMarker.h"

#include <QColor>
#include <QGuiApplication>
#include <QImage>
#include <QPoint>
#include <QRect>

#include <gtest/gtest.h>

#include <memory>

using GyanisScribe::Core::annotateStepClick;

namespace
{
    /// 矩形内是否有一处不是 expected 的像素
    [[nodiscard]] bool containsNonColor(const QImage &image, const QRect &rect, const QColor &expected)
    {
        const auto area = rect.intersected(image.rect());
        for (auto y = area.top(); y <= area.bottom(); ++y)
        {
            for (auto x = area.left(); x <= area.right(); ++x)
            {
                if (image.pixelColor(x, y) != expected)
                {
                    return true;
                }
            }
        }
        return false;
    }

    std::unique_ptr<QGuiApplication> application;

    /// 序号要画字，字体后端要求应用对象就位：没有它 QPainter::drawText 会一直等字体初始化，用例根本跑不完
    class StepImageMarkerEnvironment : public ::testing::Environment
    {
    public:
        void SetUp() override
        {
            // argv 与 argc 的缓冲要有静态存储期：Qt 只存指针不拷贝
            static int   argumentCount  = 1;
            static char  argumentZero[] = "TestCoreSteps";
            static char *arguments[]    = {argumentZero, nullptr};
            qputenv("QT_QPA_PLATFORM", "offscreen");
            application = std::make_unique<QGuiApplication>(argumentCount, arguments);
        }

        void TearDown() override
        {
            application.reset();
        }
    };
} // namespace

[[maybe_unused]] const auto *const kStepImageMarkerEnvironment = ::testing::AddGlobalTestEnvironment(new StepImageMarkerEnvironment);

/// 标记不改尺寸；点击点周围出现品牌红环，圆外远处像素保持原样
TEST(StepImageMarker, DrawsRedRingAtTheClickPoint)
{
    QImage screen{400, 300, QImage::Format_ARGB32_Premultiplied};
    screen.fill(Qt::white);
    const auto marked = annotateStepClick(screen, QPoint(200, 150), 1.0, 3);
    ASSERT_EQ(marked.size(), screen.size());
    auto ring = false;
    for (auto x = 180; x <= 220; ++x)
    {
        const auto color = marked.pixelColor(x, 150);
        if (color.red() > 180 && color.green() < 120 && color.blue() < 120)
        {
            ring = true;
            break;
        }
    }
    EXPECT_TRUE(ring) << "点击点附近没有红色圆环";
    EXPECT_EQ(marked.pixelColor(5, 5), QColor(Qt::white)) << "标记改到了远处像素";
}

/// 点在画面外时不画、不改尺寸
TEST(StepImageMarker, IgnoresPointsOutsideTheImage)
{
    QImage screen{100, 100, QImage::Format_ARGB32_Premultiplied};
    screen.fill(Qt::white);
    EXPECT_EQ(annotateStepClick(screen, QPoint(500, 500), 1.0, 1), screen);
    EXPECT_EQ(annotateStepClick(screen, QPoint(-5, 50), 1.0, 1), screen) << "负坐标落在画面外，不该画标记";
    EXPECT_EQ(annotateStepClick(screen, QPoint(50, 100), 1.0, 1), screen) << "下边界外一格不该画标记";
}

/// 高倍屏按 dpr 把逻辑点与标记尺寸换成物理像素
TEST(StepImageMarker, ScalesTheMarkerByDevicePixelRatio)
{
    QImage screen{400, 300, QImage::Format_ARGB32_Premultiplied};
    screen.fill(Qt::white);
    const auto marked = annotateStepClick(screen, QPoint(100, 100), 2.0, 4);
    ASSERT_EQ(marked.size(), screen.size());

    auto ring = false;
    for (auto x = 160; x <= 240; ++x)
    {
        const auto color = marked.pixelColor(x, 200);
        if (color.red() > 180 && color.green() < 120 && color.blue() < 120)
        {
            ring = true;
            break;
        }
    }
    EXPECT_TRUE(ring) << "高倍屏的圆环没有按 dpr 放大到物理像素";
    EXPECT_EQ(marked.pixelColor(100, 100), QColor(Qt::white)) << "标记仍画在逻辑坐标上";
}

/// 序号画在圆环右上方，左下方仍是原色
TEST(StepImageMarker, DrawsTheStepNumberAboveAndRightOfTheRing)
{
    QImage screen{400, 300, QImage::Format_ARGB32_Premultiplied};
    screen.fill(Qt::white);
    const auto marked = annotateStepClick(screen, QPoint(200, 150), 1.0, 7);
    EXPECT_TRUE(containsNonColor(marked, QRect{198, 100, 60, 48}, QColor(Qt::white))) << "圆环右上方没有序号";
    EXPECT_FALSE(containsNonColor(marked, QRect{150, 172, 40, 25}, QColor(Qt::white))) << "圆环左下方不该有字";
}

/// 标记画在新图上，入参原图一个像素都不改
TEST(StepImageMarker, DoesNotMutateTheSource)
{
    QImage screen{200, 200, QImage::Format_ARGB32_Premultiplied};
    screen.fill(Qt::white);
    const auto copy = screen.copy();
    ASSERT_EQ(annotateStepClick(screen, QPoint(100, 100), 1.0, 2).size(), screen.size());
    EXPECT_EQ(screen, copy);
}
