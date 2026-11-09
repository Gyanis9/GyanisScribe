#include "Core/Geometry/ContinuousCorner.h"

#include <gtest/gtest.h>

#include <QImage>
#include <QPainter>
#include <QPainterPath>

namespace
{
    using GyanisScribe::Core::continuousCornerPath;
    using GyanisScribe::Core::cornerPathParameters;
    using GyanisScribe::Core::CornerShape;

    QImage renderMask(const QPainterPath &path, const QSize &size)
    {
        QImage image{size, QImage::Format_ARGB32_Premultiplied};
        image.fill(Qt::transparent);
        QPainter painter{&image};
        painter.setRenderHint(QPainter::Antialiasing, true);
        painter.setPen(Qt::NoPen);
        painter.setBrush(Qt::black);
        painter.drawPath(path);
        return image;
    }

    int differingAlphaPixels(const QImage &left, const QImage &right)
    {
        int count = 0;
        for (int y = 0; y < left.height(); ++y)
        {
            for (int x = 0; x < left.width(); ++x)
            {
                if (left.pixelColor(x, y).alpha() != right.pixelColor(x, y).alpha())
                {
                    ++count;
                }
            }
        }
        return count;
    }
} // namespace

TEST(ContinuousCorner, SmoothingZeroMatchesQtRoundedRect)
{
    const QRectF rect{0.0, 0.0, 200.0, 120.0};
    QPainterPath expected;
    expected.addRoundedRect(rect, 20.0, 20.0);

    const auto actual = continuousCornerPath(rect, CornerShape{20.0, 0.0});
    EXPECT_LE(differingAlphaPixels(renderMask(expected, QSize{200, 120}), renderMask(actual, QSize{200, 120})), 4);
}

TEST(ContinuousCorner, MatchesFigmaReferenceParameters)
{
    const auto params = cornerPathParameters(QSizeF{200.0, 120.0}, CornerShape{20.0, 0.8});
    EXPECT_NEAR(params.radius, 20.0, 1e-9);
    EXPECT_NEAR(params.p, 36.0, 1e-9);
    EXPECT_NEAR(params.arcSectionLength, 4.424634841649485, 1e-9);
    EXPECT_NEAR(params.a, 14.998929283105419, 1e-9);
    EXPECT_NEAR(params.b, 7.499464641552709, 1e-9);
    EXPECT_NEAR(params.c, 5.257311121191336, 1e-9);
    EXPECT_NEAR(params.d, 3.8196601125010505, 1e-9);
}

TEST(ContinuousCorner, ClampsRadiusAndSmoothingToBudget)
{
    const auto params = cornerPathParameters(QSizeF{30.0, 30.0}, CornerShape{12.0, 0.5});
    EXPECT_NEAR(params.radius, 12.0, 1e-9);
    EXPECT_NEAR(params.p, 15.0, 1e-9);
    EXPECT_NEAR(params.arcMeasure, 67.5, 1e-9);
}

/**
 * @brief 钉住负尺寸不再落进上下界颠倒的夹取
 * @details 半径的上界取短边一半；尺寸为负时上界也是负的，std::clamp 要求下界不大于上界。
 *          夹取后的半径与占位都必须回落成 0，路径按无圆角处理。
 */
TEST(ContinuousCorner, NegativeSizeYieldsNoCurvature)
{
    const auto params = cornerPathParameters(QSizeF{-20.0, 10.0}, CornerShape{8.0, 0.35});
    EXPECT_EQ(params.radius, 0.0);
    EXPECT_EQ(params.p, 0.0);
}

TEST(ContinuousCorner, PathStaysInsideRect)
{
    const QRectF rect{5.0, 7.0, 180.0, 100.0};
    const auto   bounds = continuousCornerPath(rect, CornerShape{20.0, 0.8}).boundingRect();
    EXPECT_GE(bounds.left(), rect.left() - 0.01);
    EXPECT_GE(bounds.top(), rect.top() - 0.01);
    EXPECT_LE(bounds.right(), rect.right() + 0.01);
    EXPECT_LE(bounds.bottom(), rect.bottom() + 0.01);
}

TEST(ContinuousCorner, StrokeStaysInsideTheGivenRect)
{
    QImage image{QSize{80, 60}, QImage::Format_ARGB32_Premultiplied};
    image.fill(Qt::transparent);
    {
        QPainter painter{&image};
        GyanisScribe::Core::paintContinuousCorner(painter, QRectF{2.0, 2.0, 76.0, 56.0}, CornerShape{16.0, 0.7}, QColor{255, 0, 0}, QPen{QColor{0, 0, 255}, 2.0});
    }
    EXPECT_EQ(image.pixelColor(1, 30).alpha(), 0);
    EXPECT_EQ(image.pixelColor(78, 30).alpha(), 0);
    EXPECT_EQ(image.pixelColor(40, 1).alpha(), 0);
    EXPECT_EQ(image.pixelColor(40, 58).alpha(), 0);
    EXPECT_GT(image.pixelColor(40, 30).alpha(), 0);
}

TEST(ContinuousCorner, ApplyMaskClearsCornersAndKeepsCenter)
{
    QImage image{QSize{80, 60}, QImage::Format_ARGB32_Premultiplied};
    image.fill(Qt::red);
    GyanisScribe::Core::applyCornerMask(image, CornerShape{16.0, 0.7});
    EXPECT_EQ(image.pixelColor(0, 0).alpha(), 0);
    EXPECT_EQ(image.pixelColor(40, 30).alpha(), 255);
}
