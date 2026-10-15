// 二维码/条形码识别的直测：自造一张码再认回来、噪声与空图都该是「一个都没认出」而不是错误。
// 后端是可选的（见 src/Scan/CMakeLists.txt）：宏按档给，没编后端时这一支只断言桩的行为——
// 两种形状各有用例看住，不留「没编过」的分支。
#include "Plugins/Barcode/BarcodeDecoder.h"

#include <QColor>
#include <QImage>

#include <gtest/gtest.h>

#if defined(GYANISSCRIBE_HAS_ZXING)
#include <ZXing/BarcodeFormat.h>
#include <ZXing/CreateBarcode.h>
#include <ZXing/WriteBarcode.h>

#include <random>
#include <ranges>
#include <string>
#endif

using GyanisScribe::Plugins::barcodeBackendAvailable;
using GyanisScribe::Plugins::BarcodeReading;
using GyanisScribe::Plugins::preferredReading;
using GyanisScribe::Plugins::readBarcodes;

/**
 * @brief 钉住空图的那一格：后端在册时是「没认出」空列表，不在册时是明确的 DependencyMissing
 */
TEST(BarcodeDecoder, EmptyImageYieldsNoReadingsInsteadOfAnError)
{
    const auto readings = readBarcodes(QImage{});
#if defined(GYANISSCRIBE_HAS_ZXING)
    EXPECT_TRUE(barcodeBackendAvailable()) << "编进了 zxing-cpp 却报没有后端";
    ASSERT_TRUE(readings.has_value()) << "空图该是「一个都没认出」而不是错误";
    EXPECT_TRUE(readings->empty());
#else
    EXPECT_FALSE(barcodeBackendAvailable()) << "没编后端却报有后端";
    ASSERT_FALSE(readings.has_value()) << "没编后端时该交回明确的错误";
    EXPECT_EQ(readings.error().code, GyanisScribe::Base::ErrorCode::DependencyMissing) << "错误码该是「依赖不在场」这一档";
#endif
}

/**
 * @brief 钉住挑选口径：有二维码就挑二维码，没有才取第一条一维，空列表为空
 * @details 这条钉的是实测里碰到的事实：一维扫描在二维码的花纹里偶发假阳性
 *          （自造 QR 旁边报出一条 ITF「85」），挑的时候必须把二维码压在前面
 */
TEST(BarcodeDecoder, PreferredReadingFavorsMatrixCodes)
{
    const BarcodeReading oneDimensional{QStringLiteral("85"), QStringLiteral("ITF"), false};
    const BarcodeReading matrix{QStringLiteral("https://example.com"), QStringLiteral("QR Code"), true};

    EXPECT_FALSE(preferredReading({}).has_value()) << "空列表该交回空";
    EXPECT_EQ(preferredReading({oneDimensional})->text, QStringLiteral("85")) << "只有一维时该取一维";
    EXPECT_EQ(preferredReading({oneDimensional, matrix})->text, QStringLiteral("https://example.com")) << "列表里有二维码时该挑二维码";
    EXPECT_EQ(preferredReading({matrix, oneDimensional})->text, QStringLiteral("https://example.com"));
}

#if defined(GYANISSCRIBE_HAS_ZXING)

/**
 * @brief 钉住认得出自造的二维码：文本一字不差、码制报的是 QRCode
 * @details 用同一份后端自造样例（不是钉死的图片文件）：样例与判据同源，升级后端时一起走
 */
TEST(BarcodeDecoder, ReadsBackASelfMadeQrCode)
{
    const std::string  payload  = "https://example.com/gyanisscribe?q=1";
    const auto         barcode  = ZXing::CreateBarcodeFromText(payload, ZXing::CreatorOptions{ZXing::BarcodeFormat::QRCode});
    const ZXing::Image rendered = ZXing::WriteBarcodeToImage(barcode, ZXing::WriterOptions{}.scale(4).addQuietZones(true));

    const QImage canvas{rendered.data(), rendered.width(), rendered.height(), rendered.rowStride(), QImage::Format_Grayscale8};
    const auto   readings = readBarcodes(canvas);
    ASSERT_TRUE(readings.has_value());
    EXPECT_TRUE(std::ranges::any_of(*readings, [](const BarcodeReading &reading) { return reading.matrix; })) << "二维码那条没被标成二维";
    const auto preferred = preferredReading(*readings);
    ASSERT_TRUE(preferred.has_value()) << "自造的二维码没认出来";
    EXPECT_EQ(preferred->text, QString::fromStdString(payload)) << "挑出来的文本对不上";
    EXPECT_EQ(preferred->format, QStringLiteral("QR Code")) << "码制没报对";
}

/**
 * @brief 钉住噪声图那一格：认不出东西，但不当成错误
 */
TEST(BarcodeDecoder, NoiseYieldsNoReadings)
{
    QImage                          noise{120, 120, QImage::Format_Grayscale8};
    std::mt19937                    generator{7};
    std::uniform_int_distribution<> distribution{0, 255};
    for (int y = 0; y < noise.height(); ++y)
    {
        for (int x = 0; x < noise.width(); ++x)
        {
            noise.setPixelColor(x, y, QColor{distribution(generator), 0, 0});
        }
    }

    const auto readings = readBarcodes(noise);
    ASSERT_TRUE(readings.has_value()) << "认不出码不该是错误";
    EXPECT_TRUE(readings->empty()) << "噪声图认出了码：判据的靶子不干净";
}

#endif
