#include "Core/Share/ShareStyle.h"

#include "Base/Logger.h"
#include "Base/Text.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonParseError>
#include <QJsonValue>
#include <QUuid>

#include <array>
#include <cmath>
#include <cstddef>
#include <limits>
#include <optional>
#include <string>
#include <string_view>
#include <utility>

namespace GyanisScribe::Core
{
    namespace
    {
        // 样式对象的键名集中在这里，写入与读回共用同一组常量，才不会一边写 padding 另一边读 Padding
        constexpr std::string_view kKeyBackgroundTop     = "backgroundTop";
        constexpr std::string_view kKeyBackgroundBottom  = "backgroundBottom";
        constexpr std::string_view kKeyGradient          = "gradient";
        constexpr std::string_view kKeyPadding           = "padding";
        constexpr std::string_view kKeyCornerRadius      = "cornerRadius";
        constexpr std::string_view kKeyBorder            = "border";
        constexpr std::string_view kKeyBorderWidth       = "borderWidth";
        constexpr std::string_view kKeyBorderColor       = "borderColor";
        constexpr std::string_view kKeyShadow            = "shadow";
        constexpr std::string_view kKeyShadowWidth       = "shadowWidth";
        constexpr std::string_view kKeyEditorChrome      = "editorChrome";
        constexpr std::string_view kKeyEditorTitle       = "editorTitle";
        constexpr std::string_view kKeyEditorChromeColor = "editorChromeColor";
        constexpr std::string_view kKeyEditorTitleColor  = "editorTitleColor";
        constexpr std::string_view kKeyWatermark         = "watermark";
        constexpr std::string_view kKeyWatermarkOpacity  = "watermarkOpacity";
        constexpr std::string_view kKeyWatermarkCorner   = "watermarkCorner";

        // 模板目录那一层的键名
        constexpr std::string_view kKeyVersion   = "version";
        constexpr std::string_view kKeyTemplates = "templates";
        constexpr std::string_view kKeyId        = "id";
        constexpr std::string_view kKeyName      = "name";
        constexpr std::string_view kKeyStyle     = "style";

        constexpr std::string_view kCornerBottomRight = "bottomRight";
        constexpr std::string_view kCornerBottomLeft  = "bottomLeft";

        /// 内置模板的固定 id 与顺序，与 builtInShareTemplates 的产出同序
        constexpr std::array<std::string_view, 4> kBuiltInTemplateIds = {"light", "dark", "gradient", "code"};

        /// 内置模板名称的源码写法；查表时再按当前语言翻译
        constexpr std::array<const char *, 4> kBuiltInTemplateNameSources = {"留白", "聚焦", "渐变", "代码窗口"};

        /// 自定义模板名的上限
        constexpr qsizetype kMaximumShareTemplateNameLength = 40;

        /// 键名是有长度的视图，转 QLatin1StringView 时按 (指针, 长度) 收，不假设它过 0 结尾
        constexpr QLatin1StringView keyText(const std::string_view key)
        {
            return {key.data(), static_cast<qsizetype>(key.size())};
        }

        [[nodiscard]] Base::Error invalidArgument(std::string message)
        {
            return Base::Error{Base::ErrorCode::InvalidArgument, std::move(message)};
        }

        /// 越界拒绝的同一句话：各字段只差键名与范围，文案口径要一致
        [[nodiscard]] Base::Error outOfRangeError(const std::string_view field, const int minimum, const int maximum)
        {
            return invalidArgument(Base::uiFormat(Base::uiTextUtf8("美化样式的 {} 不在 {} 到 {} 之间：请重选一个值，或回到默认模板"), std::string{field}, minimum, maximum));
        }

        /// 类型不对或取值不在词表里：JSON 里写了别的东西，按坏值拒绝而不是悄悄落回默认
        [[nodiscard]] Base::Error badValueError(const std::string_view field)
        {
            return invalidArgument(Base::uiFormat(Base::uiTextUtf8("美化样式的 {} 不是可用的取值：请重选一个值，或回到默认模板"), std::string{field}));
        }

        [[nodiscard]] Base::Error colorError(const QString &text)
        {
            return invalidArgument(Base::uiFormat(Base::uiTextUtf8("美化样式的颜色 {} 认不出来（如 #RRGGBB）：请重选一个值，或回到默认模板"), text.toStdString()));
        }

