#include "Ocr/OcrPreprocessor.h"

#include "Ocr/OcrTypes.h"

#include <QColor>
#include <QImage>

#include <gtest/gtest.h>

#include <vector>

using namespace GyanisScribe::Ocr;

namespace
{
    /// 一块纯色小图
    QImage solid(QColor color, int width = 8, int height = 8)
    {
        QImage image(width, height, QImage::Format_ARGB32);
        image.fill(color);
        return image;
    }

    /// 灰度图的取值，越界时返回 -1，方便断言「只可能是 0 或 255」
    int grayAt(const QImage &image, int x, int y)
    {
        EXPECT_EQ(image.format(), QImage::Format_Grayscale8) << "预处理链的产出必须是灰度图，否则像素判据不成立";
        if (x < 0 || y < 0 || x >= image.width() || y >= image.height())
        {
            return -1;
        }
        return static_cast<int>(*(reinterpret_cast<const quint8 *>(image.constScanLine(y)) + x));
    }

    QImage grayField(int width, int height, quint8 value)
    {
        QImage     image(width, height, QImage::Format_Grayscale8);
        auto      *line   = reinterpret_cast<quint8 *>(image.scanLine(0));
        const auto stride = static_cast<int>(image.bytesPerLine());
        for (int y = 0; y < height; ++y)
        {
            for (int x = 0; x < width; ++x)
            {
                line[y * stride + x] = value;
            }
        }
        return image;
    }

    void setGrayPixel(QImage &image, int x, int y, quint8 value)
    {
        *(reinterpret_cast<quint8 *>(image.scanLine(y)) + x) = value;
    }
} // namespace

/**
 * @brief 钉住灰度权重的整数算式：红绿蓝与黑白的手算值
 */
TEST(OcrPreprocessor, GrayscaleUsesIntegerWeights)
{
    // 权重 77/150/29 之和正好 256，故白色恒为 255；单通道值 = 权重×255>>8
    EXPECT_EQ(grayAt(toGrayscale(solid(QColor(255, 255, 255))), 3, 3), 255);
    EXPECT_EQ(grayAt(toGrayscale(solid(QColor(0, 0, 0))), 3, 3), 0);
    EXPECT_EQ(grayAt(toGrayscale(solid(QColor(255, 0, 0))), 3, 3), (77 * 255) >> 8);
    EXPECT_EQ(grayAt(toGrayscale(solid(QColor(0, 255, 0))), 3, 3), (150 * 255) >> 8);
    EXPECT_EQ(grayAt(toGrayscale(solid(QColor(0, 0, 255))), 3, 3), (29 * 255) >> 8);

    EXPECT_TRUE(toGrayscale(QImage()).isNull()) << "空图要交回空图，而不是 0×0 的假图";
    EXPECT_EQ(toGrayscale(toGrayscale(solid(QColor(10, 20, 30)))).size(), QSize(8, 8)) << "二次转换不该改变尺寸";
}

/**
 * @brief 钉住二值化在阈值两侧的判定与「等于阈值判白」
 */
TEST(OcrPreprocessor, BinarizeSplitsAtThresholdWithEqualGoingWhite)
{
    QImage source = grayField(8, 8, 127);
    setGrayPixel(source, 4, 4, 128);
    setGrayPixel(source, 5, 5, 255);
    setGrayPixel(source, 6, 6, 1);

    const auto binary = binarize(source, 128);
    EXPECT_EQ(grayAt(binary, 0, 0), 0) << "127 在阈值下方该判黑";
    EXPECT_EQ(grayAt(binary, 4, 4), 255) << "正好等于阈值判白：把边界算进黑侧会让细字变粗粘连";
    EXPECT_EQ(grayAt(binary, 5, 5), 255);
    EXPECT_EQ(grayAt(binary, 6, 6), 0);

    // 换阈值后同一个像素应翻面：证明阈值真的被用上了，而不是写死 128
    EXPECT_EQ(grayAt(binarize(source, 100), 0, 0), 255);
}

/**
 * @brief 钉住放大的倍率、硬边与上下限夹取
 */
