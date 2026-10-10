#include "Core/Paths.h"

#include <QCoreApplication>

#include <gtest/gtest.h>

#include <filesystem>
#include <format>
#include <fstream>
#include <memory>
#include <string>

using GyanisScribe::Core::ErrorCode;
using GyanisScribe::Core::Paths;

namespace
{

    std::unique_ptr<QCoreApplication> application;

    /// QStandardPaths 的取值由「组织名 + 应用名」决定，所以用例必须先立起 QCoreApplication 再取路径
    class PathsFixture : public ::testing::Test
    {
    protected:
        static void SetUpTestSuite()
        {
            // argv 与 argc 的缓冲要有静态存储期：Qt 只存指针不拷贝，
            // 而 QCoreApplication::applicationDirPath() 这类调用是在装置函数返回之后才读它们。
            static int   argumentCount  = 1;
            static char  argumentZero[] = "TestPaths";
            static char *arguments[]    = {argumentZero, nullptr};
            application                 = std::make_unique<QCoreApplication>(argumentCount, arguments);
            QCoreApplication::setOrganizationName(QStringLiteral("Gyanis"));
            QCoreApplication::setApplicationName(QStringLiteral("GyanisScribe"));
        }

        static void TearDownTestSuite()
        {
            application.reset();
        }
    };

    /// 每个用例用自己的目录名，并行跑（ctest 逐用例起进程）时不会共用同一棵临时树
    std::filesystem::path probeDirectory(std::string_view name)
    {
        return std::filesystem::temp_directory_path() / std::format("GyanisScribe-Paths-{}", name);
    }

} // namespace

/**
 * @brief 钉住「日志目录是数据目录下的 logs」这一层派生关系
 * @details 两处读数必须同解：一旦 logDirectory 自己另起一个根，设置页里改的那条路径就只管不到日志。
 */
TEST_F(PathsFixture, LogDirectoryHangsUnderApplicationData)
{
    const auto data = Paths::applicationDataDirectory();
    const auto logs = Paths::logDirectory();

    EXPECT_EQ(logs.parent_path(), data) << "日志目录不在数据目录之下，实际是 " << logs.string();
    EXPECT_EQ(logs.filename(), "logs") << "末段目录名变了，交付文档里那句 logs 就对不上";
}

/**
 * @brief 钉住截图与录屏默认目录都带应用同名的一层，且分别落在图片与视频位置
 */
TEST_F(PathsFixture, UserFacingDirectoriesCarryApplicationFolder)
{
    const auto screenshots = Paths::defaultScreenshotDirectory();
    const auto recordings  = Paths::defaultRecordingDirectory();

    EXPECT_EQ(screenshots.filename(), "GyanisScribe") << "截图目录没套上应用那一层，实际是 " << screenshots.string();
    EXPECT_EQ(recordings.filename(), "GyanisScribe") << "录屏目录没套上应用那一层，实际是 " << recordings.string();
    EXPECT_NE(screenshots.parent_path(), recordings.parent_path()) << "图片与视频位置相同：两个默认目录塌成一处，用户整理的预期就落空了";
}

/**
 * @brief 钉住 ensureDirectory 的幂等：目录已存在时不算失败
 */
TEST_F(PathsFixture, EnsureDirectoryIsIdempotent)
{
    const auto      directory = probeDirectory("Idempotent") / "nested/deeper";
    std::error_code cleanupError;
    std::filesystem::remove_all(probeDirectory("Idempotent"), cleanupError);

    ASSERT_TRUE(Paths::ensureDirectory(directory).has_value()) << "第一次创建就该成功";
    ASSERT_TRUE(std::filesystem::is_directory(directory));

    // 第二次走的是 create_directories 返回 false（什么都没建）那条路，判据必须仍然认它成功
    const auto again = Paths::ensureDirectory(directory);
    EXPECT_TRUE(again.has_value()) << "目录已存在被当成失败，实际原因：" << (again.has_value() ? std::string{} : again.error().message);
}

/**
 * @brief 钉住拒绝面：路径中段被一个普通文件占住时必须返回原因
 */
TEST_F(PathsFixture, EnsureDirectoryRejectsPathBlockedByFile)
{
    const auto      root = probeDirectory("BlockedByFile");
    std::error_code cleanupError;
    std::filesystem::remove_all(root, cleanupError);
    ASSERT_TRUE(Paths::ensureDirectory(root).has_value());

    const auto blocker = root / "blocker";
    {
        std::ofstream file(blocker, std::ios::binary);
        file << "占位";
    }

    const auto result = Paths::ensureDirectory(blocker / "sub");
    ASSERT_FALSE(result.has_value()) << "把文件当成目录用了却返回成功，下游会往一个不存在的路径写产物";
    EXPECT_EQ(result.error().code, ErrorCode::FileWriteFailed);
    EXPECT_NE(result.error().message.find("目录创建失败"), std::string::npos) << "文案要写给用户看的中文原因，实际是：" << result.error().message;
    EXPECT_NE(result.error().message.find(blocker.string()), std::string::npos) << "文案里没带出问题的路径，用户无法自查";
}