        /// 内置名称的两种写法：当前语言一份、源码中文一份。英文档存下的中文名与中文档存下的英文名
        /// 都不该冒充内置模板，否则内置项与自定义项会并排出现两个同名条目
        [[nodiscard]] bool isBuiltInShareTemplateName(const QString &name)
        {
            for (const char *source: kBuiltInTemplateNameSources)
            {
                if (name.compare(Base::uiText(source), Qt::CaseInsensitive) == 0)
                {
                    return true;
                }
                if (name.compare(QString::fromUtf8(source), Qt::CaseInsensitive) == 0)
                {
                    return true;
                }
            }
            return false;
        }

        /// 合法颜色按 #rrggbb 写；坏颜色写成空串，读回时按坏值拒绝而不是悄悄落成黑色
        [[nodiscard]] QString colorText(const QColor &color)
        {
            return color.isValid() ? color.name(QColor::HexRgb) : QString{};
        }

        [[nodiscard]] std::optional<Base::Error> readInt(const QJsonObject &json, const std::string_view key, int &target)
        {
            const auto value = json.value(keyText(key));
            if (value.isUndefined())
            {
                return std::nullopt;
            }
            if (!value.isDouble())
            {
                return badValueError(key);
            }
            // JSON 的数只有浮点形态：47.9 直接转 int 会被悄悄截成 47，超出 int 范围的会撞上转换的默认值，
            // 两种都按坏值拒绝，不接受一个和存进去不一样的数
            const auto number = value.toDouble();
            if (number != std::trunc(number) || number < static_cast<double>(std::numeric_limits<int>::min()) || number > static_cast<double>(std::numeric_limits<int>::max()))
            {
                return badValueError(key);
            }
            target = static_cast<int>(number);
            return std::nullopt;
        }

        [[nodiscard]] std::optional<Base::Error> readBool(const QJsonObject &json, const std::string_view key, bool &target)
        {
            const auto value = json.value(keyText(key));
            if (value.isUndefined())
            {
                return std::nullopt;
            }
            if (!value.isBool())
            {
                return badValueError(key);
            }
            target = value.toBool();
            return std::nullopt;
        }

        [[nodiscard]] std::optional<Base::Error> readString(const QJsonObject &json, const std::string_view key, QString &target)
        {
            const auto value = json.value(keyText(key));
            if (value.isUndefined())
            {
                return std::nullopt;
            }
            if (!value.isString())
            {
                return badValueError(key);
            }
            target = value.toString();
            return std::nullopt;
        }

        [[nodiscard]] std::optional<Base::Error> readColor(const QJsonObject &json, const std::string_view key, QColor &target)
        {
            const auto value = json.value(keyText(key));
            if (value.isUndefined())
            {
                return std::nullopt;
            }
            if (!value.isString())
            {
                return badValueError(key);
            }
            const auto text = value.toString();
            if (!QColor::isValidColorName(text))
            {
                return colorError(text);
            }
            QColor color{text};
            // 美化成品总是不透明底：JPG 那一路因此不需要额外垫白，存回来的 alpha 也一律按 255 处理
            color.setAlpha(255);
            target = color;
            return std::nullopt;
        }

        [[nodiscard]] std::optional<Base::Error> readWatermarkCorner(const QJsonObject &json, ShareWatermarkCorner &target)
        {
            const auto value = json.value(keyText(kKeyWatermarkCorner));
            if (value.isUndefined())
            {
                return std::nullopt;
            }
            if (!value.isString())
            {
                return badValueError(kKeyWatermarkCorner);
            }
            const auto text = value.toString();
            if (text == QLatin1StringView{kCornerBottomRight.data(), static_cast<qsizetype>(kCornerBottomRight.size())})
            {
                target = ShareWatermarkCorner::BottomRight;
                return std::nullopt;
            }
            if (text == QLatin1StringView{kCornerBottomLeft.data(), static_cast<qsizetype>(kCornerBottomLeft.size())})
            {
                target = ShareWatermarkCorner::BottomLeft;
                return std::nullopt;
            }
            return badValueError(kKeyWatermarkCorner);
        }
    } // namespace