TEST(OcrPreprocessor, EnlargeMultipliesSizeAndClampsFactor)
{
    const auto enlarged = enlarge(grayField(4, 4, 0), 2);
    EXPECT_EQ(enlarged.size(), QSize(8, 8));
    EXPECT_EQ(grayAt(enlarged, 0, 0), 0);
    EXPECT_EQ(grayAt(enlarged, 7, 7), 0) << "硬边放大后整块都该是同一个值";

    EXPECT_EQ(enlarge(grayField(4, 4, 255), 1).size(), QSize(4, 4)) << "倍率 1 就是原样";
    EXPECT_EQ(enlarge(grayField(4, 4, 255), 0).size(), QSize(4, 4)) << "0 倍被夹成 1，而不是把图变没";
    EXPECT_EQ(enlarge(grayField(4, 4, 255), 999).size(), QSize(4 * kMaximumEnlargeFactor, 4 * kMaximumEnlargeFactor)) << "超大倍率要停在上限";
}

/**
 * @brief 钉住中值滤波「抹掉孤立噪点、留住成片笔画」这一对
 */
TEST(OcrPreprocessor, DenoiseRemovesIsolatedSpeckAndKeepsStroke)
{
    QImage speckled = grayField(9, 9, 255);
    setGrayPixel(speckled, 4, 4, 0); // 孤立黑点
    for (int y = 1; y <= 3; ++y)
    {
        for (int x = 6; x <= 8; ++x)
        {
            setGrayPixel(speckled, x, y, 0); // 3×3 黑块
        }
    }

    const auto cleaned = denoise(speckled, 1);
    EXPECT_EQ(grayAt(cleaned, 4, 4), 255) << "孤立噪点没被抹掉，中值滤波没生效";
    EXPECT_EQ(grayAt(cleaned, 7, 2), 0) << "成片的笔画被抹掉了，去噪太狠";

    EXPECT_EQ(denoise(speckled, 0).size(), speckled.size()) << "半径 0 应当原样返回";
    EXPECT_EQ(grayAt(denoise(speckled, 99), 4, 4), 255) << "半径要夹在上限内，否则窗口会吃掉整幅图";
}

/**
 * @brief 钉住整条预处理链的产出：尺寸按倍率、取值只剩黑白两档
 */
TEST(OcrPreprocessor, PrepareChainOutputsBinarizedImageAtRequestedSize)
{
    const auto prepared = prepareForRecognition(solid(QColor(0, 0, 0), 6, 6), 2, 128);
    EXPECT_EQ(prepared.size(), QSize(12, 12));
    ASSERT_EQ(prepared.format(), QImage::Format_Grayscale8);
    for (int y = 0; y < prepared.height(); ++y)
    {
        for (int x = 0; x < prepared.width(); ++x)
        {
            const int value = grayAt(prepared, x, y);
            EXPECT_TRUE(value == 0 || value == 255) << "二值化之后出现了中间值 " << value;
        }
    }
    EXPECT_TRUE(prepareForRecognition(QImage()).isNull());
}

/**
 * @brief 钉住结果文本的拼接形状：词间空格、行间换行
 */
TEST(OcrTypes, ResultTextJoinsWordsWithSpacesAndLinesWithNewlines)
{
    OcrResult result;
    OcrLine   first;
    first.words = {OcrWord{QStringLiteral("hello"), QRect(0, 0, 10, 10)}, OcrWord{QStringLiteral("world"), QRect(12, 0, 10, 10)}};
    OcrLine second;
    second.words = {OcrWord{QStringLiteral("!"), QRect(0, 12, 4, 10)}};
    result.lines = {first, second};

    EXPECT_EQ(first.text(), QStringLiteral("hello world")) << "词之间必须补空格，否则两个词粘成一段";
    EXPECT_EQ(result.text(), QStringLiteral("hello world\n!"));
    EXPECT_FALSE(result.isEmpty());

    const OcrResult empty;
    EXPECT_TRUE(empty.isEmpty());
    EXPECT_TRUE(empty.text().isEmpty());
}
