#include "Pin/PinLayout.h"

#include "Core/Text.h"
#include "Pin/PinTransform.h"
#include "Settings/ISettingsStore.h"
#include "Settings/SettingsKeys.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

#include <format>
#include <string>

namespace GyanisScribe::Pin
{
    namespace
    {
        // 单个对象的字段名集中在这里，序列化与解析读同一组常量，才不会一个写成 scale 另一个写成 Scale
        constexpr std::string_view kKeyPositionX = "x";
        constexpr std::string_view kKeyPositionY = "y";
        constexpr std::string_view kKeyWidth     = "width";
        constexpr std::string_view kKeyHeight    = "height";
        constexpr std::string_view kKeyScale     = "scale";
        constexpr std::string_view kKeyRotation  = "rotation";
        constexpr std::string_view kKeyOpacity   = "opacity";
        constexpr std::string_view kKeyImageFile = "imageFile";
        constexpr std::string_view kKeyGroup     = "group";
        constexpr std::string_view kKeyOnTop     = "alwaysOnTop";
        constexpr std::string_view kKeyThrough   = "mouseThrough";

        Core::Expected<QVector<PinState>> failWith(std::string reason)
        {
            return std::unexpected(Core::Error{Core::ErrorCode::InvalidArgument, std::move(reason)});
        }
    } // namespace

    QString serializeLayout(const QVector<PinState> &states)
    {
        QJsonArray entries;
        for (const auto &state: states)
        {
            QJsonObject entry;
            entry.insert(QLatin1String(kKeyPositionX.data()), state.position.x());
            entry.insert(QLatin1String(kKeyPositionY.data()), state.position.y());
            entry.insert(QLatin1String(kKeyWidth.data()), state.logicalSize.width());
            entry.insert(QLatin1String(kKeyHeight.data()), state.logicalSize.height());
            entry.insert(QLatin1String(kKeyScale.data()), clampScale(state.scale));
            entry.insert(QLatin1String(kKeyRotation.data()), normalizeDegrees(state.rotationDegrees));
            entry.insert(QLatin1String(kKeyOpacity.data()), state.opacity);
            entry.insert(QLatin1String(kKeyImageFile.data()), state.imageFile);
            entry.insert(QLatin1String(kKeyGroup.data()), state.group);
            entry.insert(QLatin1String(kKeyOnTop.data()), state.alwaysOnTop);
            entry.insert(QLatin1String(kKeyThrough.data()), state.mouseThrough);
            entries.append(entry);
        }
        return QString::fromUtf8(QJsonDocument(entries).toJson(QJsonDocument::Compact));
    }