    std::vector<ShareTemplate> builtInShareTemplates()
    {
        auto light = defaultShareTemplate();
        // 「留白」的 1px 浅描边是这套模板的外观之一：只改描边色而不打开开关，浅底上就看不出边界
        light.style.border = true;

        auto dark                   = defaultShareTemplate();
        dark.id                     = QStringLiteral("dark");
        dark.name                   = Base::uiText("聚焦");
        dark.style.backgroundTop    = QColor{0x1E, 0x1F, 0x22};
        dark.style.backgroundBottom = dark.style.backgroundTop;
        dark.style.border           = true;
        dark.style.borderColor      = QColor{0x3A, 0x3D, 0x42};
        dark.style.shadowPixels     = 40;

        auto gradient                     = defaultShareTemplate();
        gradient.id                       = QStringLiteral("gradient");
        gradient.name                     = Base::uiText("渐变");
        gradient.style.backgroundTop      = QColor{0x6D, 0x5E, 0xF8};
        gradient.style.backgroundBottom   = QColor{0x3B, 0x82, 0xF6};
        gradient.style.gradient           = true;
        gradient.style.paddingPixels      = 56;
        gradient.style.cornerRadiusPixels = 20;
        gradient.style.border             = false;
        gradient.style.shadowPixels       = 40;

        auto code                     = defaultShareTemplate();
        code.id                       = QStringLiteral("code");
        code.name                     = Base::uiText("代码窗口");
        code.style.backgroundTop      = QColor{0x0F, 0x11, 0x15};
        code.style.backgroundBottom   = code.style.backgroundTop;
        code.style.paddingPixels      = 40;
        code.style.cornerRadiusPixels = 12;
        code.style.border             = false;
        code.style.shadowPixels       = 40;
        code.style.editorChrome       = true;
        code.style.editorChromeColor  = QColor{0x1C, 0x1F, 0x26};
        code.style.editorTitleColor   = QColor{0x9A, 0xA4, 0xB2};

        return {light, dark, gradient, code};
    }

    ShareTemplate defaultShareTemplate()
    {
        return {QStringLiteral("light"), Base::uiText("留白"), ShareStyle{}};
    }

    bool isBuiltInShareTemplateId(const QString &id)
    {
        for (const auto builtInId: kBuiltInTemplateIds)
        {
            if (id == QLatin1StringView{builtInId.data(), static_cast<qsizetype>(builtInId.size())})
            {
                return true;
            }
        }
        return false;
    }

    Base::Expected<void> validateShareStyle(const ShareStyle &style)
    {
        const auto requireRange = [](const int value, const int minimum, const int maximum, const std::string_view field) -> std::optional<Base::Error>
        {
            if (value >= minimum && value <= maximum)
            {
                return std::nullopt;
            }
            return outOfRangeError(field, minimum, maximum);
        };

        if (const auto error = requireRange(style.paddingPixels, kMinimumSharePadding, kMaximumSharePadding, kKeyPadding))
        {
            return std::unexpected(*error);
        }
        if (const auto error = requireRange(style.cornerRadiusPixels, kMinimumShareCornerRadius, kMaximumShareCornerRadius, kKeyCornerRadius))
        {
            return std::unexpected(*error);
        }
        if (const auto error = requireRange(style.borderPixels, kMinimumShareBorderPixels, kMaximumShareBorderPixels, kKeyBorderWidth))
        {
            return std::unexpected(*error);
        }
        if (const auto error = requireRange(style.shadowPixels, kMinimumShareShadowPixels, kMaximumShareShadowPixels, kKeyShadowWidth))
        {
            return std::unexpected(*error);
        }
        if (const auto error = requireRange(style.watermarkOpacity, kMinimumShareWatermarkOpacity, kMaximumShareWatermarkOpacity, kKeyWatermarkOpacity))
        {
            return std::unexpected(*error);
        }

        if (style.editorTitle.size() > static_cast<qsizetype>(kMaximumShareEditorTitleLength))
        {
            return std::unexpected(invalidArgument(Base::uiFormat(Base::uiTextUtf8("美化样式的标题最多 {} 个字符"), kMaximumShareEditorTitleLength)));
        }
        if (style.watermarkText.size() > static_cast<qsizetype>(kMaximumShareWatermarkLength))
        {
            return std::unexpected(invalidArgument(Base::uiFormat(Base::uiTextUtf8("美化样式的水印最多 {} 个字符"), kMaximumShareWatermarkLength)));
        }

        const std::array<std::pair<std::string_view, const QColor *>, 5> colors{{
                {kKeyBackgroundTop, &style.backgroundTop},
                {kKeyBackgroundBottom, &style.backgroundBottom},
                {kKeyBorderColor, &style.borderColor},
                {kKeyEditorChromeColor, &style.editorChromeColor},
                {kKeyEditorTitleColor, &style.editorTitleColor},
        }};
        for (const auto &[field, color]: colors)
        {
            const auto fieldText = QString::fromLatin1(field.data(), static_cast<qsizetype>(field.size()));
            if (!color->isValid())
            {
                return std::unexpected(colorError(fieldText));
            }
            // 序列化只写 #rrggbb：半透明颜色存下去就变成不透明，读回来与眼前所见对不上，构造时先拒绝
            if (color->alpha() != 255)
            {
                return std::unexpected(colorError(colorText(*color)));
            }
        }

        return {};
    }

