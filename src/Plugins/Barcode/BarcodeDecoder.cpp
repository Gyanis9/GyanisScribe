#include "Plugins/Barcode/BarcodeDecoder.h"

#include "Base/Text.h"

#include <algorithm>

#if defined(GYANISSCRIBE_HAS_ZXING)
#include <ZXing/BarcodeFormat.h>
#include <ZXing/ReadBarcode.h>
#endif

namespace GyanisScribe::Plugins
{

    bool barcodeBackendAvailable() noexcept
    {
#if defined(GYANISSCRIBE_HAS_ZXING)
        return true;
#else
        return false;
#endif
    }

    Base::Expected<std::vector<BarcodeReading>> readBarcodes(const QImage &image)
    {
#if defined(GYANISSCRIBE_HAS_ZXING)
        if (image.isNull())
        {
            // 空图里没有码：这是「一个都没认出」而不是「出了什么岔子」
            return std::vector<BarcodeReading>{};
        }

        // 灰度一份交给后端：它的 ImageView 认 Lum 这类连续像素，QImage 的位深与跨距由 bytesPerLine 交代
        const QImage           gray = image.convertToFormat(QImage::Format_Grayscale8);
        const ZXing::ImageView view{gray.constBits(), gray.width(), gray.height(), ZXing::ImageFormat::Lum, static_cast<int>(gray.bytesPerLine())};
        const auto             results = ZXing::ReadBarcodes(view, ZXing::ReaderOptions{}.setFormats(ZXing::BarcodeFormat::All));

        std::vector<BarcodeReading> readings;
        readings.reserve(results.size());
        for (const auto &result: results)
        {
            if (result.isValid())
            {
                // 四个定位点是码角在输入图上的像素坐标；外接框取最外沿，再夹回图像范围。
                // 一维码的定位点往往只覆盖解码行附近的一条窄带，夹取不会给它补出条码高度
                const ZXing::Position &position = result.position();
                int                    left     = position.topLeft().x;
                int                    top      = position.topLeft().y;
                int                    right    = left;
                int                    bottom   = top;
                for (const ZXing::PointI &corner: position)
                {
                    left   = std::min(left, corner.x);
                    top    = std::min(top, corner.y);
                    right  = std::max(right, corner.x);
                    bottom = std::max(bottom, corner.y);
                }

                readings.push_back(BarcodeReading{QString::fromStdString(result.text()), QString::fromStdString(ZXing::ToString(result.format())),
                                                  result.format() & ZXing::BarcodeFormat::AllMatrix, QRect{QPoint{left, top}, QPoint{right, bottom}}.intersected(image.rect())});
            }
        }
        return readings;
#else
        Q_UNUSED(image);
        return std::unexpected(Base::Error{Base::ErrorCode::DependencyMissing, Base::uiText("本档没带二维码/条形码识别后端。请用随包发布的安装版，或让构建依赖里带上 zxing-cpp")});
#endif
    }

    std::optional<BarcodeReading> preferredReading(const std::vector<BarcodeReading> &readings)
    {
        for (const auto &reading: readings)
        {
            if (reading.matrix)
            {
                return reading;
            }
        }
        return readings.empty() ? std::nullopt : std::optional<BarcodeReading>{readings.front()};
    }

} // namespace GyanisScribe::Plugins
