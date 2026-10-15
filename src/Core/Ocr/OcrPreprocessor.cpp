#include "Core/Ocr/OcrPreprocessor.h"

#include <algorithm>
#include <array>
#include <vector>

namespace GyanisScribe::Core
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

    QImage prepareForRecognition(const QImage &image, const int enlargeFactor)
    {
        if (image.isNull())
        {
            return {};
        }
        // 只到灰度与放大为止：二值化与中值滤波在系统后端实测把 4 词读成 0 词，理由见头文件的 @warning
        return enlarge(toGrayscale(image), enlargeFactor);
    }

} // namespace GyanisScribe::Core
