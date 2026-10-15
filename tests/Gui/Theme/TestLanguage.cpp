#include "Gui/Theme/Language.h"

#include <gtest/gtest.h>

#include <QCoreApplication>
#include <QDir>
#include <QString>
#include <QTranslator>

#include <format>
#include <filesystem>
#include <fstream>
#include <memory>
#include <string>

using GyanisScribe::Gui::AppLanguage;
using GyanisScribe::Gui::appLanguageText;
using GyanisScribe::Gui::applyLanguage;
using GyanisScribe::Gui::defaultTranslationDirectories;
using GyanisScribe::Gui::findTranslationFiles;
using GyanisScribe::Gui::parseAppLanguage;
using GyanisScribe::Gui::translationFileName;

namespace
{
    /**
     * @brief 语言档位的夹具：只需要 QCoreApplication，不需要界面
     * @details applyLanguage 要把翻译器挂到 qApp 上、defaultTranslationDirectories 要读应用目录，
     *          两者都没有实例就是空操作加一次静默失败。
     */
    class LanguageFixture : public ::testing::Test
    {
    protected:
        static void SetUpTestSuite()
        {
            // argv 缓冲必须是静态期：QCoreApplication 只留住这个指针，applicationDirPath() 之后还要
            // 从 argv[0] 反推路径。放在 SetUpTestSuite 的栈上，那次调用就在已释放的内存上 strlen
            // （实测访问违例：Access violation reading location 0xffffffffffffffff）
            application = std::make_unique<QCoreApplication>(kArgumentCount, kArguments);
        }

        static void TearDownTestSuite()
        {
            application.reset();
        }

        /// 把进程里挂着的翻译器全部摘掉：留给后面的用例会改变它们的取值
        static void removeEveryTranslator()
        {
            if (application == nullptr)
            {
                return;
            }
            for (auto *translator: application->findChildren<QTranslator *>())
            {
                QCoreApplication::removeTranslator(translator);
                delete translator;
            }
        }

        inline static std::unique_ptr<QCoreApplication> application;

    private:
        inline static int   kArgumentCount  = 1;                        ///< 只有一个程序名参数
        inline static char  kArgumentZero[] = "TestLanguage";           ///< argv[0] 本体，静态期
        inline static char *kArguments[]    = {kArgumentZero, nullptr}; ///< 交给 QCoreApplication 的那份指针
    };

    /**
     * @brief 给本用例单独建一个临时目录
     * @details 语言包按文件名找，多个用例共用一个目录时，一个用例放进去的坏包会被另一个读到；
     *          目录名带用例名，ctest 并行时（每用例一个进程）也不会互相踩。
     */
    std::filesystem::path makeCaseDirectory()
    {
        const auto     *info      = ::testing::UnitTest::GetInstance()->current_test_info();
        const auto      directory = std::filesystem::temp_directory_path() / std::format("GyanisScribe-Language-{}-{}", info->test_suite_name(), info->name());
        std::error_code ignored;
        std::filesystem::remove_all(directory, ignored);
        std::filesystem::create_directories(directory);
        return directory;
    }

    /// 往目录里放一个指定名字的文件，内容由调用方给（非空内容在这里都表示「不是合法语言包」）
    void writeFile(const std::filesystem::path &directory, const std::string &name, const std::string &content)
    {
        std::ofstream stream{directory / name, std::ios::binary | std::ios::trunc};
        stream << content;
    }

    QString toQt(const std::filesystem::path &path)
    {
        // 走 wstring：Windows 上 narrow 转换按本地码页解释，含中文的临时目录名会被读成另一条路径
        return QString::fromStdWString(path.wstring());
    }
} // namespace

/**
 * @brief 钉住配置取值词表与档位的互指
 * @details 两条断言各写自己的字面量：拿 appLanguageText 的输出去反推 parseAppLanguage，
 *          整张词表换成别的词也照样绿。
 */