    Base::Expected<ShareStyle> parseShareStyle(const QJsonObject &json)
    {
        ShareStyle style;
        if (const auto error = readColor(json, kKeyBackgroundTop, style.backgroundTop))
        {
            return std::unexpected(*error);
        }
        if (const auto error = readColor(json, kKeyBackgroundBottom, style.backgroundBottom))
        {
            return std::unexpected(*error);
        }
        if (const auto error = readBool(json, kKeyGradient, style.gradient))
        {
            return std::unexpected(*error);
        }
        if (const auto error = readInt(json, kKeyPadding, style.paddingPixels))
        {
            return std::unexpected(*error);
        }
        if (const auto error = readInt(json, kKeyCornerRadius, style.cornerRadiusPixels))
        {
            return std::unexpected(*error);
        }
        if (const auto error = readBool(json, kKeyBorder, style.border))
        {
            return std::unexpected(*error);
        }
        if (const auto error = readInt(json, kKeyBorderWidth, style.borderPixels))
        {
            return std::unexpected(*error);
        }
        if (const auto error = readColor(json, kKeyBorderColor, style.borderColor))
        {
            return std::unexpected(*error);
        }
        if (const auto error = readBool(json, kKeyShadow, style.shadow))
        {
            return std::unexpected(*error);
        }
        if (const auto error = readInt(json, kKeyShadowWidth, style.shadowPixels))
        {
            return std::unexpected(*error);
        }
        if (const auto error = readBool(json, kKeyEditorChrome, style.editorChrome))
        {
            return std::unexpected(*error);
        }
        if (const auto error = readString(json, kKeyEditorTitle, style.editorTitle))
        {
            return std::unexpected(*error);
        }
        if (const auto error = readColor(json, kKeyEditorChromeColor, style.editorChromeColor))
        {
            return std::unexpected(*error);
        }
        if (const auto error = readColor(json, kKeyEditorTitleColor, style.editorTitleColor))
        {
            return std::unexpected(*error);
        }
        if (const auto error = readString(json, kKeyWatermark, style.watermarkText))
        {
            return std::unexpected(*error);
        }
        if (const auto error = readInt(json, kKeyWatermarkOpacity, style.watermarkOpacity))
        {
            return std::unexpected(*error);
        }
        if (const auto error = readWatermarkCorner(json, style.watermarkCorner))
        {
            return std::unexpected(*error);
        }

        // 读回来的数还得过同一道范围与长度校验：JSON 是手改得到的，字段读成整数不等于值可用
        if (const auto status = validateShareStyle(style); !status)
        {
            return std::unexpected(status.error());
        }
        return style;
    }

