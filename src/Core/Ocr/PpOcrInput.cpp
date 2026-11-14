#include "Core/Ocr/PpOcrInput.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>

namespace GyanisScribe::Core
{
    namespace
    {
        // 检测的归一化系数按 B、G、R 的平面顺序排列。PaddleOCR 的检测模型用 ImageNet 统计量训练，
        // 训练时喂进网络的是 cv2 读出的 BGR 图，所以 0.485 这一项对应 B 通道而不是 R 通道
        constexpr std::array<float, 3> kDetMean = {0.485F, 0.456F, 0.406F};
        constexpr std::array<float, 3> kDetStd  = {0.229F, 0.224F, 0.225F};

        // 识别模型把像素线性映射到 [-1, 1]，等价于均值与标准差都取 0.5
        constexpr std::array<float, 3> kRecMean = {0.5F, 0.5F, 0.5F};
        constexpr std::array<float, 3> kRecStd  = {0.5F, 0.5F, 0.5F};

        constexpr int kRecMinWidth = 16;

        int roundUp(const int value, const int multiple) noexcept
        {
            return (value + multiple - 1) / multiple * multiple;
        }

        /**
         * @brief 把 RGB32 图拆成三个通道平面并归一化
         * @param rgb 已转成 Format_RGB32 的图
         * @param mean 三个通道的均值，按 B、G、R 顺序
         * @param stdDev 三个通道的标准差，按 B、G、R 顺序
         * @param scaleX 原图到这里的横向比例
         * @param scaleY 原图到这里的纵向比例
         * @return 通道平面排布的张量
         */
        PpOcrTensor toPlanarTensor(const QImage &rgb, const std::array<float, 3> &mean, const std::array<float, 3> &stdDev, const double scaleX, const double scaleY)
        {
            PpOcrTensor tensor;
            tensor.width  = rgb.width();
            tensor.height = rgb.height();
            tensor.scaleX = scaleX;
            tensor.scaleY = scaleY;

            const auto planeSize = static_cast<size_t>(rgb.width()) * static_cast<size_t>(rgb.height());
            tensor.data.resize(planeSize * 3U);

            for (int y = 0; y < rgb.height(); ++y)
            {
                const auto *line      = reinterpret_cast<const QRgb *>(rgb.constScanLine(y));
                const auto  rowOffset = static_cast<size_t>(y) * static_cast<size_t>(rgb.width());
                for (int x = 0; x < rgb.width(); ++x)
                {
                    const QRgb pixel = line[x];
                    const auto index = rowOffset + static_cast<size_t>(x);
                    tensor.data[index]               = (qBlue(pixel) / 255.0F - mean[0]) / stdDev[0];
                    tensor.data[planeSize + index]   = (qGreen(pixel) / 255.0F - mean[1]) / stdDev[1];
                    tensor.data[planeSize * 2 + index] = (qRed(pixel) / 255.0F - mean[2]) / stdDev[2];
                }
            }
            return tensor;
        }
    } // namespace

    PpOcrTensor buildDetInput(const QImage &image, const int maxSide)
    {
        if (image.isNull())
        {
            return {};
        }

        const int    longestSide   = std::max(image.width(), image.height());
        const double scale         = longestSide > maxSide ? static_cast<double>(maxSide) / static_cast<double>(longestSide) : 1.0;
        const int    resizedWidth  = std::max(1, static_cast<int>(std::lround(static_cast<double>(image.width()) * scale)));
        const int    resizedHeight = std::max(1, static_cast<int>(std::lround(static_cast<double>(image.height()) * scale)));

        const QImage resized = image.scaled(resizedWidth, resizedHeight, Qt::IgnoreAspectRatio, Qt::SmoothTransformation).convertToFormat(QImage::Format_RGB32);

        // 先补成黑色像素再统一归一化，与训练时的预处理顺序一致；只补右侧与下缘，左上角原点不动
        QImage canvas(roundUp(resizedWidth, kPpOcrDetMultiple), roundUp(resizedHeight, kPpOcrDetMultiple), QImage::Format_RGB32);
        canvas.fill(qRgb(0, 0, 0));
        for (int y = 0; y < resizedHeight; ++y)
        {
            std::memcpy(canvas.scanLine(y), resized.constScanLine(y), static_cast<size_t>(resizedWidth) * sizeof(QRgb));
        }

        return toPlanarTensor(canvas, kDetMean, kDetStd,
                              static_cast<double>(resizedWidth) / static_cast<double>(image.width()),
                              static_cast<double>(resizedHeight) / static_cast<double>(image.height()));
    }

    PpOcrTensor buildRecInput(const QImage &crop, const int height, const int maxWidth)
    {
        if (crop.isNull())
        {
            return {};
        }

        const double aspectWidth = static_cast<double>(height) * static_cast<double>(crop.width()) / static_cast<double>(crop.height());
        const int    upperBound  = std::max(maxWidth, kRecMinWidth);
        const int    resizedWidth = std::clamp(static_cast<int>(std::lround(aspectWidth)), kRecMinWidth, upperBound);

        const QImage resized = crop.scaled(resizedWidth, height, Qt::IgnoreAspectRatio, Qt::SmoothTransformation).convertToFormat(QImage::Format_RGB32);

        return toPlanarTensor(resized, kRecMean, kRecStd,
                              static_cast<double>(resizedWidth) / static_cast<double>(crop.width()),
                              static_cast<double>(height) / static_cast<double>(crop.height()));
    }

} // namespace GyanisScribe::Core
