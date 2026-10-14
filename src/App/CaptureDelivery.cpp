#include "App/CaptureDelivery.h"

#include "Core/Text.h"

#include <algorithm>

namespace GyanisScribe::App
{
    namespace
    {
        /// 表里的那一格（找不到返回词表长度）：排序与「认不认得」都按表序判
        [[nodiscard]] std::size_t choiceIndexOf(const CaptureOutput output) noexcept
        {
            const auto found = std::ranges::find(kCaptureOutputChoices, output, &CaptureOutputChoice::output);
            return static_cast<std::size_t>(std::ranges::distance(kCaptureOutputChoices.begin(), found));
        }

        /// token 的 QString 形状：命令行上是 UTF-8 串，比对按整段来
        [[nodiscard]] QString tokenText(const std::string_view token)
        {
            return QString::fromUtf8(token.data(), static_cast<qsizetype>(token.size()));
        }
    } // namespace

    CaptureOutputSet defaultCaptureOutputs()
    {
        return {CaptureOutput::File, CaptureOutput::Clipboard};
    }

    Core::Expected<CaptureOutputSet> parseCaptureOutputs(const QString &text)
    {
        if (text.trimmed().isEmpty())
        {
            return std::unexpected(Core::Error{.code    = Core::ErrorCode::InvalidArgument,
                                               .message = Core::uiFormat(Core::uiTextUtf8("至少要写一处目的地：可取的只有 {}"), captureOutputTokenList())});
        }

        CaptureOutputSet outputs;
        for (const auto &rawPiece: text.split(QLatin1Char(',')))
        {
            // 只吃段前后的空格：脚本里 --to "file, clipboard" 是常见写法，而取值本身照旧区分大小写
            const auto piece = rawPiece.trimmed();
            if (piece.isEmpty())
            {
                return std::unexpected(
                        Core::Error{.code = Core::ErrorCode::InvalidArgument, .message = Core::uiTextUtf8("逗号两边都要有内容：把多余的逗号去掉（比如结尾那一个）")});
            }

            const auto found = std::ranges::find_if(kCaptureOutputChoices, [&](const CaptureOutputChoice &choice) { return piece == tokenText(choice.token); });
            if (found == kCaptureOutputChoices.end())
            {
                // 不就近取一个：拼错一个字母就当没这一处，脚本会把「少了一路输出」当成程序没跑对
                return std::unexpected(
                        Core::Error{.code    = Core::ErrorCode::InvalidArgument,
                                    .message = Core::uiFormat(Core::uiTextUtf8("目的地「{}」不认识：可取的只有 {}（区分大小写）"), piece.toStdString(), captureOutputTokenList())});
            }
            if (std::ranges::find(outputs, found->output) != outputs.end())
            {
                return std::unexpected(Core::Error{.code    = Core::ErrorCode::InvalidArgument,
                                                   .message = Core::uiFormat(Core::uiTextUtf8("目的地「{}」写了两次：一处写一次就够"), piece.toStdString())});
            }
            outputs.push_back(found->output);
        }

        // 交付顺序只由词表定，不由输入里的先后定：`pin,file` 与 `file,pin` 走的是同一条路
        std::ranges::stable_sort(outputs, [](const CaptureOutput left, const CaptureOutput right) { return choiceIndexOf(left) < choiceIndexOf(right); });
        return outputs;
    }

    bool captureOutputsContain(const CaptureOutputSet &outputs, const CaptureOutput output) noexcept
    {
        return std::ranges::find(outputs, output) != outputs.end();
    }

    std::string captureOutputTokenList()
    {
        std::string joined;
        for (const auto &choice: kCaptureOutputChoices)
        {
            joined += joined.empty() ? "" : " / ";
            joined += choice.token;
        }
        return joined;
    }

} // namespace GyanisScribe::App
