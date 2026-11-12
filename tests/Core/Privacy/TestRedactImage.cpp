// 静态图打码的用例：只改框内、框外逐像素不变，倍比映射正确，坏输入明确失败。

#include "Base/Types.h"
#include "Core/Annotation/AnnotationTool.h"
#include "Core/Privacy/RedactImage.h"

#include <QColor>
#include <QGuiApplication>
#include <QImage>
#include <QSize>

#include <gtest/gtest.h>

#include <array>
#include <cstring>
#include <memory>
#include <vector>

using GyanisScribe::Base::ErrorCode;
using GyanisScribe::Core::ToolKind;
using GyanisScribe::Core::Privacy::redactImage;

namespace
{
    std::unique_ptr<QGuiApplication> application;

    class RedactImageFixture : public ::testing::Test
    {
    protected:
        static void SetUpTestSuite()
        {
            static int   argumentCount  = 1;
            static char  argumentZero[] = "TestCoreRedactImage";
            static char *arguments[]    = {argumentZero, nullptr};
            application                 = std::make_unique<QGuiApplication>(argumentCount, arguments);
        }

        static void TearDownTestSuite()
        {
            application.reset();
        }
    };

    QImage makePattern(const QSize &size)
    {
        QImage image{size, QImage::Format_ARGB32_Premultiplied};
        for (int y = 0; y < image.height(); ++y)
        {
            auto *line = reinterpret_cast<QRgb *>(image.scanLine(y));
            for (int x = 0; x < image.width(); ++x)
            {
                line[x] = qRgba((x * 7 + y * 3) % 256, (x * 11) % 256, (y * 13) % 256, 255);
            }
        }
        return image;
    }

    bool samePixels(const QImage &left, const QImage &right)
    {
        if (left.size() != right.size())
        {
            return false;
        }
        for (int y = 0; y < left.height(); ++y)
        {
            if (std::memcmp(left.constScanLine(y), right.constScanLine(y), static_cast<std::size_t>(left.width()) * 4U) != 0)
            {
                return false;
            }
        }
        return true;
    }

    int changedPixels(const QImage &before, const QImage &after, const QRect &area)
    {
        int changed = 0;
        for (int y = area.top(); y <= area.bottom(); ++y)
        {
            for (int x = area.left(); x <= area.right(); ++x)
            {
                if (before.pixel(x, y) != after.pixel(x, y))
                {
                    ++changed;
                }
            }
        }
        return changed;
    }
} // namespace

TEST_F(RedactImageFixture, MosaicChangesOnlyTheGivenBox)
{
    const auto original = makePattern(QSize{120, 80});
    const auto before   = original.copy();
    const QRect box{20, 20, 40, 20};
    const auto  result = redactImage(original, std::array<QRect, 1>{box}, ToolKind::Mosaic, original.size(), 8);
    ASSERT_TRUE(result.has_value()) << result.error().message;
    EXPECT_EQ(result->appliedCount, 1);
    EXPECT_EQ(result->image.size(), original.size());

    const auto padded = box.adjusted(-2, -2, 2, 2).intersected(original.rect());
    EXPECT_GT(changedPixels(before, result->image, padded), 0) << "命中框里一个像素都没变";
    for (int y = 0; y < original.height(); ++y)
    {
        for (int x = 0; x < original.width(); ++x)
        {
            if (!padded.contains(x, y))
            {
                ASSERT_EQ(before.pixel(x, y), result->image.pixel(x, y)) << "框外像素被改了：" << x << "," << y;
            }
        }
    }
    EXPECT_TRUE(samePixels(original, before)) << "输入图被改动了";
}

TEST_F(RedactImageFixture, MapsPhysicalBoxesThroughTheLogicalSize)
{
    const auto original = makePattern(QSize{200, 120});
    const auto before   = original.copy();
    const auto result   = redactImage(original, std::array<QRect, 1>{QRect{20, 20, 60, 40}}, ToolKind::Mosaic, QSize{100, 60}, 8);
    ASSERT_TRUE(result.has_value()) << result.error().message;

    const QRect physicalPadded = QRect{16, 16, 68, 48}.intersected(original.rect());
    EXPECT_GT(changedPixels(before, result->image, physicalPadded), 0) << "倍比映射后的框里一个像素都没变";
    EXPECT_EQ(before.pixel(10, 10), result->image.pixel(10, 10));
    EXPECT_EQ(before.pixel(190, 110), result->image.pixel(190, 110));
    // 正确框在 16..84 × 16..64；150,100 落在「把物理框当成逻辑框再乘一次倍比」的 40..160 × 40..120 里，
    // 只按整张图的边界判不出倍比算错，必须拿框外但错缩放区内的这一点来咬
    EXPECT_EQ(before.pixel(150, 100), result->image.pixel(150, 100));
}

TEST_F(RedactImageFixture, BlurAlsoLeavesPixelsOutsideTheBoxAlone)
{
    const auto original = makePattern(QSize{120, 80});
    const auto before   = original.copy();
    const auto result   = redactImage(original, std::array<QRect, 1>{QRect{30, 20, 40, 30}}, ToolKind::Blur, original.size(), 8);
    ASSERT_TRUE(result.has_value()) << result.error().message;
    EXPECT_EQ(result->appliedCount, 1);
    EXPECT_GT(changedPixels(before, result->image, QRect{28, 18, 44, 34}), 0) << "模糊框里一个像素都没变";
    EXPECT_EQ(before.pixel(5, 5), result->image.pixel(5, 5));
    EXPECT_EQ(before.pixel(110, 70), result->image.pixel(110, 70));
    EXPECT_TRUE(samePixels(original, before)) << "输入图被改动了";
}

TEST_F(RedactImageFixture, RejectsInvalidInputs)
{
    const auto image = makePattern(QSize{40, 30});
    const auto boxes = std::array<QRect, 1>{QRect{5, 5, 10, 10}};

    EXPECT_FALSE(redactImage(QImage{}, boxes, ToolKind::Mosaic, image.size(), 8).has_value());
    EXPECT_FALSE(redactImage(image, std::span<const QRect>{}, ToolKind::Mosaic, image.size(), 8).has_value());
    EXPECT_FALSE(redactImage(image, boxes, ToolKind::Mosaic, QSize{}, 8).has_value());
    EXPECT_FALSE(redactImage(image, boxes, ToolKind::Rectangle, image.size(), 8).has_value());
    EXPECT_FALSE(redactImage(image, boxes, ToolKind::Mosaic, image.size(), 0).has_value());

    const auto outside = std::array<QRect, 1>{QRect{100, 100, 10, 10}};
    const auto rejected = redactImage(image, outside, ToolKind::Mosaic, image.size(), 8);
    ASSERT_FALSE(rejected.has_value());
    EXPECT_EQ(rejected.error().code, ErrorCode::InvalidArgument);
    EXPECT_FALSE(rejected.error().message.empty());
}

/// 部分框落在画布外时只把画布内的框交给引擎：计数跟着有效框走，不拿检测命中数充数
TEST_F(RedactImageFixture, CountsOnlyBoxesThatLandInsideTheCanvas)
{
    const auto original = makePattern(QSize{40, 30});
    const auto boxes    = std::array<QRect, 2>{QRect{5, 5, 10, 10}, QRect{100, 100, 10, 10}};
    const auto result   = redactImage(original, boxes, ToolKind::Mosaic, original.size(), 8);
    ASSERT_TRUE(result.has_value()) << result.error().message;
    EXPECT_EQ(result->appliedCount, 1);
}
