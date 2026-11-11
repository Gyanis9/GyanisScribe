#include "Core/Steps/StepImageMarker.h"

#include <QColor>
#include <QDir>
#include <QGuiApplication>
#include <QImage>
#include <QPoint>
#include <QRect>
#include <QTemporaryDir>

#include <gtest/gtest.h>

#include <memory>

using GyanisScribe::Core::annotatedStepImage;
using GyanisScribe::Core::annotateStepClick;
using GyanisScribe::Core::RecordedStep;
using GyanisScribe::Core::StepKind;

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

    /// 穿过给定的横线上是否有品牌红的圆环像素
    [[nodiscard]] bool hasRedRingAt(const QImage &image, const QPoint &center)
    {
        const auto y = center.y();
        if (y < 0 || y >= image.height())
        {
            return false;
        }
        for (auto x = center.x() - 40; x <= center.x() + 40; ++x)
        {
            if (x < 0 || x >= image.width())
            {
                continue;
            }
            const auto color = image.pixelColor(x, y);
            if (color.red() > 180 && color.green() < 120 && color.blue() < 120)
            {
                return true;
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

/// 序号画在圆环右上方；取一块离圆环足够远的区域，删掉序号就没人画这些像素
TEST(StepImageMarker, DrawsTheStepNumberAboveAndRightOfTheRing)
{
    QImage screen{400, 300, QImage::Format_ARGB32_Premultiplied};
    screen.fill(Qt::white);
    const auto marked = annotateStepClick(screen, QPoint(200, 150), 1.0, 7);
    // 圆环外沿离圆心 17 像素，这块区域的最近角离圆心 28 像素，不会借到圆环的颜色
    EXPECT_TRUE(containsNonColor(marked, QRect{216, 116, 12, 12}, QColor(Qt::white))) << "圆环右上方没有序号";
    EXPECT_FALSE(containsNonColor(marked, QRect{150, 172, 40, 25}, QColor(Qt::white))) << "圆环左下方不该有字";
}

/// 点击贴着上边或右边时，序号圆牌被夹回画面内，不会被裁掉
TEST(StepImageMarker, ClampsTheStepNumberInsideTheImageNearEdges)
{
    QImage screen{400, 300, QImage::Format_ARGB32_Premultiplied};
    screen.fill(Qt::white);

    // 点落在右上角：不夹的话圆牌整体在图外，这块只属于圆牌的区域会保持白底
    const auto corner = annotateStepClick(screen, QPoint(399, 0), 1.0, 12);
    EXPECT_TRUE(containsNonColor(corner, QRect{370, 10, 10, 10}, QColor(Qt::white))) << "贴在上边的序号被裁掉了";

    // 点贴着右边中部：圆牌往左夹，序号仍完整可见
    const auto rightEdge = annotateStepClick(screen, QPoint(399, 150), 1.0, 3);
    EXPECT_TRUE(containsNonColor(rightEdge, QRect{374, 124, 10, 10}, QColor(Qt::white))) << "贴在右边的序号被裁掉了";
}

/// 导出前按步骤读原图：点击步骤补画标记，其它来源原样返回，没图时给空图
TEST(StepImageMarker, AnnotatedStepImageMarksClicksAndLeavesOtherKindsAlone)
{
    QTemporaryDir dir;
    ASSERT_TRUE(dir.isValid());
    QImage screen{400, 300, QImage::Format_ARGB32_Premultiplied};
    screen.fill(Qt::white);
    const auto path = QDir{dir.path()}.filePath(QStringLiteral("step-001.png"));
    ASSERT_TRUE(screen.save(path, "PNG"));

    RecordedStep clickStep;
    clickStep.kind              = StepKind::Click;
    clickStep.imagePath         = path;
    clickStep.clickPointInImage = QPoint{200, 150};
    const auto marked           = annotatedStepImage(clickStep, 9);
    ASSERT_EQ(marked.size(), screen.size());
    EXPECT_TRUE(hasRedRingAt(marked, QPoint{200, 150})) << "读出来的截图没有画点击圆环";
    EXPECT_TRUE(containsNonColor(marked, QRect{216, 116, 12, 12}, QColor(Qt::white))) << "序号没有用传入的显示序号";
    EXPECT_EQ(marked.pixelColor(5, 5), QColor(Qt::white)) << "标记改到了远处像素";

    // 截图所在屏的缩放因子随步骤带进来，逻辑点先换成物理像素再画
    auto hidpiStep                  = clickStep;
    hidpiStep.clickPointInImage     = QPoint{100, 100};
    hidpiStep.imageDevicePixelRatio = 2.0;
    EXPECT_TRUE(hasRedRingAt(annotatedStepImage(hidpiStep, 1), QPoint{200, 200})) << "画标记没用步骤里的缩放因子";

    // 窗口切换与手动补步不画标记，返回读出来的原图
    QImage stored;
    ASSERT_TRUE(stored.load(path));
    auto windowStep = clickStep;
    windowStep.kind = StepKind::WindowChange;
    EXPECT_EQ(annotatedStepImage(windowStep, 9), stored) << "非点击步骤不该动截图";

    // 没有截图路径时返回空图，由调用方决定占位
    auto noImage = clickStep;
    noImage.imagePath.clear();
    EXPECT_TRUE(annotatedStepImage(noImage, 9).isNull());
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
