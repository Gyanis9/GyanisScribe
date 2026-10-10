#include "Pin/PinLayout.h"

#include "Pin/PinTransform.h"

#include "Settings/ISettingsStore.h"
#include "Settings/SettingsKeys.h"
#include "Settings/SettingsStore.h"

#include <QCoreApplication>
#include <QPoint>
#include <QSizeF>
#include <QString>

#include <gtest/gtest.h>

#include <cmath>
#include <filesystem>
#include <format>
#include <memory>
#include <string>

using namespace GyanisScribe::Pin;
using GyanisScribe::Core::ErrorCode;
using GyanisScribe::Settings::SettingsStore;

namespace
{
    std::unique_ptr<QCoreApplication> application;

    class PinLayoutFixture : public ::testing::Test
    {
    protected:
        static void SetUpTestSuite()
        {
            // argv 与 argc 的缓冲要有静态存储期：Qt 只存指针不拷贝，
            // 而 QCoreApplication::applicationDirPath() 这类调用是在装置函数返回之后才读它们。
            static int   argumentCount  = 1;
            static char  argumentZero[] = "TestPinLayout";
            static char *arguments[]    = {argumentZero, nullptr};
            application                 = std::make_unique<QCoreApplication>(argumentCount, arguments);
            QCoreApplication::setOrganizationName(QStringLiteral("Gyanis"));
            QCoreApplication::setApplicationName(QStringLiteral("GyanisScribe-Tests"));
        }

        static void TearDownTestSuite()
        {
            application.reset();
        }

        void SetUp() override
        {
            const auto caseName = std::string_view(::testing::UnitTest::GetInstance()->current_test_info()->name());
            m_iniFile           = std::filesystem::temp_directory_path() / std::format("GyanisScribe-PinLayout-{}.ini", caseName);
            std::error_code ignored;
            std::filesystem::remove(m_iniFile, ignored);
        }

        std::filesystem::path m_iniFile;
    };

    PinState sample(qint32 x, qint32 y, double scale, double rotation, double opacity)
    {
        // 路径是布局里唯一指向像素的那根线，往返必须一个字都不改
        return PinState{QPoint(x, y), QSizeF(120.0, 80.0), scale, rotation, opacity, QStringLiteral("Pins/pin-0.png")};
    }
} // namespace

/**
 * @brief 钉住序列化与解析的往返：三项进去三项出来，每个字段都保持原值
 */
TEST_F(PinLayoutFixture, RoundTripsEveryField)
{
    const QVector<PinState> states{
            sample(10, 20, 1.5, 0.0, 1.0),
            sample(-300, 40, 0.25, 90.0, 0.4),
            sample(0, 0, 20.0, 359.0, 0.05),
    };

    const auto text = serializeLayout(states);
    ASSERT_FALSE(text.isEmpty()) << "空文本会被解析成「没有贴图」，等于把布局丢了";

    const auto parsed = parseLayout(text.toStdString());
    ASSERT_TRUE(parsed.has_value()) << "自己序列化的文本读不回来，原因：" << parsed.error().message;
    ASSERT_EQ(parsed->size(), 3);

    for (int index = 0; index < 3; ++index)
    {
        EXPECT_EQ((*parsed)[index].position, states[index].position) << "第 " << index << " 项位置变了";
        EXPECT_DOUBLE_EQ((*parsed)[index].logicalSize.width(), 120.0);
        EXPECT_DOUBLE_EQ((*parsed)[index].logicalSize.height(), 80.0);
        EXPECT_NEAR((*parsed)[index].scale, states[index].scale, 1e-6) << "缩放被改";
        EXPECT_NEAR((*parsed)[index].rotationDegrees, states[index].rotationDegrees, 1e-6) << "角度被改";
        EXPECT_NEAR((*parsed)[index].opacity, states[index].opacity, 1e-6) << "透明度被改";
        EXPECT_EQ((*parsed)[index].imageFile, states[index].imageFile) << "图片位置被改，恢复时就读不到那张图了";
    }
}

/**
 * @brief 钉住「没存过」与「存了空列表」都是空结果，而不是失败
 */
TEST_F(PinLayoutFixture, EmptyTextAndEmptyListBothParseToNoPins)
{
    const auto emptyText = parseLayout("");
    ASSERT_TRUE(emptyText.has_value());
    EXPECT_TRUE(emptyText->isEmpty()) << "第一次启动没有布局，报失败会让启动流程多一条假错误";

    const auto emptyList = parseLayout("[]");
    ASSERT_TRUE(emptyList.has_value());
    EXPECT_TRUE(emptyList->isEmpty());
}

/**
 * @brief 钉住写出时对越界取值的夹取，与读取端同一口径
 */
TEST_F(PinLayoutFixture, SerializingClampsOutOfRangeScale)
{
    const QVector<PinState> states{sample(5, 5, 999.0, 0.0, 1.0)};
    const auto              parsed = parseLayout(serializeLayout(states).toStdString());
    ASSERT_TRUE(parsed.has_value());
    ASSERT_EQ(parsed->size(), 1);
    EXPECT_NEAR(parsed->front().scale, kMaximumScale, 1e-6) << "写出时不夹取，读回来就是一个画不出的尺寸";
}

/**
 * @brief 钉住解析的拒绝面：形状、字段、取值三档都各点名到哪一项
 */
