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
        constexpr std::string_view kKeySource    = "sourceFile";
        constexpr std::string_view kKeyBackdrop  = "transparentBackdrop";
        constexpr std::string_view kKeyColorCard = "colorCardHex";

        Core::Expected<QVector<PinState>> failWith(std::string reason)
        {
            return std::unexpected(Core::Error{Core::ErrorCode::InvalidArgument, std::move(reason)});
        }

        /// 名字表那条通道的同一条失败形状：载荷类型不同，不能共用上面那个
        Core::Expected<QMap<int, QString>> failNamesWith(std::string reason)
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
            entry.insert(QLatin1String(kKeySource.data()), state.sourceFile);
            entry.insert(QLatin1String(kKeyBackdrop.data()), static_cast<int>(state.transparentBackdrop));
            entry.insert(QLatin1String(kKeyColorCard.data()), state.colorCardHex);
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

            // 动图来源同样是后加的字段：缺省就是静图（旧布局里放着的本来也都是静图）
            if (entry.contains(QLatin1String(kKeySource.data())))
            {
                state.sourceFile = entry[QLatin1String(kKeySource.data())].toString();
            }

            // 色卡原色同样是后加的字段：缺省就是「不是色卡」（旧布局里那几张本来也没有这一档）
            if (entry.contains(QLatin1String(kKeyColorCard.data())))
            {
                state.colorCardHex = entry[QLatin1String(kKeyColorCard.data())].toString();
            }

            // 透明区同样是后加的字段，缺省落「跟随窗口底色」（旧布局里那几张露的就是窗口底色）；
            // 写了但不是整数、或不在档数里，才是坏记录——越界的数字静默当成第一档，等于把用户选的棋盘偷偷换掉
            if (entry.contains(QLatin1String(kKeyBackdrop.data())))
            {
                const auto stored = entry[QLatin1String(kKeyBackdrop.data())];
                if (!stored.isDouble())
                {
                    return failWith(
                            GyanisScribe::Core::uiFormat(GyanisScribe::Core::uiTextUtf8("贴图布局第 {} 项的透明区档位不是整数。请清除这一项后让程序重新记一次"), index + 1));
                }
                const auto raw = stored.toInt();
                if (raw < 0 || raw >= kTransparentBackdropCount)
                {
                    return failWith(GyanisScribe::Core::uiFormat(GyanisScribe::Core::uiTextUtf8("贴图布局第 {} 项的透明区档位不在 0 到 {} 里：{}。请清除这一项后让程序重新记一次"),
                                                                 index + 1, kTransparentBackdropCount - 1, raw));
                }
                state.transparentBackdrop = static_cast<TransparentBackdrop>(raw);
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

    QString serializeGroupNames(const QMap<int, QString> &names)
    {
        QJsonObject entries;
        for (auto iterator = names.constBegin(); iterator != names.constEnd(); ++iterator)
        {
            // 空名就是「没起过名字」：写进去只会在下次读回来时多一条没人认领的键
            if (iterator.value().trimmed().isEmpty())
            {
                continue;
            }
            entries.insert(QString::number(iterator.key()), iterator.value().trimmed());
        }
        return QString::fromUtf8(QJsonDocument(entries).toJson(QJsonDocument::Compact));
    }

    Core::Expected<QMap<int, QString>> parseGroupNames(std::string_view text)
    {
        QMap<int, QString> names;
        if (text.empty())
        {
            // 没存过与一组都没起过名字是同一件事：交回空表，界面按「分组 N」显示每一组
            return names;
        }

        QJsonParseError parseError{};
        const auto      document = QJsonDocument::fromJson(QByteArray(text.data(), static_cast<qsizetype>(text.size())), &parseError);
        if (parseError.error != QJsonParseError::NoError || !document.isObject())
        {
            return failNamesWith(
                    GyanisScribe::Core::uiFormat(GyanisScribe::Core::uiTextUtf8("贴图分组名字不是一段 JSON 对象：{}。请在设置里清除 Pin/GroupNames 这一项，让程序重新记一次"),
                                                 parseError.errorString().toStdString()));
        }

        const auto entries = document.object();
        for (auto iterator = entries.constBegin(); iterator != entries.constEnd(); ++iterator)
        {
            bool       ok    = false;
            const auto group = iterator.key().toInt(&ok);
            if (!ok || group < kFirstPinGroup)
            {
                return failNamesWith(GyanisScribe::Core::uiFormat(
                        GyanisScribe::Core::uiTextUtf8("贴图分组名字里有一条的键不是正整数：{}。请在设置里清除 Pin/GroupNames 这一项，让程序重新记一次"),
                        iterator.key().toStdString()));
            }
            if (!iterator.value().isString())
            {
                return failNamesWith(GyanisScribe::Core::uiFormat(
                        GyanisScribe::Core::uiTextUtf8("贴图分组名字第 {} 组的值不是一段文本。请在设置里清除 Pin/GroupNames 这一项，让程序重新记一次"), group));
            }
            const auto name = iterator.value().toString().trimmed();
            if (name.isEmpty())
            {
                continue; // 与序列化那一侧同一口径：空名不当成名字
            }
            names.insert(group, name);
        }
        return names;
    }

    Core::Expected<void> saveGroupNames(Settings::ISettingsStore &settings, const QMap<int, QString> &names)
    {
        return settings.setValue(Settings::kPinGroupNames, QVariant(serializeGroupNames(names)));
    }

    Core::Expected<QMap<int, QString>> loadGroupNames(const Settings::ISettingsStore &settings)
    {
        const auto stored = settings.stringValue(Settings::kPinGroupNames, QString());
        return parseGroupNames(stored.toStdString());
    }

    Core::Expected<QVector<int>> parseGroupRotation(const QStringList &stored)
    {
        QVector<int> order;
        order.reserve(stored.size());
        for (const auto &token: stored)
        {
            const auto trimmed = token.trimmed();
            if (trimmed.isEmpty())
            {
                // 逗号连写与首尾的空格：多出来的是分隔符而不是坏值，跳过它才谈得上「这一格能用」
                continue;
            }

            bool       numeric = false;
            const auto group   = trimmed.toInt(&numeric);
            const auto bad     = [&trimmed](const QString &why)
            {
                return Core::Expected<QVector<int>>{std::unexpected{Core::Error{
                        .code    = Core::ErrorCode::InvalidArgument,
                        .message = GyanisScribe::Core::uiFormat(GyanisScribe::Core::uiTextUtf8("分组轮换顺序里 \"{}\" {}。请在设置的通用页改这一格，或整格清空回到按组号升序"),
                                                                trimmed.toStdString(), why.toStdString()),
                }}};
            };
            if (!numeric || group < kFirstPinGroup)
            {
                return bad(QStringLiteral("不是一个正整数"));
            }
            if (order.contains(group))
            {
                return bad(QStringLiteral("出现了两次：同一个组在一圈里走两次，切组就会在同一处多跳一步"));
            }
            order.push_back(group);
        }
        return order;
    }

    QVector<int> rotationCycle(const QVector<int> &order, const QVector<int> &groupsInUse)
    {
        QVector<int> cycle;
        cycle.reserve(groupsInUse.size());
        for (const auto group: order)
        {
            // 只列在册的组：名单里留着早已并掉的组号，切组就会停在一组不存在的贴图前
            if (groupsInUse.contains(group) && !cycle.contains(group))
            {
                cycle.push_back(group);
            }
        }
        // 没被点名的组按升序补在末尾：新建的那一组立刻就能切到，不必先回设置里补一笔
        for (const auto group: groupsInUse)
        {
            if (!cycle.contains(group))
            {
                cycle.push_back(group);
            }
        }
        return cycle;
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
