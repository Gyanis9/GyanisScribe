// 剪贴板里的「旧程序兼容」判据：Qt 的 setImage 在 Windows 上要落成 CF_DIB、CF_DIBV5 与 CF_BITMAP 三路，
// 只认 DIB 的旧程序（旧版 Office、画图一类）由此能直接吃到。这条承诺以前没有任何用例看着——
// 本批做「以 BMP 复制」时先写探针实测：普通 setImage 已经给出这三路，而自造的 MIME 载荷反而一路都没有，
// 于是那一档不另设实现（自造载荷是负收益），改由本文件把既有承诺钉住。
#include <QApplication>
#include <QClipboard>
#include <QImage>

#include <gtest/gtest.h>

#include <memory>

#ifdef Q_OS_WIN
#include <Windows.h>

#include <format>
#include <string>
#endif

namespace
{
    std::unique_ptr<QApplication> application;

    class ClipboardImageFixture : public ::testing::Test
    {
    protected:
        static void SetUpTestSuite()
        {
            static int   argumentCount  = 1;
            static char  argumentZero[] = "TestClipboardImage";
            static char *arguments[]    = {argumentZero, nullptr};
            application                 = std::make_unique<QApplication>(argumentCount, arguments);
        }

        static void TearDownTestSuite()
        {
            application.reset();
        }
    };

    QImage makeImage()
    {
        QImage image{6, 4, QImage::Format_ARGB32};
        image.fill(QColor{10, 200, 30});
        image.setPixelColor(0, 0, QColor{255, 0, 0});
        return image;
    }
} // namespace

/**
 * @brief 交进去的图能原样读回来（走 Qt 自己在路上的那一份）
 * @details 复制成品这条路每一次截图都在走；读回来一致是它最基本的承诺。
 */
TEST_F(ClipboardImageFixture, RoundTripsThroughTheClipboard)
{
    const auto image = makeImage();
    QApplication::clipboard()->setImage(image);
    QApplication::processEvents();

    const auto readBack = QApplication::clipboard()->image();
    ASSERT_FALSE(readBack.isNull()) << "交进去的图读不回来";
    EXPECT_EQ(readBack.size(), image.size());
    EXPECT_EQ(readBack.pixelColor(0, 0), QColor(255, 0, 0)) << "标志像素读丢了";
}

#ifdef Q_OS_WIN
/**
 * @brief Windows 上钉住旧程序兼容的落点：setImage 之后 CF_DIB、CF_DIBV5 与 CF_BITMAP 三路都要在
 * @details 「只认 DIB 的旧程序读得到」这句承诺的判据在这里：Qt 的 Windows 剪贴板把
 *          DIB 三路一并发布（本机实测），用系统 API 问剪贴板要格式，与旧程序看到的是同一份名单。
 *          失败时把此刻的格式单子打出来，Qt 少发了哪几路一眼可见。
 */
TEST_F(ClipboardImageFixture, WindowsClipboardCarriesTheDibFamily)
{
    QApplication::clipboard()->setImage(makeImage());
    QApplication::processEvents();

    const auto requireFormat = [](const UINT format, const char *name)
    {
        if (IsClipboardFormatAvailable(format) != 0)
        {
            return;
        }

        std::string formats;
        if (OpenClipboard(nullptr) != 0)
        {
            for (UINT current = EnumClipboardFormats(0); current != 0; current = EnumClipboardFormats(current))
            {
                wchar_t registered[256] = {};
                if (GetClipboardFormatNameW(current, registered, 256) > 0)
                {
                    formats += std::format(" {}('{}')", current, QString::fromWCharArray(registered).toStdString());
                } else
                {
                    formats += std::format(" {}", current);
                }
            }
            CloseClipboard();
        }
        ADD_FAILURE() << "剪贴板里没有 " << name << "：只认这一路的旧程序读不到。当前格式：" << formats;
    };

    requireFormat(CF_DIB, "CF_DIB");
    requireFormat(CF_DIBV5, "CF_DIBV5");
    requireFormat(CF_BITMAP, "CF_BITMAP");
}
#endif