    QJsonObject shareStyleToJson(const ShareStyle &style)
    {
        QJsonObject json;
        json.insert(keyText(kKeyBackgroundTop), colorText(style.backgroundTop));
        json.insert(keyText(kKeyBackgroundBottom), colorText(style.backgroundBottom));
        json.insert(keyText(kKeyGradient), style.gradient);
        json.insert(keyText(kKeyPadding), style.paddingPixels);
        json.insert(keyText(kKeyCornerRadius), style.cornerRadiusPixels);
        json.insert(keyText(kKeyBorder), style.border);
        json.insert(keyText(kKeyBorderWidth), style.borderPixels);
        json.insert(keyText(kKeyBorderColor), colorText(style.borderColor));
        json.insert(keyText(kKeyShadow), style.shadow);
        json.insert(keyText(kKeyShadowWidth), style.shadowPixels);
        json.insert(keyText(kKeyEditorChrome), style.editorChrome);
        json.insert(keyText(kKeyEditorTitle), style.editorTitle);
        json.insert(keyText(kKeyEditorChromeColor), colorText(style.editorChromeColor));
        json.insert(keyText(kKeyEditorTitleColor), colorText(style.editorTitleColor));
        json.insert(keyText(kKeyWatermark), style.watermarkText);
        json.insert(keyText(kKeyWatermarkOpacity), style.watermarkOpacity);
        json.insert(keyText(kKeyWatermarkCorner), style.watermarkCorner == ShareWatermarkCorner::BottomLeft
                                                          ? QLatin1StringView{kCornerBottomLeft.data(), static_cast<qsizetype>(kCornerBottomLeft.size())}
                                                          : QLatin1StringView{kCornerBottomRight.data(), static_cast<qsizetype>(kCornerBottomRight.size())});
        return json;
    }

    std::vector<ShareTemplate> parseShareTemplates(const QString &storedJson)
    {
        if (storedJson.isEmpty())
        {
            // 空串是「一套自定义模板都没存过」，不是坏记录，不必记日志
            return {};
        }

        QJsonParseError parseError{};
        const auto      document = QJsonDocument::fromJson(storedJson.toUtf8(), &parseError);
        if (parseError.error != QJsonParseError::NoError)
        {
            Base::Logger::warning(Base::uiFormat(Base::uiTextUtf8("美化模板不是一段合法的 JSON（{}），这一格按空处理"), parseError.errorString().toStdString()));
            return {};
        }
        if (!document.isObject())
        {
            Base::Logger::warning(Base::uiTextUtf8("美化模板的根不是对象，这一格按空处理"));
            return {};
        }

        const auto root = document.object();

        // 写端每次都落 version：缺版本、类型不对、版本不认识都按整格坏数据处理，
        // 不把别的版本的字段硬按 v1 解释
        const auto versionValue = root.value(keyText(kKeyVersion));
        if (!versionValue.isDouble() || versionValue.toDouble() != 1.0)
        {
            const auto shown = versionValue.isDouble() ? QString::number(versionValue.toDouble()) : QStringLiteral("?");
            Base::Logger::warning(Base::uiFormat(Base::uiTextUtf8("美化模板的版本不是 1（{}），这一格按空处理"), shown.toStdString()));
            return {};
        }

        const auto entries = root.value(keyText(kKeyTemplates));
        if (!entries.isArray())
        {
            Base::Logger::warning(Base::uiTextUtf8("美化模板缺少 templates 数组，这一格按空处理"));
            return {};
        }

        std::vector<ShareTemplate> templates;
        const auto                 array = entries.toArray();
        for (qsizetype index = 0; index < array.size(); ++index)
        {
            if (templates.size() >= static_cast<std::size_t>(kMaximumCustomShareTemplates))
            {
                break;
            }

            const auto entryValue = array.at(index);
            if (!entryValue.isObject())
            {
                Base::Logger::warning(Base::uiFormat(Base::uiTextUtf8("美化模板第 {} 项不是对象，已跳过"), index + 1));
                continue;
            }
            const auto entry     = entryValue.toObject();
            const auto idValue   = entry.value(keyText(kKeyId));
            const auto nameValue = entry.value(keyText(kKeyName));
            if (!idValue.isString() || idValue.toString().isEmpty())
            {
                Base::Logger::warning(Base::uiFormat(Base::uiTextUtf8("美化模板第 {} 项没有 id，已跳过"), index + 1));
                continue;
            }
            if (!nameValue.isString() || nameValue.toString().isEmpty())
            {
                Base::Logger::warning(Base::uiFormat(Base::uiTextUtf8("美化模板第 {} 项没有名字，已跳过"), index + 1));
                continue;
            }

            const auto id   = idValue.toString();
            const auto name = nameValue.toString();
            if (isBuiltInShareTemplateId(id))
            {
                Base::Logger::warning(Base::uiFormat(Base::uiTextUtf8("美化模板「{}」用了内置 id，已跳过"), id.toStdString()));
                continue;
            }
            if (isBuiltInShareTemplateName(name))
            {
                Base::Logger::warning(Base::uiFormat(Base::uiTextUtf8("美化模板「{}」与内置模板同名，已跳过"), name.toStdString()));
                continue;
            }

            const auto styleValue = entry.value(keyText(kKeyStyle));
            if (!styleValue.isUndefined() && !styleValue.isObject())
            {
                Base::Logger::warning(Base::uiFormat(Base::uiTextUtf8("美化模板「{}」的风格不是对象，已跳过"), name.toStdString()));
                continue;
            }
            const auto style = parseShareStyle(styleValue.toObject());
            if (!style)
            {
                Base::Logger::warning(Base::uiFormat(Base::uiTextUtf8("美化模板「{}」的风格读不出来（{}），已跳过"), name.toStdString(), style.error().message));
                continue;
            }
            templates.push_back(ShareTemplate{id, name, *style});
        }
        return templates;
    }

