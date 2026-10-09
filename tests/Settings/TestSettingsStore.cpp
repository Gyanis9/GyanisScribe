#include "Core/Logger.h"
#include "Settings/SettingsStore.h"

#include <QCoreApplication>
#include <QSettings>

#include <gtest/gtest.h>

#include <filesystem>
#include <format>
#include <fstream>
#include <iterator>
#include <memory>
#include <string>
#include <vector>

using GyanisScribe::Core::ErrorCode;
using GyanisScribe::Settings::SettingsStore;

namespace
{
    std::unique_ptr<QCoreApplication> application;

    /// QSettings 的后端位置与 QVariant 的元类型都要在应用对象之后才可靠，用例自己把它立起来
    class SettingsFixture : public ::testing::Test
    {
    protected:
        static void SetUpTestSuite()
        {
            int   argumentCount  = 1;
            char  argumentZero[] = "TestSettingsStore";
            char *arguments[]    = {argumentZero, nullptr};
            application          = std::make_unique<QCoreApplication>(argumentCount, arguments);
            QCoreApplication::setOrganizationName(QStringLiteral("Gyanis"));
            QCoreApplication::setApplicationName(QStringLiteral("GyanisScribe-Tests"));
        }

        static void TearDownTestSuite()
        {
            application.reset();
        }

        /// 每条用例一套 ini 路径，并行跑（ctest 逐用例起进程）时不会共写一份配置
        static std::filesystem::path probePath(std::string_view caseName, std::string_view suffix = "store")
        {
            return std::filesystem::temp_directory_path() / std::format("GyanisScribe-Settings-{}-{}.ini", caseName, suffix);
        }

        static void removeProbeFiles(std::string_view caseName)
        {
            std::error_code ignored;
            for (const auto &suffix: {"store", "target", "export", "source"})
            {
                std::filesystem::remove(probePath(caseName, suffix), ignored);
            }
        }
    };

    std::string readFile(const std::filesystem::path &path)
    {
        std::ifstream input(path, std::ios::binary);
        return std::string{std::istreambuf_iterator<char>{input}, std::istreambuf_iterator<char>{}};
    }

    /// 直接写在配置文本里的原始取值：造的就是「后端里躺着坏值」这一格，而不是走存储接口写进去
    void writeRawValue(const std::filesystem::path &iniFile, const QString &key, const QString &value)
    {
        QSettings raw(QString::fromLocal8Bit(iniFile.string().c_str()), QSettings::IniFormat);
        raw.setValue(key, value);
        raw.sync();
    }
} // namespace

/**
 * @brief 钉住四类取值写下去能读回来，且真的落到文件里（换一个新实例读同一份文件）
 */
TEST_F(SettingsFixture, RoundTripsEveryTypedValue)
{
    const auto caseName = "RoundTrip";
    removeProbeFiles(caseName);
    const auto iniFile = probePath(caseName);

    {
        SettingsStore store(iniFile);
        ASSERT_TRUE(store.setValue("Paths/Screenshot", QVariant(QStringLiteral("D:/Shots"))).has_value());
        ASSERT_TRUE(store.setValue("Record/Fps", QVariant(30)).has_value());
        ASSERT_TRUE(store.setValue("Capture/CursorHighlight", QVariant(true)).has_value());
        ASSERT_TRUE(store.setValue("Ocr/Languages", QVariant(QStringList{"zh-CN", "en-US"})).has_value());
        store.sync();
    }

    // 新实例 + 原始文件文本双判据：只看内存会漏掉「根本没写盘」这一类失败
    ASSERT_TRUE(std::filesystem::exists(iniFile)) << "sync 之后配置文件不在，路径 " << iniFile.string();
    const auto rawText = readFile(iniFile);
    EXPECT_NE(rawText.find("Screenshot"), std::string::npos) << "文件里没有 Paths/Screenshot 这一项，说明写盘没生效";

    SettingsStore reopened(iniFile);
    EXPECT_EQ(reopened.stringValue("Paths/Screenshot", QString()), QStringLiteral("D:/Shots"));
    EXPECT_EQ(reopened.intValue("Record/Fps", -1), 30);
    EXPECT_TRUE(reopened.boolValue("Capture/CursorHighlight", false));
    EXPECT_EQ(reopened.stringListValue("Ocr/Languages", QStringList()), (QStringList{"zh-CN", "en-US"}));
}

/**
 * @brief 钉住默认值只在「键不存在」时生效，并且存在的键不会被当成缺失
 */