    Core::Expected<QVector<PinState>> parseLayout(std::string_view text)
    {
        if (text.empty())
        {
            // 没存过与存了个空列表是同一件事：交回空列表，让调用方按「没有要恢复的」处理
            return QVector<PinState>{};
        }

        QJsonParseError parseError{};
        const auto      document = QJsonDocument::fromJson(QByteArray(text.data(), static_cast<qsizetype>(text.size())), &parseError);
        if (parseError.error != QJsonParseError::NoError || !document.isArray())
        {
            return failWith(GyanisScribe::Core::uiFormat(GyanisScribe::Core::uiTextUtf8("贴图布局不是一段 JSON 数组：{}。请在设置里清除这一项，让程序重新记一次"),
                                                         parseError.errorString().toStdString()));
        }

        QVector<PinState> states;
        const auto        entries = document.array();
        for (int index = 0; index < entries.size(); ++index)
        {
            if (!entries[index].isObject())
            {
                // 一项坏就整体失败：跳过它等于悄悄少恢复一张贴图，用户会以为那张被删了
                return failWith(GyanisScribe::Core::uiFormat(GyanisScribe::Core::uiTextUtf8("贴图布局第 {} 项不是对象。请清除这一项后让程序重新记一次"), index + 1));
            }

            const auto entry = entries[index].toObject();
            for (std::string_view key: {kKeyPositionX, kKeyPositionY, kKeyWidth, kKeyHeight, kKeyScale, kKeyRotation, kKeyOpacity, kKeyImageFile})
            {
                if (!entry.contains(QLatin1String(key.data())))
                {
                    return failWith(GyanisScribe::Core::uiFormat(GyanisScribe::Core::uiTextUtf8("贴图布局第 {} 项缺少字段 {}。请清除这一项后让程序重新记一次"), index + 1,
                                                                 std::string(key)));
                }
            }

            PinState state;
            state.position        = QPoint(entry[QLatin1String(kKeyPositionX.data())].toInt(), entry[QLatin1String(kKeyPositionY.data())].toInt());
            state.logicalSize     = QSizeF(entry[QLatin1String(kKeyWidth.data())].toDouble(), entry[QLatin1String(kKeyHeight.data())].toDouble());
            state.scale           = entry[QLatin1String(kKeyScale.data())].toDouble();
            state.rotationDegrees = entry[QLatin1String(kKeyRotation.data())].toDouble();
            state.opacity         = entry[QLatin1String(kKeyOpacity.data())].toDouble();
            state.imageFile       = entry[QLatin1String(kKeyImageFile.data())].toString();

            if (state.imageFile.isEmpty())
            {
                // 像素进不了配置，路径就是恢复的唯一凭据：空路径那张图无论如何也摆不回去
                return failWith(GyanisScribe::Core::uiFormat(GyanisScribe::Core::uiTextUtf8("贴图布局第 {} 项没有图片位置。请清除这一项后让程序重新记一次"), index + 1));
            }

            if (state.logicalSize.width() <= 0.0 || state.logicalSize.height() <= 0.0)
            {
                return failWith(GyanisScribe::Core::uiFormat(GyanisScribe::Core::uiTextUtf8("贴图布局第 {} 项的尺寸不是正数（{}×{}）。请清除这一项后让程序重新记一次"), index + 1,
                                                             state.logicalSize.width(), state.logicalSize.height()));
            }
            if (state.opacity <= 0.0 || state.opacity > 1.0)
            {
                return failWith(GyanisScribe::Core::uiFormat(GyanisScribe::Core::uiTextUtf8("贴图布局第 {} 项的不透明度不在 (0,1] 里：{}。请清除这一项后让程序重新记一次"),
                                                             index + 1, state.opacity));
            }

            // group 是后加的字段，必须容忍缺省：升级后第一次启动要摆回上一版存下的那几张，
            // 缺字段就整项失败等于把用户的桌面清空一次。缺省落第 1 组；写了但不合法才是坏记录
            if (entry.contains(QLatin1String(kKeyGroup.data())))
            {
                const auto group = entry[QLatin1String(kKeyGroup.data())].toInt();
                if (group < kFirstPinGroup)
                {
                    return failWith(GyanisScribe::Core::uiFormat(GyanisScribe::Core::uiTextUtf8("贴图布局第 {} 项的分组号不是正整数：{}。请清除这一项后让程序重新记一次"),
                                                                 index + 1, group));
                }
                state.group = group;
            }

            // 置顶与穿透同样是后加的字段、同样容忍缺省（旧布局按今天的新建行为：置顶、不穿透）；
            // 写了但不是布尔值才是坏记录——toBool() 会把任意内容都折成 false，静默读反一个开关
            if (entry.contains(QLatin1String(kKeyOnTop.data())))
            {
                if (!entry[QLatin1String(kKeyOnTop.data())].isBool())
                {
                    return failWith(
                            GyanisScribe::Core::uiFormat(GyanisScribe::Core::uiTextUtf8("贴图布局第 {} 项的置顶字段不是布尔值。请清除这一项后让程序重新记一次"), index + 1));
                }
                state.alwaysOnTop = entry[QLatin1String(kKeyOnTop.data())].toBool();
            }
            if (entry.contains(QLatin1String(kKeyThrough.data())))
            {
                if (!entry[QLatin1String(kKeyThrough.data())].isBool())
                {
                    return failWith(
                            GyanisScribe::Core::uiFormat(GyanisScribe::Core::uiTextUtf8("贴图布局第 {} 项的穿透字段不是布尔值。请清除这一项后让程序重新记一次"), index + 1));
                }
                state.mouseThrough = entry[QLatin1String(kKeyThrough.data())].toBool();
            }

            states.push_back(state);
        }
        return states;
    }

    Core::Expected<void> saveLayout(Settings::ISettingsStore &settings, const QVector<PinState> &states)
    {
        return settings.setValue(Settings::kPinLayout, QVariant(serializeLayout(states)));
    }

    Core::Expected<QVector<PinState>> loadLayout(const Settings::ISettingsStore &settings)
    {
        const auto stored = settings.stringValue(Settings::kPinLayout, QString());
        return parseLayout(stored.toStdString());
    }

    Core::Expected<double> parseOpacityPercentSetting(const int percent)
    {
        if (percent >= 1 && percent <= 100)
        {
            return static_cast<double>(percent) / 100.0;
        }
        return Core::Expected<double>{std::unexpected{Core::Error{
                .code    = Core::ErrorCode::InvalidArgument,
                .message = GyanisScribe::Core::uiFormat(
                        GyanisScribe::Core::uiTextUtf8("贴图默认不透明度 \"{}\" 不在 1 到 100 之间：请在设置的贴图页重选一个值，或删掉 Pin/DefaultOpacity 这一项回到完全不透明"),
                        percent),
        }}};
    }

} // namespace GyanisScribe::Pin