TEST_F(LanguageFixture, TokensMapBackToKinds)
{
    EXPECT_EQ(parseAppLanguage("zh").value(), AppLanguage::Chinese);
    EXPECT_EQ(parseAppLanguage("en").value(), AppLanguage::English);

    EXPECT_EQ(appLanguageText(AppLanguage::Chinese), "zh");
    EXPECT_EQ(appLanguageText(AppLanguage::English), "en");
}

/**
 * @brief 钉住大小写与首尾空格的容忍，以及坏值的拒绝面
 */
TEST_F(LanguageFixture, BlanksAndCaseToleratedButUnknownRejected)
{
    EXPECT_EQ(parseAppLanguage(" ZH ").value(), AppLanguage::Chinese);
    EXPECT_EQ(parseAppLanguage("En").value(), AppLanguage::English);

    for (const auto *bad: {"", "chinese", "EN-US", "jp", "1"})
    {
        const auto parsed = parseAppLanguage(bad);
        ASSERT_FALSE(parsed.has_value()) << bad;
        // 原因里要同时给出两个可用取值，只报「解析失败」时用户不知道该填哪一个
        EXPECT_NE(parsed.error().message.find("zh"), std::string::npos) << bad;
        EXPECT_NE(parsed.error().message.find("en"), std::string::npos) << bad;
    }
}

/**
 * @brief 钉住「中文档不需要语言包」与英文包的文件名
 * @details 这个文件名同时是 src/CMakeLists.txt 里 lrelease 的产物名（取 .ts 的本名）：
 *          两边对不上时应用永远找不到包，所以钉成字面量而不是从别处推导。
 */
TEST_F(LanguageFixture, ChineseNeedsNoPackAndEnglishHasFixedName)
{
    EXPECT_TRUE(translationFileName(AppLanguage::Chinese).isEmpty());
    EXPECT_EQ(translationFileName(AppLanguage::English), QStringLiteral("GyanisScribe_en.qm"));
}

/**
 * @brief 钉住搜索顺序、只收真文件、以及中文档列不出候选
 */
TEST_F(LanguageFixture, FirstDirectoryWithThePackWins)
{
    const auto      first  = makeCaseDirectory();
    const auto      second = std::filesystem::path{first.string() + "-2"};
    std::error_code ignored;
    std::filesystem::remove_all(second, ignored);
    std::filesystem::create_directories(second);

    // 中文档永远列不出候选：它不该去读任何包
    EXPECT_TRUE(findTranslationFiles(AppLanguage::Chinese, {toQt(first), toQt(second)}).isEmpty());

    // 两处都没有：空列表，让调用方去报「找不到」
    EXPECT_TRUE(findTranslationFiles(AppLanguage::English, {toQt(first), toQt(second)}).isEmpty());

    writeFile(second, "GyanisScribe_en.qm", "x");
    const auto onlySecond = findTranslationFiles(AppLanguage::English, {toQt(first), toQt(second)});
    ASSERT_EQ(onlySecond.size(), 1);
    EXPECT_EQ(onlySecond.first(), QDir{toQt(second)}.filePath(QStringLiteral("GyanisScribe_en.qm")));

    // 两处都有时按搜索顺序给全，第一个就是排前目录里的那份
    writeFile(first, "GyanisScribe_en.qm", "x");
    const auto both = findTranslationFiles(AppLanguage::English, {toQt(first), toQt(second)});
    ASSERT_EQ(both.size(), 2);
    EXPECT_EQ(both.first(), QDir{toQt(first)}.filePath(QStringLiteral("GyanisScribe_en.qm")));

    // 同名但其实是个目录：不算语言包在场（QFile::exists 对目录也为真，这一条就是防它的）
    const auto third = std::filesystem::path{first.string() + "-3"};
    std::filesystem::create_directories(third / "GyanisScribe_en.qm");
    EXPECT_TRUE(findTranslationFiles(AppLanguage::English, {toQt(third)}).isEmpty());
}