TEST_F(SettingsFixture, MissingKeysFallBackToGivenDefault)
{
    const auto caseName = "MissingKeys";
    removeProbeFiles(caseName);

    SettingsStore store(probePath(caseName));
    EXPECT_EQ(store.stringValue("Paths/NeverSet", QStringLiteral("回落")), QStringLiteral("回落"));
    EXPECT_EQ(store.intValue("Record/NeverSet", 7), 7);
    EXPECT_TRUE(store.boolValue("Capture/NeverSet", true));
    EXPECT_EQ(store.stringListValue("Ocr/NeverSet", QStringList{"x"}), (QStringList{"x"}));
    EXPECT_TRUE(store.allKeys().isEmpty()) << "一个键都没写，allKeys 不该报出条目";
}

/**
 * @brief 钉住坏值按默认值回落，同时后端里的原值不被悄悄改写
 * @details 这一格是 boolValue 的关键：QVariant::toBool() 会把 "maybe" 读成真，
 *          于是「一个拼错的开关」会静默变成「打开」。判据是拿默认值而不是拿真。
 */
TEST_F(SettingsFixture, MalformedValuesFallBackInsteadOfCoercing)
{
    const auto caseName = "Malformed";
    removeProbeFiles(caseName);
    const auto iniFile = probePath(caseName);

    writeRawValue(iniFile, "Record/Fps", "12abc");
    writeRawValue(iniFile, "Capture/CursorHighlight", "maybe");
    writeRawValue(iniFile, "Theme/Mode", "   ");

    // 日志先接到位：回落不许是静默的，这一格同时验「警告真的写进了日志」而不只是代码里有一句
    const auto      logDirectory = std::filesystem::temp_directory_path() / std::format("GyanisScribe-Settings-{}-logs", caseName);
    std::error_code cleanupError;
    std::filesystem::remove_all(logDirectory, cleanupError);
    ASSERT_TRUE(GyanisScribe::Core::Logger::initialize(logDirectory).has_value()) << "用例自带的日志目录建不出来";

    SettingsStore store(iniFile);
    EXPECT_EQ(store.intValue("Record/Fps", 15), 15) << "把 \"12abc\" 截断成 12 就是静默变形";
    EXPECT_FALSE(store.boolValue("Capture/CursorHighlight", false)) << "词表外的文本被读成了真，开关的含义就此丢失";
    EXPECT_TRUE(store.boolValue("Capture/CursorHighlight", true)) << "词表外的文本没走回落，默认值起不了作用";
    EXPECT_EQ(store.stringValue("Theme/Mode", QStringLiteral("System")), QStringLiteral("System")) << "空文本应当按缺失处理";
    const auto logText = readFile(logDirectory / "GyanisScribe.log");
    EXPECT_NE(logText.find("不是十进制整数"), std::string::npos) << "整数回落没留日志，配置写错的人永远不会知道";
    EXPECT_NE(logText.find("不在布尔词表"), std::string::npos) << "布尔回落没留日志";
    EXPECT_NE(logText.find("空文本"), std::string::npos) << "文本回落没留日志";
    GyanisScribe::Core::Logger::shutdown();
}

/**
 * @brief 钉住布尔词表的接受面：三种写法都指向同一个真值
 */
TEST_F(SettingsFixture, BooleanVocabularyAcceptsThreeSpellings)
{
    const auto caseName = "BooleanVocabulary";
    removeProbeFiles(caseName);

    SettingsStore store(probePath(caseName));
    for (const auto &spelling: {"true", "1", "yes", "  TRUE  ", "Yes"})
    {
        ASSERT_TRUE(store.setValue("Capture/CursorHighlight", QVariant(QString::fromUtf8(spelling))).has_value());
        EXPECT_TRUE(store.boolValue("Capture/CursorHighlight", false)) << "写法「" << spelling << "」应当被认成打开";
    }
    for (const auto &spelling: {"false", "0", "no", "FALSE"})
    {
        ASSERT_TRUE(store.setValue("Capture/CursorHighlight", QVariant(QString::fromUtf8(spelling))).has_value());
        EXPECT_FALSE(store.boolValue("Capture/CursorHighlight", true)) << "写法「" << spelling << "」应当被认成关闭";
    }
}

/**
 * @brief 钉住空键被拒绝且不发出变更通知
 */
TEST_F(SettingsFixture, EmptyKeyIsRejectedWithoutNotification)
{
    const auto caseName = "EmptyKey";
    removeProbeFiles(caseName);

    SettingsStore store(probePath(caseName));
    int           notified = 0;
    QObject::connect(&store, &SettingsStore::valueChanged, [&notified](const QString &) { ++notified; });

    const auto rejected = store.setValue("", QVariant(1));
    ASSERT_FALSE(rejected.has_value()) << "空键写进去了，后面谁都读不到它，却会占住一次通知";
    EXPECT_EQ(rejected.error().code, ErrorCode::InvalidArgument);
    EXPECT_NE(rejected.error().message.find("键名为空"), std::string::npos) << "文案要写给用户看的中文原因，实际是：" << rejected.error().message;
    EXPECT_EQ(notified, 0) << "失败的写入也发了通知，订阅者会为一个没落地的值去重载";
}

