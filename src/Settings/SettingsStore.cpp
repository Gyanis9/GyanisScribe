#include "Settings/SettingsStore.h"

#include "Core/Logger.h"
#include "Core/Text.h"

#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>
#include <QSettings>
#include <QString>
#include <QVariant>

#include <algorithm>
#include <cctype>
#include <charconv>
#include <format>
#include <system_error>

namespace GyanisScribe::Settings
{
    namespace
    {

        QString toKey(std::string_view key)
        {
            // 按「指针 + 长度」构造，不依赖结尾零符：键名来自常量表，但走同一条路径才不用分两种情况
            return QString::fromUtf8(key.data(), static_cast<qsizetype>(key.size()));
        }

        std::string trimmedToLower(std::string_view text)
        {
            const auto begin = text.find_first_not_of(" \t\r\n");
            if (begin == std::string_view::npos)
            {
                return {};
            }
            const auto  end = text.find_last_not_of(" \t\r\n");
            std::string lowered(text.substr(begin, end - begin + 1));
            std::ranges::transform(lowered, lowered.begin(), [](unsigned char character) { return static_cast<char>(std::tolower(character)); });
            return lowered;
        }

        bool parseStrictInteger(std::string_view text, int &result)
        {
            const auto begin = text.find_first_not_of(" \t");
            const auto end   = text.find_last_not_of(" \t");
            if (begin == std::string_view::npos)
            {
                return false;
            }
            const auto digits = text.substr(begin, end - begin + 1);

            int        parsed     = 0;
            auto       pointer    = digits.data();
            const auto stop       = digits.data() + digits.size();
            const auto conversion = std::from_chars(pointer, stop, parsed);
            // 整串必须被消费完：允许 "12abc" 取到 12 就是把坏值当成好值，配置写错了谁也看不见
            if (conversion.ec != std::errc{} || conversion.ptr != stop)
            {
                return false;
            }
            result = parsed;
            return true;
        }

        bool parseStrictBoolean(std::string_view text, bool &result)
        {
            const auto normalized = trimmedToLower(text);
            if (normalized == "true" || normalized == "1" || normalized == "yes")
            {
                result = true;
                return true;
            }
            if (normalized == "false" || normalized == "0" || normalized == "no")
            {
                result = false;
                return true;
            }
            // 词表之外没有「按真处理」这条路：QVariant::toBool() 会把任何非空文本变成真，那是静默变形
            return false;
        }

        QString storageFailureReason(std::string_view key)
        {
            return QString::fromUtf8(
                    GyanisScribe::Core::uiFormat(GyanisScribe::Core::uiTextUtf8("配置项 {} 存不进去：后端返回了错误状态，请检查应用配置目录是否可写"), key).c_str());
        }
    } // namespace

    SettingsStore::SettingsStore(QObject *parent) : SettingsStore(std::make_unique<QSettings>(), parent)
    {
    }

    SettingsStore::SettingsStore(const std::filesystem::path &iniFile, QObject *parent) :
        SettingsStore(std::make_unique<QSettings>(QString::fromStdWString(iniFile.wstring()), QSettings::IniFormat), parent)
    {
    }

    SettingsStore::SettingsStore(std::unique_ptr<QSettings> settings, QObject *parent) : ISettingsStore(parent), m_settings(std::move(settings))
    {
    }

    SettingsStore::~SettingsStore()
    {
        // 析构统一冲刷：写操作不留盘是为了让设置页能连续输入而不每次敲盘
        m_settings->sync();
    }

    QString SettingsStore::stringValue(std::string_view key, const QString &defaultValue) const
    {
        const auto stored = m_settings->value(toKey(key));
        if (!stored.isValid())
        {
            return defaultValue;
        }

        const auto text = stored.toString();
        if (text.trimmed().isEmpty())
        {
            // 空文本按「没值」处理而不是原样交回：一个空的截图目录会被下游当成合法路径，
            // 到写文件那一刻才炸。与整数·布尔同一口径——坏值记一条 Warning 再回落
            Core::Logger::warning(std::format("配置项 {} 存的是空文本，按默认值处理；要恢复默认就把这一项删掉", std::string_view(key)));
            return defaultValue;
        }
        return text;
    }

    int SettingsStore::intValue(std::string_view key, int defaultValue) const
    {
        const auto stored = m_settings->value(toKey(key));
        if (!stored.isValid())
        {
            return defaultValue;
        }
        if (stored.typeId() == QMetaType::Int)
        {
            return stored.toInt();
        }

        int parsed = 0;
        if (parseStrictInteger(stored.toString().toStdString(), parsed))
        {
            return parsed;
        }

        // 坏值记一条 Warning 后回落默认：既不截断也不取整，但也不让一个键把整条启动流程带崩
        Core::Logger::warning(std::format("配置项 {} 存的值「{}」不是十进制整数，按默认值 {} 继续", std::string_view(key), stored.toString().toStdString(), defaultValue));
        return defaultValue;
    }

    bool SettingsStore::boolValue(std::string_view key, bool defaultValue) const
    {
        const auto stored = m_settings->value(toKey(key));
        if (!stored.isValid())
        {
            return defaultValue;
        }
        if (stored.typeId() == QMetaType::Bool)
        {
            return stored.toBool();
        }

        bool parsed = false;
        if (parseStrictBoolean(stored.toString().toStdString(), parsed))
        {
            return parsed;
        }

        Core::Logger::warning(std::format("配置项 {} 存的值「{}」不在布尔词表（true/false/1/0/yes/no）里，按默认值 {} 继续", std::string_view(key), stored.toString().toStdString(),
                                          defaultValue));
        return defaultValue;
    }

