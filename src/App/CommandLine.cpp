#include "App/CommandLine.h"

#include "Base/Text.h"

namespace
{
    using GyanisScribe::App::CommandLineAction;

    /// 这三个动作才有「截完之后落到哪儿」这一步：--to 与 --exec 只配它们用
    [[nodiscard]] bool isCaptureAction(const CommandLineAction action) noexcept
    {
        return action == CommandLineAction::CaptureFullScreen || action == CommandLineAction::CaptureLastRegion || action == CommandLineAction::CaptureWindow;
    }

    /// 报错文案里列出可取的去处，出处只有 App/CaptureDelivery.h 那张词表
    [[nodiscard]] QString outputTokenListText()
    {
        return QString::fromStdString(GyanisScribe::App::captureOutputTokenList());
    }

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
        bool               execGiven = false;
        for (int index = 0; index < arguments.size(); ++index)
        {
            const auto       &argument = arguments[index];
            CommandLineAction action   = CommandLineAction::None;
            QString           value;

            // --to 与 --exec 是「修饰」而不是动作：它们改的是这一次截图的交付方式，认完就接着看下一个参数
            QString optionValue;
            if (matchWithValue(argument, arguments, index, QStringLiteral("--to"), optionValue))
            {
                if (request.outputsGiven)
                {
                    result.errorText = Base::uiText("--to 只给一次：把要落的几处去处写进同一串，用逗号分开");
                    return result;
                }
                if (optionValue.trimmed().isEmpty())
                {
                    result.errorText = Base::uiText("--to 要带至少一处目的地：可取的只有 %1").arg(outputTokenListText());
                    return result;
                }
                const auto parsed = GyanisScribe::App::parseCaptureOutputs(optionValue);
                if (!parsed)
                {
                    result.errorText = QString::fromUtf8(parsed.error().message.c_str());
                    return result;
                }
                request.outputs      = *parsed;
                request.outputsGiven = true;
                continue;
            }
            if (matchWithValue(argument, arguments, index, QStringLiteral("--exec"), optionValue))
            {
                if (execGiven)
                {
                    result.errorText = Base::uiText("--exec 只给一次：一条命令行用引号包住整串，参数照旧写在里面");
                    return result;
                }
                if (optionValue.trimmed().isEmpty())
                {
                    result.errorText = Base::uiText("--exec 要带一条命令：形如 --exec=\"上传工具 --path %file%\"，参数里的 %file% 会换成成品路径");
                    return result;
                }
                request.execCommand = optionValue;
                execGiven           = true;
                continue;
            }

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
                    result.errorText = Base::uiText("--capture 要带一个范围取值：full、region 或 window");
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
                    result.errorText = Base::uiText("--capture 的取值「%1」不认识：只能是 full、region 或 window（区分大小写）").arg(value);
                    return result;
                }
            } else if (argument == QStringLiteral("--pin") || argument.startsWith(QStringLiteral("--pin=")))
            {
                if (!matchWithValue(argument, arguments, index, QStringLiteral("--pin"), value) || value.isEmpty())
                {
                    result.errorText = Base::uiText("--pin 要带一个图片文件路径");
                    return result;
                }
                action           = CommandLineAction::PinFile;
                request.filePath = value;
            } else
            {
                result.errorText = Base::uiText("不认识的参数：%1。可用参数见 --help").arg(argument);
                return result;
            }

            // 一次只执行一个动作：同时给了两个就报错，不替用户挑一个
            if (request.action != CommandLineAction::None)
            {
                result.errorText = Base::uiText("一次只能给一个动作：%1 与 %2 只能选一个").arg(actionName(request.action), actionName(action));
                return result;
            }
            request.action = action;
        }

        // --to 与 --exec 描述的是「截完之后」，别的动作没有这一步：悄悄收下会让脚本以为生效了
        if (!isCaptureAction(request.action) && (request.outputsGiven || execGiven))
        {
            result.errorText = Base::uiText("--to 与 --exec 只配 --capture 用：--pin 与 --pick 没有「截完之后落到哪儿」这一步");
            return result;
        }
        // 没点名时仍是早先那两处（落盘 + 剪贴板）：默认值写在这一处，旧脚本的输出形状不会因为加了开关而变
        if (isCaptureAction(request.action) && !request.outputsGiven)
        {
            request.outputs = GyanisScribe::App::defaultCaptureOutputs();
        }

        result.request = std::move(request);
        return result;
    }

    QString commandLineUsageText()
    {
        QString captureLines;
        for (const auto &choice: kCommandLineCaptureChoices)
        {
            const auto token = QString::fromUtf8(choice.token.data(), static_cast<qsizetype>(choice.token.size()));
            captureLines += Base::uiText("    --capture=%1       %2\n").arg(token, Base::uiText(choice.description));
        }

        return Base::uiText("智录：截图、标注、贴图、取色、录屏与文字识别。不带参数启动即常驻（托盘 + 全局热键）。\n"
                            "\n"
                            "用法：GyanisScribe [参数]\n"
                            "  -h, --help         打这份说明后退出\n"
                            "  -v, --version      打版本号后退出\n"
                            "  --selftest         把启动路径跑完就退出：热键登记与日志就绪算通过（退出码 0），否则 1\n"
                            "  --capture=<范围>   截一张，默认存成文件并复制进剪贴板，把文件路径打到标准输出后退出：\n") +
               captureLines +
               Base::uiText("  --to=<去处>       只配 --capture：这一次落哪几处，逗号分隔，可取 file / clipboard / pin\n"
                            "                    交付顺序恒为 file → clipboard → pin；点了 pin 就留在桌面上不退出（同 --pin）\n"
                            "  --exec=<命令>      只配 --capture：截完之后起跑一条外部命令，参数里的 %file% 换成成品路径\n"
                            "                    整串用引号包住；没点 file 时这一轮没有成品，带 %file% 的命令会被拒\n"
                            "  --pin <文件>       把盘上的图片贴到屏幕上（保持运行）\n"
                            "  --pick             进入屏幕取色（保持运行）\n");
    }

} // namespace GyanisScribe::App
