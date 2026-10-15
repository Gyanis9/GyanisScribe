#include "Pin/PinLayout.h"

#include "Pin/PinTransform.h"

#include "Core/Settings/ISettingsStore.h"
#include "Core/Settings/SettingsKeys.h"
#include "Core/Settings/SettingsStore.h"

#include <QColor>
#include <QCoreApplication>
#include <QMap>
#include <QPoint>
#include <QSizeF>
#include <QString>

#include <gtest/gtest.h>

#include <cmath>
#include <array>
#include <filesystem>
#include <format>
#include <memory>
#include <string>

using namespace GyanisScribe::Pin;
using GyanisScribe::Base::ErrorCode;
using GyanisScribe::Core::SettingsStore;

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
    QVector<PinState> states{
            sample(10, 20, 1.5, 0.0, 1.0),
            sample(-300, 40, 0.25, 90.0, 0.4),
            sample(0, 0, 20.0, 359.0, 0.05),
    };
    states[1].group = 2;

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
        EXPECT_EQ((*parsed)[index].group, states[index].group) << "第 " << index << " 项的分组号变了";
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
 * @brief 钉住分组字段的往返、旧布局的兼容与非法组号的拒绝面
 * @details group 是后加的字段：上一版存下的布局没有它，升级后第一次启动必须照样摆得回来
 *          （缺省落第一组），否则用户的桌面会被清空一次；写了非法值的记录仍然按坏文件拒掉。
 */
TEST_F(PinLayoutFixture, GroupRoundTripsWhileOldLayoutsDefaultToTheFirstGroup)
{
    auto withGroup    = sample(10, 20, 1.0, 0.0, 1.0);
    withGroup.group   = 3;
    const auto parsed = parseLayout(serializeLayout(QVector<PinState>{withGroup}).toStdString());
    ASSERT_TRUE(parsed.has_value()) << "自己写出的布局读不回来：" << parsed.error().message;
    EXPECT_EQ(parsed->front().group, 3) << "分组号没进布局文本";

    // 上一版写下的记录：没有 group 字段
    const auto legacy = parseLayout(R"([{"x":1,"y":2,"width":10,"height":10,"scale":1,"rotation":0,"opacity":1,"imageFile":"a.png"}])");
    ASSERT_TRUE(legacy.has_value()) << "旧布局读不出来，升级后贴图会一张都摆不回：" << legacy.error().message;
    EXPECT_EQ(legacy->front().group, 1) << "缺 group 字段的记录该落在第一组";

    const auto zeroGroup = parseLayout(R"([{"x":1,"y":2,"width":10,"height":10,"scale":1,"rotation":0,"opacity":1,"imageFile":"a.png","group":0}])");
    ASSERT_FALSE(zeroGroup.has_value()) << "组号 0 不是合法分组，读进去就是一张谁也切不到的贴图";
    EXPECT_NE(zeroGroup.error().message.find("分组号"), std::string::npos) << "要写出坏的是分组号：" << zeroGroup.error().message;
}

/**
 * @brief 钉住置顶与穿透两个开关的往返、旧布局的缺省与非布尔值的拒绝面
 * @details 两个字段与 group 同一条规矩：缺省要能读旧布局（按今天的新建行为），
 *          写了但不是布尔值才是坏记录——JSON 里 "yes" 会被 toBool() 折成 false，静默把一个开关读反。
 */