    QStringList SettingsStore::stringListValue(std::string_view key, const QStringList &defaultValue) const
    {
        const auto stored = m_settings->value(toKey(key));
        if (!stored.isValid())
        {
            return defaultValue;
        }
        if (stored.typeId() == QMetaType::QStringList)
        {
            return stored.toStringList();
        }

        // 列表被写成了单个文本（手工编辑 INI 很常见）：按一项交回去，并留下线索
        Core::Logger::warning(std::format("配置项 {} 存的不是列表，按单元素列表处理，原文「{}」", std::string_view(key), stored.toString().toStdString()));
        return {stored.toString()};
    }

    Core::Expected<void> SettingsStore::setValue(std::string_view key, const QVariant &value)
    {
        if (key.empty())
        {
            return std::unexpected(
                    Core::Error{Core::ErrorCode::InvalidArgument, GyanisScribe::Core::uiTextUtf8("写入配置项失败：键名为空。请传「分组/大驼峰」形式的键，例如 Paths/Screenshot")});
        }

        m_settings->setValue(toKey(key), value);
        if (m_settings->status() == QSettings::AccessError)
        {
            return std::unexpected(Core::Error{Core::ErrorCode::FileWriteFailed, storageFailureReason(key).toStdString()});
        }

        // 通知发在状态检查之后：失败时不发，订阅者不会为一个没落地的值去重载
        emit valueChanged(toKey(key));
        return {};
    }

    void SettingsStore::sync()
    {
        m_settings->sync();
        if (m_settings->status() == QSettings::AccessError)
        {
            Core::Logger::warning("配置落盘失败：后端返回访问错误，本次改动可能没写进去，请检查配置目录权限");
        }
    }

    QStringList SettingsStore::allKeys() const
    {
        return m_settings->allKeys();
    }

    Core::Expected<void> SettingsStore::exportToFile(const std::filesystem::path &filePath) const
    {
        QJsonObject root;
        for (const auto &key: m_settings->allKeys())
        {
            const auto stored = m_settings->value(key);
            if (stored.typeId() == QMetaType::QStringList)
            {
                root.insert(key, QJsonArray::fromStringList(stored.toStringList()));
            } else
            {
                // 数值也导成文本：注册表分不出 int 与 string，而 INI 只有文本，取两者都装得下的形状
                root.insert(key, stored.toString());
            }
        }

        QSaveFile output(QString::fromStdWString(filePath.wstring()));
        if (!output.open(QIODevice::WriteOnly))
        {
            return std::unexpected(Core::Error{Core::ErrorCode::FileWriteFailed,
                                               GyanisScribe::Core::uiFormat(GyanisScribe::Core::uiTextUtf8("配置导出文件打不开：{}（{}）。请换一个可写的目录再试"),
                                                                            filePath.string(), output.errorString().toStdString())});
        }
        output.write(QJsonDocument(root).toJson(QJsonDocument::Indented));
        if (!output.commit())
        {
            return std::unexpected(
                    Core::Error{Core::ErrorCode::FileWriteFailed, GyanisScribe::Core::uiFormat(GyanisScribe::Core::uiTextUtf8("配置导出没落盘：{}（{}）。请检查磁盘空间与目录权限"),
                                                                                               filePath.string(), output.errorString().toStdString())});
        }
        return {};
    }

    Core::Expected<int> SettingsStore::importFromFile(const std::filesystem::path &filePath)
    {
        QFile input(QString::fromStdWString(filePath.wstring()));
        if (!input.open(QIODevice::ReadOnly))
        {
            return std::unexpected(
                    Core::Error{Core::ErrorCode::InvalidArgument, GyanisScribe::Core::uiFormat(GyanisScribe::Core::uiTextUtf8("配置导入文件读不到：{}（{}）。请确认路径与文件名"),
                                                                                               filePath.string(), input.errorString().toStdString())});
        }

        QJsonParseError parseError{};
        const auto      document = QJsonDocument::fromJson(input.readAll(), &parseError);
        if (parseError.error != QJsonParseError::NoError || !document.isObject())
        {
            return std::unexpected(
                    Core::Error{Core::ErrorCode::InvalidArgument,
                                GyanisScribe::Core::uiFormat(GyanisScribe::Core::uiTextUtf8("配置导入文件不是一个 JSON 对象：{}（{}）。请导出本程序生成的那份再导入"),
                                                             filePath.string(), parseError.errorString().toStdString())});
        }

        const auto root     = document.object();
        int        imported = 0;
        for (const auto &key: root.keys())
        {
            const QJsonValue value = root.value(key);
            QVariant         stored;
            if (value.isArray())
            {
                // QJsonArray 只给逐元素的 QJsonValue，没有一把转 QStringList 的入口（Qt 6.11 实测）
                QStringList items;
                for (const auto &element: value.toArray())
                {
                    items << element.toString();
                }
                stored = items;
            } else
            {
                // 手写的外部文件可能放数字或布尔：一律按文本收进后端（注册表与 INI 都装得下），
                // 而不是照原类型存进去，导致两个后端读到不同的东西
                stored = value.toVariant().toString();
            }
            if (const auto written = setValue(key.toStdString(), stored); !written)
            {
                return std::unexpected(written.error());
            }
            ++imported;
        }
        return imported;
    }

} // namespace GyanisScribe::Settings
