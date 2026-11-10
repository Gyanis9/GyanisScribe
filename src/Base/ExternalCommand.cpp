#include "Base/ExternalCommand.h"

#include "Base/Text.h"

#include <QProcess>

#include <algorithm>

namespace GyanisScribe::Base
{
    Base::Expected<ExternalCommandPlan> planExternalCommand(const QString &commandLine, const QString &filePath)
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
                                         .message = uiFormat(uiTextUtf8("外部命令的第一个词「{}」里带着 %file%：%file% 是输出文件路径，只能出现在参数里。程序名要写在最前面"),
                                                             pieces.front().toStdString())});
        }

        const auto wantsFile = std::ranges::any_of(pieces, [&](const QString &piece) { return piece.contains(placeholder); });
        if (wantsFile && filePath.isEmpty())
        {
            // 不静默把占位符换成空串：那样跑出去的是「少了一个参数」的命令，外部程序会按自己的方式报错，
            // 用户查到这里来只看到「截图后什么都没发生」
            return std::unexpected(Error{.code    = ErrorCode::InvalidArgument,
                                         .message = uiTextUtf8("该外部命令需要 %file%，但本次截图未生成文件。请将设置中的「截图完成后」改为「保存到文件」或「保存并复制」，"
                                                               "或删除命令中的 %file%。")});
        }

        ExternalCommandPlan plan{.program = pieces.front(), .arguments = {}};
        for (auto piece: pieces.mid(1))
        {
            // 整串替换而不是只认独立一段：`--path=%file%` 这种连写也要认
            piece.replace(placeholder, filePath);
            plan.arguments << piece;
        }
        return plan;
    }

    Base::Expected<void> startExternalCommand(const ExternalCommandPlan &plan)
    {
        // startDetached 只保证「交出去了」：外部程序自己跑得怎样不由这一层负责。
        // 等它结束就等于把截图这条路卡在别人的脚本上（上传脚本常常要几十秒）
        if (QProcess::startDetached(plan.program, plan.arguments))
        {
            return {};
        }
        return std::unexpected(
                Error{.code    = ErrorCode::DependencyMissing,
                      .message = uiFormat(uiTextUtf8("无法执行外部命令：「{}」启动失败。请检查程序名与路径是否存在；路径包含空格时，请用引号包裹完整命令。"), plan.program.toStdString())});
    }

} // namespace GyanisScribe::Base