TEST_F(PinLayoutFixture, RejectsMalformedEntriesByName)
{
    const auto notJson = parseLayout("这不是 JSON");
    ASSERT_FALSE(notJson.has_value()) << "坏文本被当成空布局，重启后贴图会全丢";
    EXPECT_EQ(notJson.error().code, ErrorCode::InvalidArgument);
    EXPECT_NE(notJson.error().message.find("JSON 数组"), std::string::npos);

    const auto notArray = parseLayout(R"({"x":1})");
    ASSERT_FALSE(notArray.has_value()) << "对象不是数组，按空布局处理同样会丢贴图";

    const auto elementNotObject = parseLayout(R"([1,2])");
    ASSERT_FALSE(elementNotObject.has_value()) << "数组里混进非对象项时不该跳过它";
    EXPECT_NE(elementNotObject.error().message.find("第 1 项"), std::string::npos) << "要指出是哪一项坏了";

    // 必填字段少一个都读不出：旧断言曾只查 opacity，漏在列表外的 rotation 会以「0 度」悄悄恢复过去
    const auto missingField = parseLayout(R"([{"x":1,"y":2,"width":10,"height":10,"scale":1}])");
    ASSERT_FALSE(missingField.has_value()) << "缺 rotation 就按 0 度恢复，用户旋过的贴图会摆正";
    EXPECT_NE(missingField.error().message.find("rotation"), std::string::npos) << "要写出缺的是哪个字段";

    const auto missingImage = parseLayout(R"([{"x":1,"y":2,"width":10,"height":10,"scale":1,"rotation":0,"opacity":1}])");
    ASSERT_FALSE(missingImage.has_value()) << "没有图片位置的记录摆不回像素，只能是一张空窗口";
    EXPECT_NE(missingImage.error().message.find("imageFile"), std::string::npos) << "要写出缺的是哪个字段";

    const auto emptyImage = parseLayout(R"([{"x":1,"y":2,"width":10,"height":10,"scale":1,"rotation":0,"opacity":1,"imageFile":""}])");
    ASSERT_FALSE(emptyImage.has_value()) << "空路径不是「指向某个文件」，读图必然失败却还是占了一项的额度";

    const auto badSize = parseLayout(R"([{"x":1,"y":2,"width":0,"height":10,"scale":1,"rotation":0,"opacity":1,"imageFile":"a.png"}])");
    ASSERT_FALSE(badSize.has_value()) << "零宽度的记录会让窗口塌成看不见的一条线";

    const auto badOpacity = parseLayout(R"([{"x":1,"y":2,"width":10,"height":10,"scale":1,"rotation":0,"opacity":0,"imageFile":"a.png"}])");
    ASSERT_FALSE(badOpacity.has_value()) << "不透明度 0 等于把贴图藏起来，不该当成合法记录";

    const auto overOpacity = parseLayout(R"([{"x":1,"y":2,"width":10,"height":10,"scale":1,"rotation":0,"opacity":1.5,"imageFile":"a.png"}])");
    ASSERT_FALSE(overOpacity.has_value()) << "超过 1 的不透明度没有意义";
}

/**
 * @brief 钉住配置读写这一对：写下去的布局换实例也读得回来
 */
TEST_F(PinLayoutFixture, SaveAndLoadThroughSettingsStore)
{
    SettingsStore           settings(m_iniFile);
    const QVector<PinState> states{sample(15, 25, 2.0, 45.0, 0.6)};

    ASSERT_TRUE(saveLayout(settings, states).has_value()) << "布局写入配置失败";
    settings.sync();

    SettingsStore reopened(m_iniFile);
    const auto    loaded = loadLayout(reopened);
    ASSERT_TRUE(loaded.has_value()) << "读不回来，原因：" << loaded.error().message;
    ASSERT_EQ(loaded->size(), 1);
    EXPECT_EQ(loaded->front().position, QPoint(15, 25));
    EXPECT_NEAR(loaded->front().scale, 2.0, 1e-6);
    EXPECT_NEAR(loaded->front().rotationDegrees, 45.0, 1e-6);
    EXPECT_NEAR(loaded->front().opacity, 0.6, 1e-6);
    EXPECT_EQ(loaded->front().imageFile, states.front().imageFile) << "布局过了配置一道，路径就不一样了";

    EXPECT_TRUE(reopened.allKeys().contains(QString::fromUtf8(GyanisScribe::Settings::kPinLayout.data()))) << "布局键名与 SettingsKeys 里的常量不同解";
}

/**
 * @brief 钉住没写过配置时读出来是空而不是失败
 */
TEST_F(PinLayoutFixture, LoadWithoutStoredLayoutIsEmpty)
{
    SettingsStore settings(m_iniFile);
    const auto    loaded = loadLayout(settings);
    ASSERT_TRUE(loaded.has_value()) << "第一次启动没有布局文件，不该报错";
    EXPECT_TRUE(loaded->isEmpty());
}

/**
 * @brief 钉住默认不透明度百分比到界面取值域的换算与拒绝面
 * @details 下限取 1% 而不是 0%：完全透明的贴图在屏幕上是看不见的存在，
 *          鼠标却还会被它挡住，那种状态比"没有贴图"更难排查。
 */
TEST(PinLayoutOpacitySetting, ConvertsPercentAndRejectsZeroAndOver)
{
    EXPECT_DOUBLE_EQ(GyanisScribe::Pin::parseOpacityPercentSetting(100).value(), 1.0);
    EXPECT_DOUBLE_EQ(GyanisScribe::Pin::parseOpacityPercentSetting(50).value(), 0.5);
    EXPECT_DOUBLE_EQ(GyanisScribe::Pin::parseOpacityPercentSetting(1).value(), 0.01);

    for (const int bad: {0, -10, 101})
    {
        const auto parsed = GyanisScribe::Pin::parseOpacityPercentSetting(bad);
        ASSERT_FALSE(parsed.has_value()) << bad;
        EXPECT_EQ(parsed.error().code, GyanisScribe::Core::ErrorCode::InvalidArgument) << bad;
        EXPECT_NE(parsed.error().message.find("Pin/DefaultOpacity"), std::string::npos) << bad;
    }
}
