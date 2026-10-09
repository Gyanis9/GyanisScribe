#include "Ocr/OcrPreprocessor.h"

#include <algorithm>
#include <array>
#include <vector>

namespace GyanisScribe::Ocr
{
    namespace
    {
        // 亮度权重按 256 归一的整数版：0.299/0.587/0.114 → 77/150/29，三者相加正好 256
        constexpr int kRedWeight   = 77;
        constexpr int kGreenWeight = 150;
        constexpr int kBlueWeight  = 29;

        int clampEnlargeFactor(int factor) noexcept
        {
            return std::clamp(factor, 1, kMaximumEnlargeFactor);
        }

        int clampRadius(int radius) noexcept
        {
            return std::clamp(radius, 0, kMaximumDenoiseRadius);
        }

        // 边缘一圈按原值留着：把邻域裁半再取中值，边界像素会系统性偏亮或偏暗
        quint8 medianOfNeighborhood(const QImage &image, int x, int y, int radius)
        {
            std::vector<quint8> samples;
            samples.reserve(static_cast<std::size_t>((2 * radius + 1) * (2 * radius + 1)));
            for (int offset_y = -radius; offset_y <= radius; ++offset_y)
            {
                const auto *line = reinterpret_cast<const quint8 *>(image.constScanLine(y + offset_y));
                for (int offset_x = -radius; offset_x <= radius; ++offset_x)
                {
                    samples.push_back(line[x + offset_x]);
                }
            }
            const auto middle = samples.begin() + static_cast<std::ptrdiff_t>(samples.size() / 2);
            std::nth_element(samples.begin(), middle, samples.end());
            return *middle;
        }
    } // namespace

    QImage toGrayscale(const QImage &image)
    {
        if (image.isNull())
        {
            return {};
        }
        if (image.format() == QImage::Format_Grayscale8)
        {
            return image.copy();
        }

        const QImage converted = image.convertToFormat(QImage::Format_ARGB32);
        QImage       result(converted.size(), QImage::Format_Grayscale8);
        for (int y = 0; y < converted.height(); ++y)
        {
            const auto *line       = reinterpret_cast<const QRgb *>(converted.constScanLine(y));
            auto       *outputLine = result.scanLine(y);
            for (int x = 0; x < converted.width(); ++x)
            {
                const int luminance = (kRedWeight * qRed(line[x]) + kGreenWeight * qGreen(line[x]) + kBlueWeight * qBlue(line[x])) >> 8;
                outputLine[x]       = static_cast<quint8>(luminance);
            }
        }
        return result;
    }

    QImage binarize(const QImage &gray, quint8 threshold)
    {
        if (gray.isNull())
        {
            return {};
        }

        QImage result = gray.format() == QImage::Format_Grayscale8 ? gray.copy() : toGrayscale(gray);
        for (int y = 0; y < result.height(); ++y)
        {
            auto *line = result.scanLine(y);
            for (int x = 0; x < result.width(); ++x)
            {
                // 等于阈值判白：文字偏暗，把边界值算进黑侧会让细字变粗并粘连
                line[x] = line[x] < threshold ? 0 : 255;
            }
        }
        return result;
    }

    QImage enlarge(const QImage &image, int factor)
    {
        if (image.isNull())
        {
            return {};
        }

        const int clamped = clampEnlargeFactor(factor);
        if (clamped == 1)
        {
            return image.copy();
        }
        // 硬边放大：平滑会把锯齿糊成灰边，下一步二值化时反而丢笔画
        return image.scaled(image.width() * clamped, image.height() * clamped, Qt::IgnoreAspectRatio, Qt::FastTransformation);
    }

    QImage denoise(const QImage &gray, int radius)
    {
        if (gray.isNull())
        {
            return {};
        }

        const int clamped = clampRadius(radius);
        if (clamped == 0)
        {
            return gray.copy();
        }

        const QImage source = gray.format() == QImage::Format_Grayscale8 ? gray : toGrayscale(gray);
        QImage       result = source.copy();
        for (int y = clamped; y + clamped < source.height(); ++y)
        {
            auto *outputLine = result.scanLine(y);
            for (int x = clamped; x + clamped < source.width(); ++x)
            {
                outputLine[x] = medianOfNeighborhood(source, x, y, clamped);
            }
        }
        return result;
    }

    QImage prepareForRecognition(const QImage &image, int enlargeFactor, quint8 threshold)
    {
        if (image.isNull())
        {
            return {};
        }
        // 顺序固定：先去噪再放大——放大后噪点也变大，中值窗口就盖不住它了
        const auto gray    = toGrayscale(image);
        const auto cleaned = denoise(gray, 1);
        const auto scaled  = enlarge(cleaned, enlargeFactor);
        return binarize(scaled, threshold);
    }

} // namespace GyanisScribe::Ocr