TEST_F(PinLayoutFixture, TopmostAndThroughRoundTripWhileOldLayoutsDefaultToTodaysBehaviour)
{
    auto flipped         = sample(10, 20, 1.0, 0.0, 1.0);
    flipped.alwaysOnTop  = false;
    flipped.mouseThrough = true;
    flipped.sourceFile   = QStringLiteral("C:/tmp/two-frames.gif");
    const auto parsed    = parseLayout(serializeLayout(QVector<PinState>{flipped}).toStdString());
    ASSERT_TRUE(parsed.has_value()) << "自己写出的布局读不回来：" << parsed.error().message;
    EXPECT_FALSE(parsed->front().alwaysOnTop) << "置顶那一档没进布局文本";
    EXPECT_TRUE(parsed->front().mouseThrough) << "穿透那一档没进布局文本";
    EXPECT_EQ(parsed->front().sourceFile, flipped.sourceFile) << "动图来源没进布局文本，重启后动图就变静图";

    const auto legacy = parseLayout(R"([{"x":1,"y":2,"width":10,"height":10,"scale":1,"rotation":0,"opacity":1,"imageFile":"a.png"}])");
    ASSERT_TRUE(legacy.has_value()) << "旧布局读不出来：" << legacy.error().message;
    EXPECT_TRUE(legacy->front().alwaysOnTop) << "旧布局缺字段时该按新建行为：置顶";
    EXPECT_FALSE(legacy->front().mouseThrough) << "旧布局缺字段时该按新建行为：不穿透";
    EXPECT_TRUE(legacy->front().sourceFile.isEmpty()) << "旧布局缺字段时该按静图处理";

    const auto notBool = parseLayout(R"([{"x":1,"y":2,"width":10,"height":10,"scale":1,"rotation":0,"opacity":1,"imageFile":"a.png","alwaysOnTop":"yes"}])");
    ASSERT_FALSE(notBool.has_value()) << "非布尔值被 toBool 折成 false，等于静默读反一个开关";
    EXPECT_NE(notBool.error().message.find("置顶字段"), std::string::npos) << "要写出坏的是哪一个字段：" << notBool.error().message;
}

/**
 * @brief 钉住透明区这一档进得了布局，旧布局缺字段落回旧行为，越界或写歪的数字被拒
 * @details 这里存的是档位序号。读到一个不存在的档必须报错而不是悄悄当成第一档——用户选的是棋盘，
 *          静默换成底色就是「重启之后我的棋盘没了」这种在代码里查不出来的坏法。
 */
TEST_F(PinLayoutFixture, TransparentBackdropRoundTripsAndRejectsUnknownSteps)
{
    auto checkered                = sample(10, 20, 1.0, 0.0, 1.0);
    checkered.transparentBackdrop = TransparentBackdrop::DarkChecker;
    const auto parsed             = parseLayout(serializeLayout(QVector<PinState>{checkered}).toStdString());
    ASSERT_TRUE(parsed.has_value()) << "自己写出的布局读不回来：" << parsed.error().message;
    EXPECT_EQ(parsed->front().transparentBackdrop, TransparentBackdrop::DarkChecker) << "透明区那一档没进布局文本";

    const auto legacy = parseLayout(R"([{"x":1,"y":2,"width":10,"height":10,"scale":1,"rotation":0,"opacity":1,"imageFile":"a.png"}])");
    ASSERT_TRUE(legacy.has_value()) << "旧布局读不出来：" << legacy.error().message;
    EXPECT_EQ(legacy->front().transparentBackdrop, TransparentBackdrop::ThemeBase) << "旧布局缺字段时该落在这一格出现之前的行为";

    constexpr std::array<const char *, 3> bad = {
            R"([{"x":1,"y":2,"width":10,"height":10,"scale":1,"rotation":0,"opacity":1,"imageFile":"a.png","transparentBackdrop":7}])",
            R"([{"x":1,"y":2,"width":10,"height":10,"scale":1,"rotation":0,"opacity":1,"imageFile":"a.png","transparentBackdrop":-1}])",
            R"([{"x":1,"y":2,"width":10,"height":10,"scale":1,"rotation":0,"opacity":1,"imageFile":"a.png","transparentBackdrop":"checker"}])",
    };
    for (const char *record: bad)
    {
        const auto rejected = parseLayout(record);
        ASSERT_FALSE(rejected.has_value()) << "这一条坏布局被当成了合法的一档：" << record;
        EXPECT_NE(rejected.error().message.find("透明区"), std::string::npos) << "要写出坏的是哪一个字段：" << rejected.error().message;
    }
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

    EXPECT_TRUE(reopened.allKeys().contains(QString::fromUtf8(GyanisScribe::Core::kPinLayout.data()))) << "布局键名与 SettingsKeys 里的常量不同解";
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
        EXPECT_EQ(parsed.error().code, GyanisScribe::Base::ErrorCode::InvalidArgument) << bad;
        EXPECT_NE(parsed.error().message.find("Pin/DefaultOpacity"), std::string::npos) << bad;
    }
}

