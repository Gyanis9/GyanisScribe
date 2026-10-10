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
 * @brief 钉住预处理链的产出：灰度、尺寸按倍率，且不出现「只剩两色」
 * @details 不二值化不是疏忽：系统后端实测同一张图「原样 4 词、灰度+放大 4 词、再加二值化与
 *          中值滤波 0 词」。这条断言把中间调留着，实现里偷偷加回二值化就会红。
 */
TEST(OcrPreprocessor, PrepareChainGrayscalesAndEnlargesWithoutBinarizing)
{
    QImage colored(6, 6, QImage::Format_ARGB32);
    colored.fill(QColor(120, 120, 120));

    const auto prepared = prepareForRecognition(colored, 2);
    EXPECT_EQ(prepared.size(), QSize(12, 12));
    ASSERT_EQ(prepared.format(), QImage::Format_Grayscale8);
    const auto middle = grayAt(prepared, 0, 0);
    EXPECT_EQ(middle, 120) << "灰度值被改成了两色之一，说明链里混进了二值化（三色相同时权重和正好 256，灰度就等于原值）";
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
