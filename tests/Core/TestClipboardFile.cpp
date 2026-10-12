// 「复制为文件」这一路的判据：落地的 PNG 要对得上、命名按内容认人、剪贴板里交出去的是那条路径。
// 剪贴板那一段要读回自己刚放进去的载荷，与别的借用剪贴板的套件同拿一把锁（tests/Core/CMakeLists.txt）。
#include "Core/ClipboardFile.h"
#include "Core/Paths.h"

#include <QApplication>
#include <QClipboard>
#include <QDir>
#include <QImage>
#include <QMimeData>
#include <QUrl>

#include <gtest/gtest.h>

#include <filesystem>
#include <format>
#include <fstream>
#include <memory>

#ifdef Q_OS_WIN
#include <Windows.h>
#endif

using GyanisScribe::Core::copyImageAsFile;
using GyanisScribe::Core::Paths;

namespace
{
    std::unique_ptr<QApplication> application;

    class ClipboardFileFixture : public ::testing::Test
    {
    protected:
        static void SetUpTestSuite()
        {
            static int   argumentCount  = 1;
            static char  argumentZero[] = "TestClipboardFile";
            static char *arguments[]    = {argumentZero, nullptr};
            application                 = std::make_unique<QApplication>(argumentCount, arguments);
        }

        static void TearDownTestSuite()
        {
            application.reset();
        }

        void SetUp() override
        {
            const auto caseName = ::testing::UnitTest::GetInstance()->current_test_info()->name();
            m_directory         = std::filesystem::temp_directory_path() / std::format("GyanisScribe-ClipboardFile-{}", caseName);
            std::error_code ignored;
            std::filesystem::remove_all(m_directory, ignored);
        }

        void TearDown() override
        {
            std::error_code ignored;
            std::filesystem::remove_all(m_directory, ignored);
        }

        [[nodiscard]] std::filesystem::path directory() const
        {
            return m_directory;
        }

    private:
        std::filesystem::path m_directory;
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
 * @brief 钉住落地的 PNG：文件在、像素对得上、名字是内容哈希
 * @details 同一张图再复制一次要复用同一个文件（内容寻址），换一张图则落在另一个文件上——
 *          这两条合起来保证「复制过的那一份」不会被后来的另一张图顶掉。
 */
TEST_F(ClipboardFileFixture, WritesTheImageAsAContentAddressedPng)
{
    const auto written = copyImageAsFile(makeImage(), directory());
    ASSERT_TRUE(written.has_value()) << written.error().message;
    EXPECT_EQ(written->extension(), ".png") << "落地的不该是别的格式";
    EXPECT_EQ(written->stem().string().size(), 16u) << "文件名主干该是 16 位十六进制的内容哈希";

    ASSERT_TRUE(std::filesystem::exists(*written)) << "文件没落地";
    const QImage readBack{Paths::toDisplayText(*written)};
    ASSERT_FALSE(readBack.isNull()) << "落地的文件读不成图";
    EXPECT_EQ(readBack.size(), QSize(6, 4));
    EXPECT_EQ(readBack.pixelColor(0, 0), QColor(255, 0, 0)) << "标志像素读丢了";

    const auto again = copyImageAsFile(makeImage(), directory());
    ASSERT_TRUE(again.has_value());
    EXPECT_EQ(*again, *written) << "同一张图复制两次落在了两个文件上";

    QImage other = makeImage();
    other.setPixelColor(5, 3, QColor{0, 0, 255});
    const auto second = copyImageAsFile(other, directory());
    ASSERT_TRUE(second.has_value());
    EXPECT_NE(*second, *written) << "换一张图复用了同一个文件：先前那一份会被顶掉";
}

/**
 * @brief 钉住剪贴板里交出去的是「文件」而不是像素：本地文件 URL 指向刚落地的那一份
 * @details 读回的是本进程自己放进去的载荷（Qt 持有），不是从别的应用那儿要回来的——
 *          别人看到什么由 Windows 那一侧换算（CF_HDROP）保证，下面那条 Windows 用例单独钉。
 */
TEST_F(ClipboardFileFixture, PutsTheLocalFileUrlOnTheClipboard)
{
    const auto written = copyImageAsFile(makeImage(), directory());
    ASSERT_TRUE(written.has_value()) << written.error().message;

    const QMimeData *mime = QApplication::clipboard()->mimeData();
    ASSERT_NE(mime, nullptr) << "剪贴板里什么都没有";
    ASSERT_TRUE(mime->hasUrls()) << "交出去的不是文件 URL，粘进资源管理器只会得到像素";
    ASSERT_EQ(mime->urls().size(), 1);
    EXPECT_EQ(QUrl::fromLocalFile(Paths::toDisplayText(*written)), mime->urls().front()) << "剪贴板里的路径不是落地的这一份";
}

#ifdef Q_OS_WIN
/**
 * @brief Windows 上钉住 CF_HDROP：资源管理器与聊天窗口读到的就是这一路
 */
TEST_F(ClipboardFileFixture, WindowsClipboardCarriesTheHdropFormat)
{
    const auto written = copyImageAsFile(makeImage(), directory());
    ASSERT_TRUE(written.has_value()) << written.error().message;
    EXPECT_NE(IsClipboardFormatAvailable(CF_HDROP), 0) << "剪贴板里没有 CF_HDROP：资源管理器看不到这份文件";
}
#endif

/**
 * @brief 钉住拒绝：空图不写、目录建不出来时如实报错
 */
TEST_F(ClipboardFileFixture, RefusesEmptyImagesAndUnwritableDirectories)
{
    EXPECT_FALSE(copyImageAsFile(QImage{}, directory()).has_value()) << "空图也写出了一份文件";

    // 拿一个「底下已经有同名文件」的路径当目录：ensureDirectory 建不出来，如实交回原因
    const auto blocked = directory() / "blocked";
    ASSERT_TRUE(std::filesystem::create_directories(directory()));
    {
        std::ofstream blocker(blocked, std::ios::binary | std::ios::trunc);
        blocker << "not a directory";
    }
    const auto refused = copyImageAsFile(makeImage(), blocked / "child");
    EXPECT_FALSE(refused.has_value()) << "目录建不出来还是写了下去";
}