/**
 * @brief 钉住分组名字的记录：空值不算名字，形状坏了要报出补救的那一格
 * @details 名字只是标签，但坏记录不能当成「都没起过名字」——那一桌的标签会集体消失，
 *          而用户看到的是「我起的名字自己没了」。拒绝文案要点名 Pin/GroupNames，才知道去哪清。
 */
TEST(PinLayoutGroupNames, RoundTripsAndRejectsMalformedRecords)
{
    const QMap<int, QString> names{{2, QStringLiteral("参照对比")}, {3, QStringLiteral("  ")}};
    const auto               text = GyanisScribe::Pin::serializeGroupNames(names);
    EXPECT_EQ(text.indexOf(QStringLiteral("3")), -1) << "只有空白的名字不该被写进记录：读回来会多一条没人认领的键";

    const auto parsed = GyanisScribe::Pin::parseGroupNames(text.toStdString());
    ASSERT_TRUE(parsed.has_value()) << parsed.error().message;
    ASSERT_EQ(parsed->size(), 1);
    EXPECT_EQ(parsed->value(2), QStringLiteral("参照对比")) << "名字过了记录一圈就变样";

    const auto empty = GyanisScribe::Pin::parseGroupNames(std::string_view{});
    ASSERT_TRUE(empty.has_value()) << "没存过就是空表，不该报坏记录：" << empty.error().message;
    EXPECT_TRUE(empty->isEmpty());

    for (const char *bad: {R"([{"2":"参照"}])", R"({"0":"第一组不存在的号"})", R"({"x":"键不是数"})", R"({"2":7})"})
    {
        const auto rejected = GyanisScribe::Pin::parseGroupNames(bad);
        ASSERT_FALSE(rejected.has_value()) << "这一条坏记录被当成没起过名字了：" << bad;
        EXPECT_NE(rejected.error().message.find("Pin/GroupNames"), std::string::npos) << "拒绝要说清去哪一项补救，实际：" << rejected.error().message;
    }
}

/**
 * @brief 钉住轮换顺序读的是「顺序」本身：照抄名单顺序、容忍空白、拒掉非正数与重复
 * @details
 * 重复组号必须拒而不是后写的盖掉前写的：那一格在一圈里出现两次，切组就会在同一处多跳一步，
 *          而界面上只看得到「顺序不对」，说不清是哪一条填歪了。

 */
TEST_F(PinLayoutFixture, GroupRotationParseKeepsOrderAndRejectsBadTokens)
{
    const auto clean = parseGroupRotation(QStringList{QStringLiteral("3"), QStringLiteral(" 1 "), QStringLiteral("2")});
    ASSERT_TRUE(clean.has_value()) << clean.error().message;
    EXPECT_EQ(*clean, (QVector<int>{3, 1, 2})) << "名单被读成了升序，那一圈就白排了";

    const auto blank = parseGroupRotation(QStringList{});
    ASSERT_TRUE(blank.has_value()) << "空名单就是「按升序」，不该报坏：" << blank.error().message;
    EXPECT_TRUE(blank->isEmpty());

    const auto holes = parseGroupRotation(QStringList{QStringLiteral("2"), QStringLiteral(""), QStringLiteral("  ")});
    ASSERT_TRUE(holes.has_value()) << "连写的逗号与首尾空白是分隔符，不是坏值：" << holes.error().message;
    EXPECT_EQ(*holes, (QVector<int>{2}));

    for (const char *bad: {"0", "-2", "x", "1.5", "2a"})
    {
        const auto rejected = parseGroupRotation(QStringList{QString::fromLatin1(bad)});
        ASSERT_FALSE(rejected.has_value()) << "这一格被当成了合法组号：" << bad;
        EXPECT_NE(rejected.error().message.find("分组轮换顺序"), std::string::npos) << "要说清坏在哪一格：" << rejected.error().message;
    }

    const auto twice = parseGroupRotation(QStringList{QStringLiteral("2"), QStringLiteral("3"), QStringLiteral("2")});
    ASSERT_FALSE(twice.has_value()) << "重复的组号被当成了合法名单，切组会在同一处多跳一步";
    EXPECT_NE(twice.error().message.find("两次"), std::string::npos) << twice.error().message;
}