    QString serializeShareTemplates(const std::vector<ShareTemplate> &templates)
    {
        QJsonArray entries;
        for (const auto &item: templates)
        {
            QJsonObject entry;
            entry.insert(keyText(kKeyId), item.id);
            entry.insert(keyText(kKeyName), item.name);
            entry.insert(keyText(kKeyStyle), shareStyleToJson(item.style));
            entries.append(entry);
        }

        QJsonObject root;
        root.insert(keyText(kKeyVersion), 1);
        root.insert(keyText(kKeyTemplates), entries);
        return QString::fromUtf8(QJsonDocument{root}.toJson(QJsonDocument::Compact));
    }

    Base::Expected<std::vector<ShareTemplate>> addCustomShareTemplate(const std::vector<ShareTemplate> &templates, const QString &name, const ShareStyle &style)
    {
        const auto trimmedName = name.trimmed();
        if (trimmedName.isEmpty())
        {
            return std::unexpected(invalidArgument(Base::uiTextUtf8("模板名不能为空")));
        }
        if (trimmedName.size() > kMaximumShareTemplateNameLength)
        {
            return std::unexpected(invalidArgument(Base::uiTextUtf8("模板名最多 40 个字符")));
        }

        const auto duplicateError = invalidArgument(Base::uiFormat(Base::uiTextUtf8("已有同名模板「{}」"), trimmedName.toStdString()));
        for (const auto &existing: templates)
        {
            if (existing.name.compare(trimmedName, Qt::CaseInsensitive) == 0)
            {
                return std::unexpected(duplicateError);
            }
        }
        if (isBuiltInShareTemplateName(trimmedName))
        {
            return std::unexpected(duplicateError);
        }

        if (const auto status = validateShareStyle(style); !status)
        {
            return std::unexpected(status.error());
        }
        if (templates.size() >= static_cast<std::size_t>(kMaximumCustomShareTemplates))
        {
            return std::unexpected(invalidArgument(Base::uiTextUtf8("自定义模板最多 30 套，请先删掉几套")));
        }

        auto updated = templates;
        updated.push_back(ShareTemplate{newCustomShareTemplateId(), trimmedName, style});

        // 这一格随设置一起导出与备份，体积要有硬上限：先序列化再量，比按字段估更贴近真正落盘的字节
        if (serializeShareTemplates(updated).toUtf8().size() > static_cast<qsizetype>(kMaximumShareTemplatesJsonBytes))
        {
            return std::unexpected(invalidArgument(Base::uiTextUtf8("模板太多，设置里存不下：请先删掉几套")));
        }
        return updated;
    }

    Base::Expected<std::vector<ShareTemplate>> removeCustomShareTemplate(const std::vector<ShareTemplate> &templates, const QString &id)
    {
        if (isBuiltInShareTemplateId(id))
        {
            return std::unexpected(invalidArgument(Base::uiTextUtf8("内置模板不能删除")));
        }
        for (std::size_t index = 0; index < templates.size(); ++index)
        {
            if (templates[index].id != id)
            {
                continue;
            }
            auto updated = templates;
            updated.erase(updated.begin() + static_cast<std::ptrdiff_t>(index));
            return updated;
        }
        return std::unexpected(invalidArgument(Base::uiTextUtf8("找不到这套模板，可能已经删掉了")));
    }

    QString newCustomShareTemplateId()
    {
        return QStringLiteral("custom-") + QUuid::createUuid().toString(QUuid::WithoutBraces).left(8);
    }

} // namespace GyanisScribe::Core