/**
 * @brief 钉住中文档「不装翻译器也算成功」
 * @details 中文就是源码语言，把「没有 zh 包」报成失败会让每次启动都带一条假故障。
 */
TEST_F(LanguageFixture, ChineseSucceedsWithoutAnyPack)
{
    const auto directory = makeCaseDirectory();
    EXPECT_TRUE(applyLanguage(AppLanguage::Chinese, {toQt(directory)}).has_value());
    removeEveryTranslator();
}

/**
 * @brief 钉住英文档的两条失败出口，并且不能把半成品留在进程里
 */
TEST_F(LanguageFixture, EnglishReportsMissingAndUnreadablePack)
{
    const auto directory = makeCaseDirectory();

    const auto missing = applyLanguage(AppLanguage::English, {toQt(directory)});
    ASSERT_FALSE(missing.has_value());
    EXPECT_EQ(missing.error().code, GyanisScribe::Base::ErrorCode::DependencyMissing);
    // 找过的目录要出现在文案里，否则用户只能猜应用去哪儿找的包
    EXPECT_NE(missing.error().message.find(toQt(directory).toStdString()), std::string::npos);

    // 有文件但内容不是语言包：装载要失败，那份半成品也不能留下
    writeFile(directory, "GyanisScribe_en.qm", "这不是语言包");
    const auto unreadable = applyLanguage(AppLanguage::English, {toQt(directory)});
    ASSERT_FALSE(unreadable.has_value());
    EXPECT_NE(unreadable.error().message.find("读不开"), std::string::npos);
    EXPECT_TRUE(application->findChildren<QTranslator *>().isEmpty()) << "读不开的那份还挂在应用对象上";

    removeEveryTranslator();
}

/**
 * @brief 钉住构建产出的真实语言包装载后取值变英文
 * @details 只在 lrelease 产出过包的那一档成立（本机 MSVC 有，容器那份 Qt 6.4 没有）：
 *          没有包就 SKIP，而不是让它红——红会掩盖真缺陷，而绿必须有真包作证。
 */
TEST_F(LanguageFixture, BuiltEnglishPackTranslatesKnownString)
{
    const auto dirs  = defaultTranslationDirectories();
    const auto packs = findTranslationFiles(AppLanguage::English, dirs);
    if (packs.isEmpty())
    {
        removeEveryTranslator();
        GTEST_SKIP() << "这一档没有构建出英文语言包（没装 lrelease，或翻译目录还没落地）";
    }

    const auto applied = applyLanguage(AppLanguage::English, dirs);
    ASSERT_TRUE(applied.has_value()) << applied.error().message;

    // 三类各取一条：字面量调用点、constexpr 表里经 uiText(row.label.data()) 运行期查的那一类、带占位符的那一条。
    // 装载生效时三者都应变英文；还是源码中文就说明这条没进目录或 context 写错，键对不上时表那一类最先坏。
    // 界面里删掉某一条样例时这里会红——那是有意的：样例必须换成界面上真的还在的那一句，否则「包装好了」这句话就没人证
    struct Sample
    {
        const char *source;   ///< 源码里的那串中文
        const char *expected; ///< 目录里钉好的英文译文
    };

    for (const Sample sample: {Sample{"截图与标注成品目录", "Folder for screenshots and annotations"}, Sample{"浅色", "Light"}, Sample{"最近使用 %1", "Recent %1"}})
    {
        const auto translated = QCoreApplication::translate("GyanisScribe", sample.source);
        EXPECT_NE(translated, QString::fromUtf8(sample.source)) << sample.source << "：包装载成功但这条没变，查 .ts 里有没有它、context 对不对";
        EXPECT_EQ(translated, QString::fromUtf8(sample.expected)) << sample.source;
    }

    // 占位符要在译文上继续能用：少一个 %1 就是界面写着 "Recent %1" 这种半成品
    EXPECT_EQ(QCoreApplication::translate("GyanisScribe", "最近使用 %1").arg(2), QStringLiteral("Recent 2"));

    removeEveryTranslator();
}
