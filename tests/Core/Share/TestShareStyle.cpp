#include "Core/Share/ShareStyle.h"

#include <QColor>
#include <QJsonObject>

#include <gtest/gtest.h>

using namespace GyanisScribe::Core;

namespace
{
    [[nodiscard]] ShareStyle fullyCustomStyle()
    {
        ShareStyle style;
        style.backgroundTop      = QColor{0x12, 0x34, 0x56};
        style.backgroundBottom   = QColor{0x65, 0x43, 0x21};
        style.gradient           = true;
        style.paddingPixels      = 12;
        style.cornerRadiusPixels = 34;
        style.border             = true;
        style.borderPixels       = 3;
        style.borderColor        = QColor{0xAA, 0xBB, 0xCC};
        style.shadow             = false;
        style.shadowPixels       = 7;
        style.editorChrome       = true;
        style.editorTitle        = QStringLiteral("hello.cpp");
        style.editorChromeColor  = QColor{0x01, 0x02, 0x03};
        style.editorTitleColor   = QColor{0x04, 0x05, 0x06};
        style.watermarkText      = QStringLiteral("内部资料");
        style.watermarkOpacity   = 200;
        style.watermarkCorner    = ShareWatermarkCorner::BottomLeft;
        return style;
    }
} // namespace

/// 内置四套、顺序固定；缺一套或顺序变了，界面与文档就同时对不上
TEST(ShareStyle, BuiltInsCoverTheDocumentedFour)
{
    const auto templates = builtInShareTemplates();
    ASSERT_EQ(templates.size(), 4u);
    EXPECT_EQ(templates[0].id, QStringLiteral("light"));
    EXPECT_EQ(templates[1].id, QStringLiteral("dark"));
    EXPECT_EQ(templates[2].id, QStringLiteral("gradient"));
    EXPECT_EQ(templates[3].id, QStringLiteral("code"));
    EXPECT_TRUE(templates[2].style.gradient);
    EXPECT_TRUE(templates[3].style.editorChrome);
    EXPECT_TRUE(templates[3].style.editorTitle.isEmpty());
}

/// 缺省样式必须合法；JSON 读回的完全自定义样式必须逐字段相等
TEST(ShareStyle, JsonRoundTripsEveryField)
{
    const auto source = fullyCustomStyle();
    ASSERT_TRUE(validateShareStyle(source).has_value());
    const auto parsed = parseShareStyle(shareStyleToJson(source));
    ASSERT_TRUE(parsed.has_value()) << parsed.error().message;
    EXPECT_EQ(*parsed, source);
}

/// 缺字段取默认值，不让旧模板因为新增一格就读不出来
TEST(ShareStyle, MissingFieldsFallBackToDefaults)
{
    const auto parsed = parseShareStyle(QJsonObject{});
    ASSERT_TRUE(parsed.has_value()) << parsed.error().message;
    EXPECT_EQ(*parsed, defaultShareTemplate().style);
}

/// 越界字段逐条拒绝，错误里带范围
TEST(ShareStyle, OutOfRangeFieldsAreRejected)
{
    const auto expectRejected = [](const char *key, const QJsonValue &value)
    {
        QJsonObject json;
        json[key]         = value;
        const auto parsed = parseShareStyle(json);
        EXPECT_FALSE(parsed.has_value()) << key;
        if (!parsed)
        {
            EXPECT_NE(parsed.error().message.find("美化样式"), std::string::npos) << key << ": " << parsed.error().message;
        }
    };
    expectRejected("padding", 257);
    expectRejected("cornerRadius", 97);
    expectRejected("borderWidth", 9);
    expectRejected("shadowWidth", 65);
    expectRejected("watermarkOpacity", 256);
    expectRejected("backgroundTop", QStringLiteral("not-a-color"));
}

/// 坏 JSON 与坏条目只丢自己，不挡整串模板
TEST(ShareStyle, BadJsonAndBadEntriesAreDropped)
{
    EXPECT_TRUE(parseShareTemplates(QStringLiteral("{oops")).empty());
    EXPECT_TRUE(parseShareTemplates(QString{}).empty());

    auto valid        = builtInShareTemplates().front();
    valid.id          = QStringLiteral("custom-1");
    valid.name        = QStringLiteral("我的模板");
    const auto json   = serializeShareTemplates({valid});
    const auto parsed = parseShareTemplates(json);
    ASSERT_EQ(parsed.size(), 1u);
    EXPECT_EQ(parsed.front().name, QStringLiteral("我的模板"));
}

/// 自定义模板可以保存/复用/删除；重名、内置名、内置 id 都拒绝
TEST(ShareStyle, CustomTemplatesAreAddedAndRemoved)
{
    const auto style = fullyCustomStyle();
    auto       added = addCustomShareTemplate({}, QStringLiteral("我的模板"), style);
    ASSERT_TRUE(added.has_value()) << added.error().message;
    ASSERT_EQ(added->size(), 1u);
    EXPECT_FALSE((*added)[0].id.isEmpty());
    EXPECT_EQ((*added)[0].style, style);

    EXPECT_FALSE(addCustomShareTemplate(*added, QStringLiteral("我的模板"), style).has_value()) << "重名应拒绝";
    EXPECT_FALSE(addCustomShareTemplate(*added, QStringLiteral("留白"), style).has_value()) << "内置名应拒绝";
    EXPECT_FALSE(addCustomShareTemplate(*added, QString{}, style).has_value()) << "空名应拒绝";
    EXPECT_FALSE(addCustomShareTemplate(*added, QString(41, QChar(u'名')), style).has_value()) << "超长名应拒绝";

    auto removed = removeCustomShareTemplate(*added, (*added)[0].id);
    ASSERT_TRUE(removed.has_value()) << removed.error().message;
    EXPECT_TRUE(removed->empty());
    EXPECT_FALSE(removeCustomShareTemplate(*added, QStringLiteral("light")).has_value()) << "内置 id 不可删";
    EXPECT_FALSE(removeCustomShareTemplate(*added, QStringLiteral("missing")).has_value());
}

/// 自定义模板最多 30 套
TEST(ShareStyle, CustomTemplateCountIsCapped)
{
    auto templates = std::vector<ShareTemplate>{};
    for (auto index = 0; index < kMaximumCustomShareTemplates; ++index)
    {
        const auto name  = QStringLiteral("模板 %1").arg(index);
        auto       added = addCustomShareTemplate(templates, name, ShareStyle{});
        ASSERT_TRUE(added.has_value()) << added.error().message;
        templates = *added;
    }
    EXPECT_FALSE(addCustomShareTemplate(templates, QStringLiteral("再来一套"), ShareStyle{}).has_value());
}
