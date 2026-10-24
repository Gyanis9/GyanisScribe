#include "Gui/Resources/IconProvider.h"

#include <gtest/gtest.h>

#include <QByteArray>
#include <QColor>
#include <QCoreApplication>
#include <QFile>
#include <QImage>
#include <QSet>
#include <QSize>
#include <QString>

#include <array>
#include <cmath>
#include <memory>

namespace
{
    constexpr std::array kBrandSizes{16, 24, 32, 48, 64, 128, 256, 512};

    bool containsColor(const QImage &image, const QColor &expected, int tolerance = 2)
    {
        for (int y = 0; y < image.height(); ++y)
        {
            const auto *line = reinterpret_cast<const QRgb *>(image.constScanLine(y));
            for (int x = 0; x < image.width(); ++x)
            {
                if (qAlpha(line[x]) == 0)
                {
                    continue;
                }
                const auto difference = std::abs(qRed(line[x]) - expected.red()) + std::abs(qGreen(line[x]) - expected.green()) + std::abs(qBlue(line[x]) - expected.blue());
                if (difference <= tolerance)
                {
                    return true;
                }
            }
        }
        return false;
    }

    bool containsOpaqueColor(const QImage &image, const QColor &expected, int tolerance = 2)
    {
        for (int y = 0; y < image.height(); ++y)
        {
            const auto *line = reinterpret_cast<const QRgb *>(image.constScanLine(y));
            for (int x = 0; x < image.width(); ++x)
            {
                if (qAlpha(line[x]) != 255)
                {
                    continue;
                }
                const auto difference = std::abs(qRed(line[x]) - expected.red()) + std::abs(qGreen(line[x]) - expected.green()) + std::abs(qBlue(line[x]) - expected.blue());
                if (difference <= tolerance)
                {
                    return true;
                }
            }
        }
        return false;
    }

    QImage loadBrandPng(int size)
    {
        QImage image;
        image.load(QStringLiteral(":/brand/AppIcon%1.png").arg(size));
        return image;
    }

    /**
     * @brief 品牌资产测试的夹具：QImage 读 qrc 需要一个应用对象把图像插件装起来
     * @details 与 TestIconProvider 的 QApplication 分属两个测试套件，先后创建、先后销毁，不并存。
     */
    class BrandIconAssetTest : public ::testing::Test
    {
    protected:
        static void SetUpTestSuite()
        {
            static int   argumentCount  = 1;
            static char  argumentZero[] = "BrandIconAssetTest";
            static char *arguments[]    = {argumentZero, nullptr};
            application                 = std::make_unique<QCoreApplication>(argumentCount, arguments);
        }

        static void TearDownTestSuite()
        {
            application.reset();
        }

        static std::unique_ptr<QCoreApplication> application;
    };

    std::unique_ptr<QCoreApplication> BrandIconAssetTest::application;
} // namespace

/**
 * @brief 钉住品牌 PNG 的八档原生尺寸与三档颜色
 * @details 文件在 qrc 里、尺寸正确、三色都在、砖外角落透明；缺一条就说明生成脚本或 qrc 漏了。
 */
TEST_F(BrandIconAssetTest, PngsHaveEightNativeSizesAndTheThreeBrandColors)
{
    // 静态入口会把 qrc 注册进来；这里要的是注册这一下，名单本身不用
    static_cast<void>(GyanisScribe::Gui::IconProvider::availableIconNames());

    for (const auto size: kBrandSizes)
    {
        const auto image = loadBrandPng(size);
        ASSERT_FALSE(image.isNull()) << size << "px 品牌 PNG 读不出";
        EXPECT_EQ(image.size(), QSize(size, size)) << size << "px 品牌 PNG 尺寸不对";

        const auto argb = image.convertToFormat(QImage::Format_ARGB32);
        EXPECT_TRUE(containsColor(argb, QColor("#151515"))) << size << " 缺砖底";
        EXPECT_TRUE(containsColor(argb, QColor("#F5F5F3"))) << size << " 缺框线";
        EXPECT_TRUE(containsColor(argb, QColor("#5BE37A"))) << size << " 缺绿点";
        EXPECT_FALSE(containsOpaqueColor(argb, QColor(0, 0, 0))) << size << " 出现不透明纯黑";
        EXPECT_FALSE(containsOpaqueColor(argb, QColor(255, 255, 255))) << size << " 出现不透明纯白";
        EXPECT_EQ(qAlpha(argb.pixel(0, 0)), 0) << size << " 左上角不是透明，圆角外多了一块底";
    }
}

/**
 * @brief 钉住 .ico 的七个标准 Windows 条目
 * @details 512 不能作为 ICO 条目（目录宽高字段上限 256），只允许在 PNG 集里出现。
 */
TEST_F(BrandIconAssetTest, IcoCarriesTheSevenWindowsSizes)
{
    QFile file(QStringLiteral(GYANIS_BRAND_DIR "/AppIcon.ico"));
    ASSERT_TRUE(file.open(QIODevice::ReadOnly));
    const auto data = file.readAll();
    ASSERT_GE(data.size(), 6);

    const auto *bytes = reinterpret_cast<const uchar *>(data.constData());
    const auto  count = static_cast<int>(bytes[4] | (bytes[5] << 8));
    ASSERT_EQ(count, 7);

    QSet<int> sizes;
    for (int index = 0; index < count; ++index)
    {
        const auto *entry = bytes + 6 + index * 16;
        sizes.insert(entry[0] == 0 ? 256 : entry[0]);
    }

    EXPECT_EQ(sizes, (QSet<int>{16, 24, 32, 48, 64, 128, 256}));
}

/**
 * @brief 钉住 96 母版是固定色而不是 currentColor
 * @details 母版给关于页与文档用；一旦写成 currentColor，品牌图会被界面主题染成单色。
 */
TEST_F(BrandIconAssetTest, MasterSvgUsesFixedColorsOnly)
{
    // 同上：读 :/brand 之前先把 qrc 注册上
    static_cast<void>(GyanisScribe::Gui::IconProvider::availableIconNames());

    QFile file(QStringLiteral(":/brand/AppIcon.svg"));
    ASSERT_TRUE(file.open(QIODevice::ReadOnly));
    const auto source = QString::fromUtf8(file.readAll());

    EXPECT_NE(source.indexOf(QStringLiteral("#151515")), -1);
    EXPECT_NE(source.indexOf(QStringLiteral("#F5F5F3")), -1);
    EXPECT_NE(source.indexOf(QStringLiteral("#5BE37A")), -1);
    EXPECT_EQ(source.indexOf(QStringLiteral("currentColor")), -1);
}