/**
 * @brief 钉住真正走的那一圈：在册的按名单排，没点名的补在末尾，一个都不落
 * @details 「没点名的补上」是这一档的可用性底线——新建一组之后切组切不到它，那一组在键盘上就是死角。

 * *          名单里留着早已并掉的组号要跳过：停在一组不存在的贴图前，等于按一次键什么都不动。
 */
TEST_F(PinLayoutFixture, RotationCycleKeepsEveryGroupReachable)
{
    const QVector<int> inUse{1, 2, 3};

    EXPECT_EQ(rotationCycle({}, inUse), inUse) << "空名单该就是升序";
    EXPECT_EQ(rotationCycle({3, 1}, inUse), (QVector<int>{3, 1, 2})) << "没点名的第二组没补在末尾";
    EXPECT_EQ(rotationCycle({9, 3, 2}, inUse), (QVector<int>{3, 2, 1})) << "不在册的组号该跳过，剩下的照名单排";
    EXPECT_EQ(rotationCycle({2, 1, 3}, inUse), (QVector<int>{2, 1, 3})) << "名单完整时就该原样走";

    // 不变式：一圈里的组恰好是「在册的那些」，不重不漏——多一格或少一格都会让切组停在看不见的位置上
    const std::array<QVector<int>, 6> orders{QVector<int>{}, QVector<int>{1}, QVector<int>{3, 2, 1}, QVector<int>{9}, QVector<int>{2, 9, 1}, QVector<int>{1, 1}};
    for (std::size_t index = 0; index < orders.size(); ++index)
    {
        const auto cycle = rotationCycle(orders[index], inUse);
        ASSERT_EQ(cycle.size(), inUse.size()) << "第 " << index << " 份名单算出的一圈长度与在册组数不同";
        for (const auto group: inUse)
        {
            EXPECT_EQ(cycle.count(group), 1) << "第 " << index << " 份名单里，组 " << group << " 在一圈中出现了 " << cycle.count(group) << " 次";
        }
    }
}

/**
 * @brief 钉住色卡原色进得了布局，旧记录里没有这一格时不当作坏值
 * @details 这一格是后加的字段：旧布局里没有它，读回来必须是「不是色卡」而不是整条记录被拒；
 *          写了就原样带回来——只存不读或只读不存，重启之后改色这一项都会回到改之前。
 */
TEST_F(PinLayoutFixture, ColorCardHexRoundTripsAndDefaultsToEmpty)
{
    auto card = sample(5, 5, 1.0, 0.0, 1.0);
    card.colorCardHex = QColor(0x12, 0x34, 0x56).name(QColor::HexArgb);
    const auto parsed = parseLayout(serializeLayout(QVector<PinState>{card}).toStdString());
    ASSERT_TRUE(parsed.has_value()) << parsed.error().message;
    ASSERT_EQ(parsed->size(), 1);
    EXPECT_EQ(parsed->front().colorCardHex, card.colorCardHex) << "色卡原色没跟着布局走：重启后这一项会消失";

    const auto legacy = parseLayout(R"([{"x":1,"y":2,"width":10,"height":10,"scale":1,"rotation":0,"opacity":1,"imageFile":"a.png"}])");
    ASSERT_TRUE(legacy.has_value()) << "旧记录里没有色卡那一格，整条记录被拒了";
    EXPECT_TRUE(legacy->front().colorCardHex.isEmpty()) << "缺省该是「不是色卡」";

    // 普通贴图那一份同样写这一格（空串），与 sourceFile 同一条约定：字段齐全，读端按空串判「不是色卡」
    const auto plain = sample(5, 5, 1.0, 0.0, 1.0);
    const auto roundTrip = parseLayout(serializeLayout(QVector<PinState>{plain}).toStdString());
    ASSERT_TRUE(roundTrip.has_value());
    EXPECT_TRUE(roundTrip->front().colorCardHex.isEmpty()) << "普通贴图往返一趟多出一个原色";
}