/**
 * @brief 钉住每写一次发一次通知，且通知里带的就是那个键
 */
TEST_F(SettingsFixture, EachSuccessfulWriteNotifiesItsOwnKey)
{
    const auto caseName = "Notify";
    removeProbeFiles(caseName);

    SettingsStore        store(probePath(caseName));
    std::vector<QString> seen;
    QObject::connect(&store, &SettingsStore::valueChanged, [&seen](const QString &key) { seen.push_back(key); });

    ASSERT_TRUE(store.setValue("Paths/Screenshot", QVariant(QStringLiteral("D:/A"))).has_value());
    ASSERT_TRUE(store.setValue("Theme/Mode", QVariant(QStringLiteral("Dark"))).has_value());
    // 同键重写两次也要各发一次，否则设置页改完不生效
    ASSERT_TRUE(store.setValue("Theme/Mode", QVariant(QStringLiteral("Light"))).has_value());

    ASSERT_EQ(seen.size(), 3U) << "写入次数与通知条数不同解";
    EXPECT_EQ(seen[0], QStringLiteral("Paths/Screenshot"));
    EXPECT_EQ(seen[1], QStringLiteral("Theme/Mode"));
    EXPECT_EQ(seen[2], QStringLiteral("Theme/Mode"));
    EXPECT_EQ(store.stringValue("Theme/Mode", QString()), QStringLiteral("Light"));
}

/**
 * @brief 钉住导出→导入这条往返：列表与文本都要原样回到另一份存储里
 */
TEST_F(SettingsFixture, ExportThenImportRestoresIntoAnotherStore)
{
    const auto caseName = "RoundAbout";
    removeProbeFiles(caseName);
    const auto sourceFile = probePath(caseName, "source");
    const auto targetFile = probePath(caseName, "target");
    const auto jsonFile   = std::filesystem::temp_directory_path() / std::format("GyanisScribe-Settings-{}.json", caseName);

    SettingsStore source(sourceFile);
    ASSERT_TRUE(source.setValue("Paths/Screenshot", QVariant(QStringLiteral("D:/Shots"))).has_value());
    ASSERT_TRUE(source.setValue("Record/Fps", QVariant(24)).has_value());
    ASSERT_TRUE(source.setValue("Ocr/Languages", QVariant(QStringList{"zh-CN", "en-US"})).has_value());
    source.sync();

    ASSERT_TRUE(source.exportToFile(jsonFile).has_value()) << "导出应当成功";
    ASSERT_TRUE(std::filesystem::exists(jsonFile)) << "导出说成功但文件不在：" << jsonFile.string();

    SettingsStore target(targetFile);
    const auto    imported = target.importFromFile(jsonFile);
    ASSERT_TRUE(imported.has_value()) << "导入失败，原因：" << (imported.has_value() ? std::string{} : imported.error().message);
    EXPECT_EQ(imported.value(), 3) << "导入的键数与导出的不符";
    EXPECT_EQ(target.stringValue("Paths/Screenshot", QString()), QStringLiteral("D:/Shots"));
    EXPECT_EQ(target.intValue("Record/Fps", -1), 24);
    EXPECT_EQ(target.stringListValue("Ocr/Languages", QStringList()), (QStringList{"zh-CN", "en-US"})) << "列表在导出里退化成文本，换后端就取不回原来的两项";

    std::error_code ignored;
    std::filesystem::remove(jsonFile, ignored);
}

/**
 * @brief 钉住导入的拒绝面：文件不存在与文件不是 JSON 对象都要给出原因
 */
TEST_F(SettingsFixture, ImportRejectsUnreadableAndMalformedFiles)
{
    const auto caseName  = "ImportReject";
    const auto absent    = probePath(caseName, "absent");
    const auto malformed = probePath(caseName, "malformed");
    removeProbeFiles(caseName);

    SettingsStore store(probePath(caseName, "store"));

    const auto missingFile = store.importFromFile(absent);
    ASSERT_FALSE(missingFile.has_value()) << "文件不存在却报成功，用户会以为配置已经换掉了";
    EXPECT_EQ(missingFile.error().code, ErrorCode::InvalidArgument);
    EXPECT_NE(missingFile.error().message.find("读不到"), std::string::npos) << "文案要写清是读不到，实际：" << missingFile.error().message;

    {
        std::ofstream file(malformed, std::ios::binary);
        file << "这不是 JSON";
    }
    const auto notAnObject = store.importFromFile(malformed);
    ASSERT_FALSE(notAnObject.has_value()) << "解析不出来却返回成功计数，坏文件会被当成空配置";
    EXPECT_NE(notAnObject.error().message.find("JSON 对象"), std::string::npos) << "文案要指出形状不对，实际：" << notAnObject.error().message;
}
