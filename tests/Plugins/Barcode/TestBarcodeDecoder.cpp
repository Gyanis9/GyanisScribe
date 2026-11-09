// 二维码/条形码识别的直测：自造一张码再认回来、噪声与空图都该是「一个都没认出」而不是错误。
// 后端是可选的（见 src/Plugins/CMakeLists.txt）：宏按档给，没编后端时这一支只断言桩的行为——
// 两种形状各有用例看住，不留「没编过」的分支。
#include "Core/Privacy/RedactionGeometry.h"
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

namespace
{
    /**
     * @brief 自造一张带静区的码灰度图
     * @param payload 码里承载的文本
     * @param format 码制
     * @param scale 每个模块放大到的像素数
     * @return 自带像素副本的图；后端写出的缓冲随 ZXing::Image 析构，不能直接借给 QImage
     */
    QImage renderCode(const std::string &payload, const ZXing::BarcodeFormat format, int scale)
    {
        const auto         barcode  = ZXing::CreateBarcodeFromText(payload, ZXing::CreatorOptions{format});
        const ZXing::Image rendered = ZXing::WriteBarcodeToImage(barcode, ZXing::WriterOptions{}.scale(scale).addQuietZones(true));

        return QImage{rendered.data(), rendered.width(), rendered.height(), rendered.rowStride(), QImage::Format_Grayscale8}.copy();
    }

    /// 自造一张二维码：既有用例的入口，内部与条形码共用一份渲染
    QImage renderQrCode(const std::string &payload, int scale)
    {
        return renderCode(payload, ZXing::BarcodeFormat::QRCode, scale);
    }

    /**
     * @brief 码在画布上的实际墨迹范围
     * @param image 灰度画布，静区为白
     * @return 非白像素的外接框；整张全白时为空
     */
    QRect inkBounds(const QImage &image)
    {
        QRect bounds;
        for (int y = 0; y < image.height(); ++y)
        {
            for (int x = 0; x < image.width(); ++x)
            {
                if (image.pixelColor(x, y).value() < 250)
                {
                    bounds = bounds.isNull() ? QRect{x, y, 1, 1} : bounds.united(QRect{x, y, 1, 1});
                }
            }
        }
        return bounds;
    }
} // namespace

/**
 * @brief 钉住认得出自造的二维码：文本一字不差、码制报的是 QRCode
 * @details 用同一份后端自造样例（不是钉死的图片文件）：样例与判据同源，升级后端时一起走
 */
TEST(BarcodeDecoder, ReadsBackASelfMadeQrCode)
{
    const std::string payload  = "https://example.com/gyanisscribe?q=1";
    const QImage      canvas   = renderQrCode(payload, 4);
    const auto        readings = readBarcodes(canvas);
    ASSERT_TRUE(readings.has_value());
    EXPECT_TRUE(std::ranges::any_of(*readings, [](const BarcodeReading &reading) { return reading.matrix; })) << "二维码那条没被标成二维";
    const auto preferred = preferredReading(*readings);
    ASSERT_TRUE(preferred.has_value()) << "自造的二维码没认出来";
    EXPECT_EQ(preferred->text, QString::fromStdString(payload)) << "挑出来的文本对不上";
    EXPECT_EQ(preferred->format, QStringLiteral("QR Code")) << "码制没报对";
}

/**
 * @brief 钉住几何框：认出的码要带上输入图物理像素的外接框，非空、落在图内、盖住码中心
 * @details 框由后端的四个定位点收出来；它比整幅图小，正好排掉「直接回整张图」这种不认定位点的实现
 */
TEST(BarcodeDecoder, ReadingCarriesGeometryBoxInsideTheImage)
{
    const QImage canvas   = renderQrCode("https://example.com/gyanisscribe?q=1", 4);
    const auto   readings = readBarcodes(canvas);
    ASSERT_TRUE(readings.has_value());
    const auto preferred = preferredReading(*readings);
    ASSERT_TRUE(preferred.has_value()) << "自造的二维码没认出来";

    const QRect &box = preferred->box;
    EXPECT_FALSE(box.isEmpty()) << "认出了码却没给出几何框";
    EXPECT_TRUE(canvas.rect().contains(box)) << "几何框越出了输入图";
    EXPECT_TRUE(box.contains(canvas.rect().center())) << "几何框没盖住码中心";
    EXPECT_LT(box.width(), canvas.width()) << "几何框占满整幅图，说明不是从定位点收出来的";
    EXPECT_LT(box.height(), canvas.height()) << "几何框占满整幅图，说明不是从定位点收出来的";
}

/**
 * @brief 钉住一维 Code128：认得出、码制与二维标记正确，几何框对条码本体有意义
 * @details zxing 给一维码的定位点可能只覆盖解码行附近；本用例要求框横着盖住条码、
 *          纵向落在条码行内。纵向高度由 Core 的打码几何负责：按 1 倍屏撑到至少
 *          32 逻辑像素高，换算成物理像素后必须把整条码上下都盖住。
 */
TEST(BarcodeDecoder, ReadsBackASelfMadeCode128WithMeaningfulGeometryBox)
{
    const std::string payload  = "GYANIS-2026-1109";
    const QImage      canvas   = renderCode(payload, ZXing::BarcodeFormat::Code128, 1);
    const auto        readings = readBarcodes(canvas);
    ASSERT_TRUE(readings.has_value());
    const auto preferred = preferredReading(*readings);
    ASSERT_TRUE(preferred.has_value()) << "自造的 Code128 没认出来";
    EXPECT_EQ(preferred->text, QString::fromStdString(payload)) << "挑出来的文本对不上";
    EXPECT_EQ(preferred->format, QStringLiteral("Code 128")) << "码制没报对";
    EXPECT_FALSE(preferred->matrix) << "Code128 被标成了二维码";

    const QRect ink = inkBounds(canvas);
    ASSERT_FALSE(ink.isEmpty()) << "自造的条码整张全白，判据的靶子不干净";
    const QRect &box = preferred->box;
    EXPECT_FALSE(box.isEmpty()) << "认出了条码却没给出几何框";
    EXPECT_TRUE(canvas.rect().contains(box)) << "几何框越出了输入图";
    EXPECT_GE(box.width(), ink.width() * 9 / 10) << "几何框没横着盖住条码本体";
    EXPECT_TRUE(ink.contains(box.center())) << "几何框的纵向中心落在条码行之外";

    // 按 1 倍屏算：32 逻辑像素就是 32 物理像素；2 倍屏上物理高度会跟着翻倍
    const QSize logicalSize = canvas.size();
    const QRect expanded = GyanisScribe::Core::Privacy::expandBarcodeScanline(box, canvas.size(), logicalSize);
    EXPECT_LE(expanded.top(), ink.top()) << "撑高后的打码框没盖住条码上沿";
    EXPECT_GE(expanded.bottom(), ink.bottom()) << "撑高后的打码框没盖住条码下沿";

    const auto mapped = GyanisScribe::Core::Privacy::logicalRedactionRect(expanded, canvas.size(), logicalSize);
    ASSERT_TRUE(mapped.has_value());
    EXPECT_GE(mapped->height(), 32) << "打码框落到画布后不足 32 逻辑像素高";
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
