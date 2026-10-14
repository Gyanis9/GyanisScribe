#include "Core/ExternalCommand.h"

#include "Core/Text.h"

#include <QProcess>

#include <algorithm>

namespace GyanisScribe::Core
{
    Core::Expected<ExternalCommandPlan> planExternalCommand(const QString &commandLine, const QString &filePath)
    {
        const auto placeholder = QString::fromUtf8(kExternalCommandFilePlaceholder.data(), static_cast<qsizetype>(kExternalCommandFilePlaceholder.size()));

        // 拆法只有一套：QProcess::splitCommand 就是 Qt 自己起进程时用的那一份引号规则，
        // 用户在别处学会的写法在这里照样成立
        const auto pieces = QProcess::splitCommand(QStringView{commandLine});
        if (pieces.isEmpty())
        {
            return std::unexpected(
                    Error{.code = ErrorCode::InvalidArgument, .message = uiTextUtf8("外部命令是空的：填成「程序名 参数1 参数2」这样一串，只写占位符 %file% 不算一条命令")});
        }
        if (pieces.front().contains(placeholder))
        {
            return std::unexpected(Error{.code    = ErrorCode::InvalidArgument,
                                         .message = uiFormat(uiTextUtf8("外部命令的第一个词「{}」里带着 %file%：%file% 是成品路径，只能出现在参数里。程序名要写在最前面"),
                                                             pieces.front().toStdString())});
        }

        const auto wantsFile = std::ranges::any_of(pieces, [&](const QString &piece) { return piece.contains(placeholder); });
        if (wantsFile && filePath.isEmpty())
        {
            // 不静默把占位符换成空串：那样跑出去的是「少了一个参数」的命令，外部程序会按自己的方式报错，
            // 用户查到这里来只看到「截图后什么都没发生」
            return std::unexpected(Error{.code    = ErrorCode::InvalidArgument,
                                         .message = uiTextUtf8("这条外部命令要 %file%，可这一轮截图没有落成文件。把设置的「截完之后」选成「直接存成文件」或「快速保存」，"
                                                               "或者删掉命令里的 %file%")});
        }

        ExternalCommandPlan plan{.program = pieces.front()};
        for (auto piece: pieces.mid(1))
        {
            // 整串替换而不是只认独立一段：`--path=%file%` 这种连写也要认
            piece.replace(placeholder, filePath);
            plan.arguments << piece;
        }
        return plan;
    }

    Core::Expected<void> startExternalCommand(const ExternalCommandPlan &plan)
    {
        // startDetached 只保证「交出去了」：外部程序自己跑得怎样不由这一层负责。
        // 等它结束就等于把截图这条路卡在别人的脚本上（上传脚本常常要几十秒）
        if (QProcess::startDetached(plan.program, plan.arguments))
        {
            return {};
        }
        return std::unexpected(
                Error{.code    = ErrorCode::DependencyMissing,
                      .message = uiFormat(uiTextUtf8("没能起跑外部命令：「{}」开不起来。检查程序名与路径是否还在，路径带空格时要把整段用引号包起来"), plan.program.toStdString())});
    }

} // namespace GyanisScribe::Core
