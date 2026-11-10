#include "Core/Share/ShareStyle.h"

#include "Base/Text.h"

#include <QColor>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonValue>

#include <gtest/gtest.h>

#include <string>

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

    /// 一条合法的模板目录条目；不写 style 时读回默认样式
    [[nodiscard]] QJsonObject makeTemplateEntry(const QString &id, const QString &name)
    {
        QJsonObject entry;
        entry.insert(QStringLiteral("id"), id);
        entry.insert(QStringLiteral("name"), name);
        return entry;
    }

    /// 包一层版本 1 的目录壳，模拟设置里存下的文本
    [[nodiscard]] QString templatesDocument(const QJsonArray &entries)
    {
        QJsonObject root;
        root.insert(QStringLiteral("version"), 1);
        root.insert(QStringLiteral("templates"), entries);
        return QString::fromUtf8(QJsonDocument{root}.toJson(QJsonDocument::Compact));
    }
} // namespace

/// 内置四套、顺序固定，关键值逐字段对照设计表；缺一套或改一个值，界面与文档就同时对不上
TEST(ShareStyle, BuiltInsCoverTheDocumentedFour)
{
    const auto templates = builtInShareTemplates();
    ASSERT_EQ(templates.size(), 4u);
    ASSERT_EQ(templates[0].id, QStringLiteral("light"));
    ASSERT_EQ(templates[1].id, QStringLiteral("dark"));
    ASSERT_EQ(templates[2].id, QStringLiteral("gradient"));
    ASSERT_EQ(templates[3].id, QStringLiteral("code"));
    for (const auto &item: templates)
    {
        EXPECT_FALSE(item.name.isEmpty()) << item.id.toStdString();
        EXPECT_TRUE(item.style.editorTitle.isEmpty()) << item.id.toStdString();
        EXPECT_TRUE(item.style.watermarkText.isEmpty()) << item.id.toStdString();
    }

    const auto &light = templates[0].style;
    EXPECT_EQ(light.backgroundTop, QColor(0xF5, 0xF6, 0xF8));
    EXPECT_EQ(light.backgroundBottom, QColor(0xF5, 0xF6, 0xF8));
    EXPECT_FALSE(light.gradient);
    EXPECT_EQ(light.paddingPixels, 48);
    EXPECT_EQ(light.cornerRadiusPixels, 16);
    EXPECT_TRUE(light.border);
    EXPECT_EQ(light.borderPixels, 1);
    EXPECT_EQ(light.borderColor, QColor(0xE2, 0xE5, 0xEA));
    EXPECT_TRUE(light.shadow);
    EXPECT_EQ(light.shadowPixels, 32);
    EXPECT_FALSE(light.editorChrome);

    const auto &dark = templates[1].style;
    EXPECT_EQ(dark.backgroundTop, QColor(0x1E, 0x1F, 0x22));
    EXPECT_EQ(dark.backgroundBottom, QColor(0x1E, 0x1F, 0x22));
    EXPECT_FALSE(dark.gradient);
    EXPECT_EQ(dark.paddingPixels, 48);
    EXPECT_EQ(dark.cornerRadiusPixels, 16);
    EXPECT_TRUE(dark.border);
    EXPECT_EQ(dark.borderPixels, 1);
    EXPECT_EQ(dark.borderColor, QColor(0x3A, 0x3D, 0x42));
    EXPECT_TRUE(dark.shadow);
    EXPECT_EQ(dark.shadowPixels, 40);
    EXPECT_FALSE(dark.editorChrome);

    const auto &gradient = templates[2].style;
    EXPECT_EQ(gradient.backgroundTop, QColor(0x6D, 0x5E, 0xF8));
    EXPECT_EQ(gradient.backgroundBottom, QColor(0x3B, 0x82, 0xF6));
    EXPECT_TRUE(gradient.gradient);
    EXPECT_EQ(gradient.paddingPixels, 56);
    EXPECT_EQ(gradient.cornerRadiusPixels, 20);
    EXPECT_FALSE(gradient.border);
    EXPECT_TRUE(gradient.shadow);
    EXPECT_EQ(gradient.shadowPixels, 40);
    EXPECT_FALSE(gradient.editorChrome);

    const auto &code = templates[3].style;
    EXPECT_EQ(code.backgroundTop, QColor(0x0F, 0x11, 0x15));
    EXPECT_EQ(code.backgroundBottom, QColor(0x0F, 0x11, 0x15));
    EXPECT_FALSE(code.gradient);
    EXPECT_EQ(code.paddingPixels, 40);
    EXPECT_EQ(code.cornerRadiusPixels, 12);
    EXPECT_FALSE(code.border);
    EXPECT_TRUE(code.shadow);
    EXPECT_EQ(code.shadowPixels, 40);
    EXPECT_TRUE(code.editorChrome);
    EXPECT_EQ(code.editorChromeColor, QColor(0x1C, 0x1F, 0x26));
    EXPECT_EQ(code.editorTitleColor, QColor(0x9A, 0xA4, 0xB2));
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

/// 缺字段取中性的 ShareStyle{}，不让旧模板因为新增一格就读不出来
TEST(ShareStyle, MissingFieldsFallBackToDefaults)
{
    const auto parsed = parseShareStyle(QJsonObject{});
    ASSERT_TRUE(parsed.has_value()) << parsed.error().message;
    EXPECT_EQ(*parsed, ShareStyle{});
}

/// 越界字段逐条拒绝，错误里带键名与该字段自己的上下限
TEST(ShareStyle, OutOfRangeFieldsAreRejected)
{
    const auto expectRejected = [](const char *key, const char *field, const int minimum, const int maximum, const QJsonValue &value)
    {
        QJsonObject json;
        json.insert(QString::fromLatin1(key), value);
        const auto parsed = parseShareStyle(json);
        ASSERT_FALSE(parsed.has_value()) << key;
        const auto &message = parsed.error().message;
        EXPECT_NE(message.find(field), std::string::npos) << key << ": " << message;
        const auto bounds = std::to_string(minimum) + " 到 " + std::to_string(maximum);
        EXPECT_NE(message.find(bounds), std::string::npos) << key << ": " << message;
    };
    expectRejected("padding", "padding", kMinimumSharePadding, kMaximumSharePadding, 257);
    expectRejected("cornerRadius", "cornerRadius", kMinimumShareCornerRadius, kMaximumShareCornerRadius, 97);
    expectRejected("borderWidth", "borderWidth", kMinimumShareBorderPixels, kMaximumShareBorderPixels, 9);
    expectRejected("shadowWidth", "shadowWidth", kMinimumShareShadowPixels, kMaximumShareShadowPixels, 65);
    expectRejected("watermarkOpacity", "watermarkOpacity", kMinimumShareWatermarkOpacity, kMaximumShareWatermarkOpacity, 256);

    // 类型不对按坏值拒绝，而不是悄悄落回默认
    QJsonObject wrongType;
    wrongType.insert(QStringLiteral("padding"), QStringLiteral("宽一点"));
    const auto parsed = parseShareStyle(wrongType);
    ASSERT_FALSE(parsed.has_value());
    EXPECT_NE(parsed.error().message.find("padding"), std::string::npos) << parsed.error().message;
    EXPECT_NE(parsed.error().message.find("不是可用的取值"), std::string::npos) << parsed.error().message;

    // 认不出的颜色把原文带回错误里，便于用户对照着改
    QJsonObject badColor;
    badColor.insert(QStringLiteral("backgroundTop"), QStringLiteral("not-a-color"));
    const auto badColorParsed = parseShareStyle(badColor);
    ASSERT_FALSE(badColorParsed.has_value());
    EXPECT_NE(badColorParsed.error().message.find("not-a-color"), std::string::npos) << badColorParsed.error().message;
    EXPECT_NE(badColorParsed.error().message.find("颜色"), std::string::npos) << badColorParsed.error().message;
}

/// 整数键只收整数：小数与超出 int 范围的数都按坏值拒绝，不悄悄截断
TEST(ShareStyle, FractionalAndHugeNumbersAreRejected)
{
    const auto expectRejected = [](const char *key, const QJsonValue &value)
    {
        QJsonObject json;
        json.insert(QString::fromLatin1(key), value);
        const auto parsed = parseShareStyle(json);
        ASSERT_FALSE(parsed.has_value()) << key;
        EXPECT_NE(parsed.error().message.find(key), std::string::npos) << key << ": " << parsed.error().message;
    };
    expectRejected("padding", 47.9);
    expectRejected("cornerRadius", 15.5);
    expectRejected("borderWidth", 1e20);
    expectRejected("shadowWidth", -1e20);
    expectRejected("watermarkOpacity", 3.5);

    // 写成 47.0 的整数值仍是合法整数
    QJsonObject json;
    json.insert(QStringLiteral("padding"), 47.0);
    const auto parsed = parseShareStyle(json);
    ASSERT_TRUE(parsed.has_value()) << parsed.error().message;
    EXPECT_EQ(parsed->paddingPixels, 47);
}

/// 半透明颜色序列化后会悄悄变成不透明，构造样式时直接拒绝
TEST(ShareStyle, TranslucentColorsAreRejected)
{
    const auto expectRejected = [](void (*apply)(ShareStyle &), const char *field)
    {
        ShareStyle style;
        apply(style);
        const auto status = validateShareStyle(style);
        ASSERT_FALSE(status.has_value()) << field;
        EXPECT_NE(status.error().message.find("美化样式"), std::string::npos) << field << ": " << status.error().message;
    };
    expectRejected([](ShareStyle &style) { style.backgroundTop = QColor{0xF5, 0xF6, 0xF8, 128}; }, "backgroundTop");
    expectRejected([](ShareStyle &style) { style.backgroundBottom = QColor{0x65, 0x43, 0x21, 0}; }, "backgroundBottom");
    expectRejected([](ShareStyle &style) { style.borderColor = QColor{0xAA, 0xBB, 0xCC, 254}; }, "borderColor");
    expectRejected([](ShareStyle &style) { style.editorChromeColor = QColor{0x1C, 0x1F, 0x26, 10}; }, "editorChromeColor");
    expectRejected([](ShareStyle &style) { style.editorTitleColor = QColor{0x9A, 0xA4, 0xB2, 200}; }, "editorTitleColor");

    // 解析路径本来就把 alpha 抬回 255：带 alpha 的文本读回来仍是不透明
    QJsonObject json;
    json.insert(QStringLiteral("backgroundTop"), QStringLiteral("#F5F6F880"));
    const auto parsed = parseShareStyle(json);
    ASSERT_TRUE(parsed.has_value()) << parsed.error().message;
    EXPECT_EQ(parsed->backgroundTop.alpha(), 255);
}

/// 坏 JSON 与坏条目只丢自己，不挡整串模板；版本缺失或不认识时整格按空
TEST(ShareStyle, BadJsonAndBadEntriesAreDropped)
{
    // 整串读不懂：坏 JSON、空串、非对象根、非数组 templates
    EXPECT_TRUE(parseShareTemplates(QStringLiteral("{oops")).empty());
    EXPECT_TRUE(parseShareTemplates(QString{}).empty());
    EXPECT_TRUE(parseShareTemplates(QStringLiteral("[1,2,3]")).empty());
    EXPECT_TRUE(parseShareTemplates(QStringLiteral(R"({"version":1,"templates":{}})")).empty());
    EXPECT_TRUE(parseShareTemplates(QStringLiteral(R"({"version":1,"templates":"nope"})")).empty());

    // 版本缺失或不是 1：整格丢弃，不把别的版本硬按 v1 解释
    EXPECT_TRUE(parseShareTemplates(QStringLiteral(R"({"templates":[{"id":"custom-1","name":"我的模板"}]})")).empty());
    EXPECT_TRUE(parseShareTemplates(QStringLiteral(R"({"version":2,"templates":[{"id":"custom-1","name":"我的模板"}]})")).empty());
    EXPECT_TRUE(parseShareTemplates(QStringLiteral(R"({"version":"1","templates":[]})")).empty());

    // 一条好、若干条坏：坏的只丢自己，好的留下
    const auto  good        = makeTemplateEntry(QStringLiteral("custom-good"), QStringLiteral("好模板"));
    const auto  noId        = makeTemplateEntry(QString{}, QStringLiteral("没有 id"));
    const auto  noName      = makeTemplateEntry(QStringLiteral("custom-noname"), QString{});
    const auto  builtinId   = makeTemplateEntry(QStringLiteral("light"), QStringLiteral("冒名模板"));
    const auto  builtinName = makeTemplateEntry(QStringLiteral("custom-light"), GyanisScribe::Base::uiText("留白"));
    auto        badStyle    = makeTemplateEntry(QStringLiteral("custom-badstyle"), QStringLiteral("坏样式"));
    QJsonObject badStyleJson;
    badStyleJson.insert(QStringLiteral("padding"), 257);
    badStyle.insert(QStringLiteral("style"), badStyleJson);

    QJsonArray entries;
    entries.append(good);
    entries.append(QStringLiteral("不是对象"));
    entries.append(noId);
    entries.append(noName);
    entries.append(builtinId);
    entries.append(builtinName);
    entries.append(badStyle);

    const auto parsed = parseShareTemplates(templatesDocument(entries));
    ASSERT_EQ(parsed.size(), 1u);
    EXPECT_EQ(parsed.front().id, QStringLiteral("custom-good"));
    EXPECT_EQ(parsed.front().name, QStringLiteral("好模板"));
    EXPECT_EQ(parsed.front().style, ShareStyle{});

    // 版本 1 的正常目录往返：id、名字、样式逐项读回
    auto custom             = builtInShareTemplates().front();
    custom.id               = QStringLiteral("custom-roundtrip");
    custom.name             = QStringLiteral("往返模板");
    const auto roundTripped = parseShareTemplates(serializeShareTemplates({custom}));
    ASSERT_EQ(roundTripped.size(), 1u);
    EXPECT_EQ(roundTripped.front(), custom);
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
