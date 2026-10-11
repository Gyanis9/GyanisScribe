#include "Annotation/ImageBlur.h"

#include <QRgb>

#include <algorithm>
#include <vector>

namespace GyanisScribe::Annotation
{
    namespace
    {
        /// 一像素的平均值：把四个通道各按窗口求和再除
        [[nodiscard]] QRgb averageOf(const qint64 sumRed, const qint64 sumGreen, const qint64 sumBlue, const qint64 sumAlpha, const qint64 count)
        {
            return qRgba(static_cast<int>(sumRed / count), static_cast<int>(sumGreen / count), static_cast<int>(sumBlue / count), static_cast<int>(sumAlpha / count));
        }

        /// 沿行滑一遍窗口；行首与行尾按夹取采样，避免四边渗黑
        void blurRows(QImage &image, const int radius)
        {
            const auto width  = image.width();
            const auto span   = 2 * radius + 1;
            auto       buffer = std::vector<QRgb>(static_cast<std::size_t>(width));

            for (int y = 0; y < image.height(); ++y)
            {
                auto *line = reinterpret_cast<QRgb *>(image.scanLine(y));

                qint64 sumRed   = 0;
                qint64 sumGreen = 0;
                qint64 sumBlue  = 0;
                qint64 sumAlpha = 0;
                for (int offset = -radius; offset <= radius; ++offset)
                {
                    const auto pixel = line[std::clamp(offset, 0, width - 1)];
                    sumRed += qRed(pixel);
                    sumGreen += qGreen(pixel);
                    sumBlue += qBlue(pixel);
                    sumAlpha += qAlpha(pixel);
                }

                for (int x = 0; x < width; ++x)
                {
                    buffer[static_cast<std::size_t>(x)] = averageOf(sumRed, sumGreen, sumBlue, sumAlpha, span);

                    // 窗口右移一格：去掉最左那个像素、加上右边新进来的那个
                    const auto leaving  = line[std::clamp(x - radius, 0, width - 1)];
                    const auto entering = line[std::clamp(x + radius + 1, 0, width - 1)];
                    sumRed += qRed(entering) - qRed(leaving);
                    sumGreen += qGreen(entering) - qGreen(leaving);
                    sumBlue += qBlue(entering) - qBlue(leaving);
                    sumAlpha += qAlpha(entering) - qAlpha(leaving);
                }

                std::copy(buffer.begin(), buffer.end(), line);
            }
        }

        /// 沿列滑一遍窗口：与行那一路同一套算法，只是跨行取
        void blurColumns(QImage &image, const int radius)
        {
            const auto height = image.height();
            const auto span   = 2 * radius + 1;
            auto       buffer = std::vector<QRgb>(static_cast<std::size_t>(height));

            for (int x = 0; x < image.width(); ++x)
            {
                const auto pixelAt = [&image, x](const int y) { return reinterpret_cast<const QRgb *>(image.constScanLine(y))[x]; };

                qint64 sumRed   = 0;
                qint64 sumGreen = 0;
                qint64 sumBlue  = 0;
                qint64 sumAlpha = 0;
                for (int offset = -radius; offset <= radius; ++offset)
                {
                    const auto pixel = pixelAt(std::clamp(offset, 0, height - 1));
                    sumRed += qRed(pixel);
                    sumGreen += qGreen(pixel);
                    sumBlue += qBlue(pixel);
                    sumAlpha += qAlpha(pixel);
                }

                for (int y = 0; y < height; ++y)
                {
                    buffer[static_cast<std::size_t>(y)] = averageOf(sumRed, sumGreen, sumBlue, sumAlpha, span);

                    const auto leaving  = pixelAt(std::clamp(y - radius, 0, height - 1));
                    const auto entering = pixelAt(std::clamp(y + radius + 1, 0, height - 1));
                    sumRed += qRed(entering) - qRed(leaving);
                    sumGreen += qGreen(entering) - qGreen(leaving);
                    sumBlue += qBlue(entering) - qBlue(leaving);
                    sumAlpha += qAlpha(entering) - qAlpha(leaving);
                }

                for (int y = 0; y < height; ++y)
                {
                    reinterpret_cast<QRgb *>(image.scanLine(y))[x] = buffer[static_cast<std::size_t>(y)];
                }
            }
        }
    } // namespace

    QImage boxBlurred(const QImage &source, const int radius)
    {
        if (source.isNull() || radius <= 0)
        {
            return source;
        }

        auto image = source.convertToFormat(QImage::Format_ARGB32);
        for (int pass = 0; pass < kBlurPasses; ++pass)
        {
            blurRows(image, radius);
            blurColumns(image, radius);
        }
        return image;
    }
} // namespace GyanisScribe::Annotation
