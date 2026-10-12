#include "App/CommandLine.h"

#include "Core/Text.h"

namespace
{
    using GyanisScribe::App::CommandLineAction;

    /// 动作的说法，报冲突时点名用
    [[nodiscard]] QString actionName(const CommandLineAction action)
    {
        switch (action)
        {
            case CommandLineAction::SelfTest:
                return QStringLiteral("--selftest");
            case CommandLineAction::CaptureFullScreen:
            case CommandLineAction::CaptureLastRegion:
            case CommandLineAction::CaptureWindow:
                return QStringLiteral("--capture");
            case CommandLineAction::PinFile:
                return QStringLiteral("--pin");
            case CommandLineAction::ColorPick:
                return QStringLiteral("--pick");
            case CommandLineAction::Help:
                return QStringLiteral("--help");
            case CommandLineAction::Version:
                return QStringLiteral("--version");
            case CommandLineAction::None:
                return {};
        }
        return {};
    }

    /**
     * @brief 认一个「带取值的参数」
     * @param argument 当前参数
     * @param arguments 全部参数
     * @param index 当前下标，会在「空格分家」的写法下前进一格
     * @param name 参数名，如 --capture
     * @param value 取到的值；两种写法都没给值时留空
     * @return 参数名对上了返回 true（值可能为空，由调用方报「要带取值」）
     */
    [[nodiscard]] bool matchWithValue(const QString &argument, const QStringList &arguments, int &index, const QString &name, QString &value)
    {
        const auto prefix = name + QLatin1Char('=');
        if (argument.startsWith(prefix))
        {
            value = argument.mid(prefix.size());
            return true;
        }
        if (argument == name)
        {
            if (index + 1 < arguments.size())
            {
                ++index;
                value = arguments[index];
            }
            return true;
        }
        return false;
    }
} // namespace

namespace GyanisScribe::App
{

    CommandLineParseResult parseStartupArguments(const QStringList &arguments)
    {
        CommandLineParseResult result;
        if (arguments.isEmpty())
        {
            return result; // None：正常启动
        }

        // 帮助与版本先认：出现在哪儿都直接生效，其余参数不再看——用户要的就是这份说明
        if (arguments.contains(QStringLiteral("-h")) || arguments.contains(QStringLiteral("--help")))
        {
            result.request.action = CommandLineAction::Help;
            return result;
        }
        if (arguments.contains(QStringLiteral("-v")) || arguments.contains(QStringLiteral("--version")))
        {
            result.request.action = CommandLineAction::Version;
            return result;
        }

        // 请求先在本地攒着：中途报错时不留半个结果（文档里的承诺由这一处保证）
        CommandLineRequest request;
        for (int index = 0; index < arguments.size(); ++index)
        {
            const auto       &argument = arguments[index];
            CommandLineAction action   = CommandLineAction::None;
            QString           value;

            if (argument == QStringLiteral("--selftest"))
            {
                action = CommandLineAction::SelfTest;
            } else if (argument == QStringLiteral("--pick"))
            {
                action = CommandLineAction::ColorPick;
            } else if (argument == QStringLiteral("--capture") || argument.startsWith(QStringLiteral("--capture=")))
            {
                if (!matchWithValue(argument, arguments, index, QStringLiteral("--capture"), value) || value.isEmpty())
                {
                    result.errorText = Core::uiText("--capture 要带一个范围取值：full、region 或 window");
                    return result;
                }
                for (const auto &choice: kCommandLineCaptureChoices)
                {
                    if (value == QString::fromUtf8(choice.token.data(), static_cast<qsizetype>(choice.token.size())))
                    {
                        action = choice.action;
                        break;
                    }
                }
                if (action == CommandLineAction::None)
                {
                    // 区分大小写，且只认表里那三个：拼错一个字母就当没这条参数，不就近取一个
                    result.errorText = Core::uiText("--capture 的取值「%1」不认识：只能是 full、region 或 window（区分大小写）").arg(value);
                    return result;
                }
            } else if (argument == QStringLiteral("--pin") || argument.startsWith(QStringLiteral("--pin=")))
            {
                if (!matchWithValue(argument, arguments, index, QStringLiteral("--pin"), value) || value.isEmpty())
                {
                    result.errorText = Core::uiText("--pin 要带一个图片文件路径");
                    return result;
                }
                action           = CommandLineAction::PinFile;
                request.filePath = value;
            } else
            {
                result.errorText = Core::uiText("不认识的参数：%1。可用参数见 --help").arg(argument);
                return result;
            }

            // 一次只执行一个动作：同时给了两个就报错，不替用户挑一个
            if (request.action != CommandLineAction::None)
            {
                result.errorText = Core::uiText("一次只能给一个动作：%1 与 %2 只能选一个").arg(actionName(request.action), actionName(action));
                return result;
            }
            request.action = action;
        }

        result.request = request;
        return result;
    }

    QString commandLineUsageText()
    {
        QString captureLines;
        for (const auto &choice: kCommandLineCaptureChoices)
        {
            const auto token = QString::fromUtf8(choice.token.data(), static_cast<qsizetype>(choice.token.size()));
            captureLines += Core::uiText("    --capture=%1       %2\n").arg(token, Core::uiText(choice.description.data()));
        }

        return Core::uiText("智录：截图、标注、贴图、取色、录屏与文字识别。不带参数启动即常驻（托盘 + 全局热键）。\n"
                            "\n"
                            "用法：GyanisScribe [参数]\n"
                            "  -h, --help         打这份说明后退出\n"
                            "  -v, --version      打版本号后退出\n"
                            "  --selftest         把启动路径跑完就退出：热键登记与日志就绪算通过（退出码 0），否则 1\n"
                            "  --capture=<范围>   截一张并直接存成文件（同时进剪贴板），把文件路径打到标准输出后退出：\n") +
               captureLines +
               Core::uiText("  --pin <文件>       把盘上的图片贴到屏幕上（保持运行）\n"
                            "  --pick             进入屏幕取色（保持运行）\n");
    }

} // namespace GyanisScribe::App
