#include "Base/Paths.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QFileInfo>

#include <gtest/gtest.h>

#include <filesystem>
#include <format>
#include <fstream>
#include <memory>
#include <string>

using GyanisScribe::Base::ErrorCode;
using GyanisScribe::Base::Paths;

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
 * @brief 钉住未保存截图那一份挂在可写数据目录下，而不是系统缓存位置
 * @details 这一份存在的理由是「误触 Esc 也还在」：挂在缓存位置等于把承诺交给别人的回收策略，
 *          清理工具扫一遍之后，历史里那些条目全指到空处。
 */
TEST_F(PathsFixture, UnsavedCapturesHangUnderApplicationDataNotCache)
{
    const auto data    = Paths::applicationDataDirectory();
    const auto unsaved = Paths::unsavedCaptureDirectory();

    EXPECT_EQ(unsaved.parent_path(), data) << "兜底那一份不在数据目录之下，实际是 " << unsaved.string();
    EXPECT_EQ(unsaved.filename(), "Unsaved");
    EXPECT_NE(unsaved.parent_path(), Paths::cacheDirectory()) << "挂在缓存位置下，清理工具会把「还在历史里」的那几张先删掉";
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

/**
 * @brief 钉住交付文件名：程序名原样保留，只有日期那几段按格式展开
 * @details 期望值写死成字面量，不用同一个 toString 现算——这条格式串里每个字母都可能是 Qt 的格式记号，
 *          同源算出来的期望会把缺陷一起抄进期望。
 */
TEST_F(PathsFixture, ImageFileNameKeepsLiteralBrandAndExpandsOnlyDateTokens)
{
    const QDateTime moment{QDate{2026, 10, 11}, QTime{5, 48, 54, 160}};

    EXPECT_EQ(Paths::imageFileName(moment, "png").toStdString(), "GyanisScribe_20261011_054854_160.png");
    EXPECT_EQ(Paths::imageFileName(moment, "txt").toStdString(), "GyanisScribe_20261011_054854_160.txt");
}

/**
 * @brief 钉住模板解析的放行面与拒因面
 * @details 拒的是会把文件带出目录、系统写不出去、以及打错字这三类；放行的是「进度 50%」这种
 *          不成对的百分号——它是正常名字的一部分，不是变量写法。
 */
TEST(PathsNameTemplate, ParsesOnlyTheThreeDocumentedTokens)
{
    const auto trimmed = Paths::parseNameTemplate(QStringLiteral("  截图_%date%_%time%_%ms%  "));
    ASSERT_TRUE(trimmed.has_value()) << trimmed.error().message;
    EXPECT_EQ(*trimmed, QStringLiteral("截图_%date%_%time%_%ms%")) << "首尾空白该去掉";

    EXPECT_FALSE(Paths::parseNameTemplate(QStringLiteral("   ")).has_value()) << "空模板不该放行";
    EXPECT_FALSE(Paths::parseNameTemplate(QStringLiteral("a/b")).has_value()) << "路径分隔符会把文件写到别的目录";
    EXPECT_FALSE(Paths::parseNameTemplate(QStringLiteral("a:b")).has_value()) << "冒号在 Windows 上写不出去";

    const auto typo = Paths::parseNameTemplate(QStringLiteral("shot_%dtae%"));
    ASSERT_FALSE(typo.has_value()) << "认不出的变量放过去，就是让打错的一个字悄悄写进文件名";
    EXPECT_NE(typo.error().message.find("dtae"), std::string::npos) << "要说清是哪一个变量不认得";

    EXPECT_TRUE(Paths::parseNameTemplate(QStringLiteral("进度 50%")).has_value()) << "不成对的百分号是正常名字的一部分";
    EXPECT_TRUE(Paths::parseNameTemplate(QStringLiteral("%date%-%time%")).has_value());
}

/**
 * @brief 钉住拼名与三条回落：模板留空、模板不合法、模板里带中文
 * @details 带分隔符的模板若被原样拼进名字，文件会落到别的目录；这一条钉的就是它落到默认档上。
 */
TEST_F(PathsFixture, FormatsProductNameByTemplateAndNeverLeavesTheDirectory)
{
    const QDateTime moment{QDate{2026, 10, 11}, QTime{5, 48, 54, 160}};

    EXPECT_EQ(Paths::formatProductName(moment, QStringLiteral("截图_%date%_%time%")), QStringLiteral("截图_20261011_054854"));
    EXPECT_EQ(Paths::imageFileName(moment, "png", QStringLiteral("shot-%date%")), QStringLiteral("shot-20261011.png"));
    EXPECT_EQ(Paths::imageFileName(moment, "png", QString{}), QStringLiteral("GyanisScribe_20261011_054854_160.png")) << "留空该落到默认档";

    EXPECT_EQ(Paths::imageFileName(moment, "png", QStringLiteral("../逃出去")), QStringLiteral("GyanisScribe_20261011_054854_160.png"))
            << "没先校验的调用方传进带分隔符的模板时，宁可写默认名字";
}

/**
 * @brief 钉住便携标记的判读：有标记才把配置落在程序目录旁，没有就交回空串走系统默认
 * @details 用标记文件而不是「默认就近」：装到 Program Files 这类不可写目录时，默认就近会直接坏掉；
 *          绿色版用户放一个空文件是明确的选择。只有配置跟着走，日志与历史不跟。
 */
TEST_F(PathsFixture, PortableConfigFollowsTheMarkerFile)
{
    const auto      directory = probeDirectory("Portable");
    std::error_code ignored;
    std::filesystem::remove_all(directory, ignored);
    std::filesystem::create_directories(directory);
    const auto directoryText = Paths::toDisplayText(directory);

    EXPECT_TRUE(Paths::portableConfigFileIn(directoryText).isEmpty()) << "没有标记时不该把配置搬到程序旁边";

    {
        std::ofstream{directory / std::string{Paths::kPortableMarkerFileName}} << "";
    }
    // 比语义而不是比字符串：两种写法只差分隔符与大小写风格，归一之后钉住「落在标记那个目录里」
    const auto portable = Paths::portableConfigFileIn(directoryText);
    EXPECT_EQ(QFileInfo{portable}.fileName(), QStringLiteral("GyanisScribe.ini"));
    const auto normalize = [](const QString &path) { return QDir::cleanPath(path).replace(QLatin1Char('\\'), QLatin1Char('/')).toLower(); };
    EXPECT_EQ(normalize(QFileInfo{portable}.absolutePath()), normalize(directoryText)) << "有标记时配置要落在它旁边";

    std::filesystem::remove_all(directory, ignored);
}

/**
 * @brief 钉住撞名时的退让：-2 加在扩展名之前，且按盘上真实存在的文件判
 */
TEST_F(PathsFixture, UniqueFilePathStepsAsideBeforeTheExtension)
{
    const auto      directory = probeDirectory("NameTemplate");
    std::error_code ignored;
    std::filesystem::remove_all(directory, ignored);
    std::filesystem::create_directories(directory);

    const auto first = Paths::uniqueFilePath(directory, QStringLiteral("shot-20261011.png"));
    EXPECT_EQ(first.filename().string(), "shot-20261011.png") << "没有撞名就该用原名";

    {
        std::ofstream{first} << "x";
    }
    const auto second = Paths::uniqueFilePath(directory, QStringLiteral("shot-20261011.png"));
    EXPECT_EQ(second.filename().string(), "shot-20261011-2.png") << "-2 该加在扩展名之前，加在后面双击打不开";

    {
        std::ofstream{second} << "x";
    }
    const auto third = Paths::uniqueFilePath(directory, QStringLiteral("shot-20261011.png"));
    EXPECT_EQ(third.filename().string(), "shot-20261011-3.png");

    std::filesystem::remove_all(directory, ignored);
}
