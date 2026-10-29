// 「复制为文件」这一路的判据：落地的 PNG 要对得上、命名按内容认人、剪贴板里交出去的是那条路径。
// 剪贴板那一段要读回自己刚放进去的载荷，与别的借用剪贴板的套件同拿一把锁（tests/Core/CMakeLists.txt）。
#include "Core/Image/ClipboardFile.h"
#include "Base/Paths.h"

#include <QApplication>
#include <QClipboard>
#include <QDir>
#include <QImage>
#include <QMimeData>
#include <QUrl>

#include <gtest/gtest.h>

#include <chrono>
#include <filesystem>
#include <format>
#include <fstream>
#include <memory>

#ifdef Q_OS_WIN
#include <Windows.h>
#endif

using GyanisScribe::Base::Paths;
using GyanisScribe::Core::copyImageAsFile;

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

/**
 * @brief 钉住中转目录只留最新的一批 PNG，刚写的那份永不删
 * @details 预置 70 份旧图并把修改时间逐份推后，复制一张之后只该剩 64 份；
 *          最旧的那批被清掉，最新的一份旧图与刚写入的文件都还在。
 */
TEST_F(ClipboardFileFixture, ClipboardCacheKeepsOnlyTheNewestFiles)
{
    constexpr int kSeedCount = 70;

    ASSERT_TRUE(std::filesystem::create_directories(directory()));
    const auto baseTime = std::filesystem::file_time_type::clock::now() - std::chrono::hours{24};
    for (int index = 0; index < kSeedCount; ++index)
    {
        const auto path = directory() / std::format("seed-{:02}.png", index);
        std::ofstream{path, std::ios::binary} << "old";
        std::error_code touched;
        std::filesystem::last_write_time(path, baseTime + std::chrono::seconds{index}, touched);
        ASSERT_FALSE(touched) << "改不动预置文件的修改时间，清理判据的前提不成立：" << touched.message();
    }

    const auto written = copyImageAsFile(makeImage(), directory());
    ASSERT_TRUE(written.has_value()) << written.error().message;
    EXPECT_TRUE(std::filesystem::exists(*written)) << "刚写入的文件被清理掉了";

    int pngCount = 0;
    for (const auto &entry: std::filesystem::directory_iterator{directory()})
    {
        if (entry.path().extension() == ".png")
        {
            ++pngCount;
        }
    }
    EXPECT_EQ(pngCount, 64) << "中转目录的文件数不是上限那一档：刚写入的那份与最新 63 份旧图应当都在";
    EXPECT_TRUE(std::filesystem::exists(directory() / "seed-69.png")) << "最新的一份旧图被删了，留下的是更旧的那批";
    EXPECT_FALSE(std::filesystem::exists(directory() / "seed-00.png")) << "最旧的一份没有被清理";
}

/**
 * @brief 钉住清理给刚写入的那份留一格：64 份时间戳在未来的旧图也挤不掉它
 * @details 刚写入的文件可能带着比谁都旧的时间戳（从别处拷来、保留源时间的文件就是），
 *          按时间排序它会落到队尾；清理若只按时间取前 64，剪贴板里正指着的这一份就被删了。
 *          预置 64 份时间戳在一天后的旧图，复制一张之后总数必须仍是 64：
 *          刚写入的那份在，旧的只删掉时间戳最靠前的一份。
 */
TEST_F(ClipboardFileFixture, ClipboardPruneReservesASlotForTheJustWrittenFile)
{
    constexpr int kSeedCount = 64;

    ASSERT_TRUE(std::filesystem::create_directories(directory()));
    const auto futureTime = std::filesystem::file_time_type::clock::now() + std::chrono::hours{24};
    for (int index = 0; index < kSeedCount; ++index)
    {
        const auto path = directory() / std::format("future-{:02}.png", index);
        std::ofstream{path, std::ios::binary} << "old";
        std::error_code touched;
        std::filesystem::last_write_time(path, futureTime + std::chrono::seconds{index}, touched);
        ASSERT_FALSE(touched) << "改不动预置文件的修改时间，留一格判据的前提不成立：" << touched.message();
    }

    const auto written = copyImageAsFile(makeImage(), directory());
    ASSERT_TRUE(written.has_value()) << written.error().message;
    EXPECT_TRUE(std::filesystem::exists(*written)) << "时间戳更旧的那份被清理掉了：刚写入的文件没有单独占一格";

    int pngCount    = 0;
    int futureCount = 0;
    for (const auto &entry: std::filesystem::directory_iterator{directory()})
    {
        if (entry.path().extension() != ".png")
        {
            continue;
        }
        ++pngCount;
        if (entry.path().filename() != written->filename())
        {
            ++futureCount;
        }
    }
    EXPECT_EQ(pngCount, 64) << "留一格之后总数应当仍是上限，实际" << pngCount << "份";
    EXPECT_EQ(futureCount, 63) << "预置的旧图应当恰好删掉一份，实际剩下" << futureCount << "份";
    EXPECT_FALSE(std::filesystem::exists(directory() / "future-00.png")) << "多出来的应当是最早的那一份旧图";
}

/**
 * @brief 钉住同一张图复制两次复用同一个文件，且清理不会把刚写的那份删掉
 * @details 文件名按内容寻址；第二次写盘后的清理必须把刚写的文件排除在删除之外，
 *          否则剪贴板里交出去的路径在下一次复制时就指向一个不存在的文件。
 */
TEST_F(ClipboardFileFixture, CopyingTheSameImageReusesTheFile)
{
    const auto first = copyImageAsFile(makeImage(), directory());
    ASSERT_TRUE(first.has_value()) << first.error().message;
    const auto second = copyImageAsFile(makeImage(), directory());
    ASSERT_TRUE(second.has_value()) << second.error().message;

    EXPECT_EQ(*second, *first) << "同一张图复制两次落在了两个文件上";
    EXPECT_TRUE(std::filesystem::exists(*first)) << "第二次复制后的清理把刚写的文件删了";

    int pngCount = 0;
    for (const auto &entry: std::filesystem::directory_iterator{directory()})
    {
        if (entry.path().extension() == ".png")
        {
            ++pngCount;
        }
    }
    EXPECT_EQ(pngCount, 1) << "同一张图复制两次留下了两份文件";
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
