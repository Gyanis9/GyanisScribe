#include "Core/BugReport/BugReport.h"

#include "Base/Text.h"

#include <QFileInfo>
#include <QStringList>

#include <algorithm>
#include <cmath>
#include <system_error>

namespace
{
    using GyanisScribe::Base::uiFormat;
    using GyanisScribe::Base::uiText;
    using GyanisScribe::Base::uiTextUtf8;
    namespace BugReport = GyanisScribe::Core::BugReport;

    QString normalizedText(const QString &text)
    {
        QString normalized = text;
        normalized.replace(QStringLiteral("\r\n"), QStringLiteral("\n"));
        normalized.replace(QLatin1Char('\r'), QLatin1Char('\n'));
        return normalized.trimmed();
    }

    QString normalizedInlineText(const QString &text)
    {
        QString normalized = normalizedText(text);
        normalized.replace(QLatin1Char('\n'), QLatin1Char(' '));
        return normalized.simplified();
    }

    QString normalizedTitle(const QString &title)
    {
        const auto normalized = normalizedInlineText(title);
        return normalized.isEmpty() ? uiText("截图问题反馈") : normalized;
    }

    QString filledOrPlaceholder(const QString &text)
    {
        const auto normalized = normalizedText(text);
        return normalized.isEmpty() ? uiText("（未填写）") : normalized;
    }

    QString systemLine(const BugReport::BugReportEnvironment &environment)
    {
        const bool hasVersion = !environment.osVersion.isEmpty();
        const bool hasArch    = !environment.architecture.isEmpty();

        // 整行都是可翻译模板：分隔符与括号跟着语言走，英文档下不会混进中文逗号
        if (environment.osName.isEmpty())
        {
            if (hasVersion && hasArch)
            {
                return uiText("系统：内核 %1，%2").arg(environment.osVersion, environment.architecture);
            }
            if (hasVersion)
            {
                return uiText("系统：%1").arg(uiText("内核 %1").arg(environment.osVersion));
            }
            if (hasArch)
            {
                return uiText("系统：%1").arg(environment.architecture);
            }
            return {};
        }
        if (hasVersion && hasArch)
        {
            return uiText("系统：%1（内核 %2，%3）").arg(environment.osName, environment.osVersion, environment.architecture);
        }
        if (hasVersion)
        {
            return uiText("系统：%1（内核 %2）").arg(environment.osName, environment.osVersion);
        }
        if (hasArch)
        {
            return uiText("系统：%1（%2）").arg(environment.osName, environment.architecture);
        }
        return uiText("系统：%1").arg(environment.osName);
    }

    QString sizeLine(const BugReport::BugReportEnvironment &environment)
    {
        if (environment.logicalSize.isEmpty() && environment.physicalSize.isEmpty())
        {
            return {};
        }
        const auto dpr = QString::number(environment.devicePixelRatio, 'f', 2);
        if (!environment.logicalSize.isEmpty() && !environment.physicalSize.isEmpty() && environment.devicePixelRatio > 0.0)
        {
            return uiText("截图尺寸：%1 × %2 逻辑像素（%3 × %4 像素，缩放 %5）")
                    .arg(QString::number(environment.logicalSize.width()), QString::number(environment.logicalSize.height()), QString::number(environment.physicalSize.width()),
                         QString::number(environment.physicalSize.height()), dpr);
        }
        if (!environment.logicalSize.isEmpty())
        {
            return uiText("截图尺寸：%1 × %2 逻辑像素").arg(QString::number(environment.logicalSize.width()), QString::number(environment.logicalSize.height()));
        }
        return uiText("截图尺寸：%1 × %2 像素").arg(QString::number(environment.physicalSize.width()), QString::number(environment.physicalSize.height()));
    }

    QString redactionSummary(const BugReport::BugReportRedaction &redaction)
    {
        using BugReport::RedactionState;
        switch (redaction.state)
        {
            case RedactionState::Pending:
                return uiText("正在检查。");
            case RedactionState::Masked:
                return uiText("已对 %1 处敏感信息打码（%2）。请自行检查后再分享。").arg(redaction.maskedCount).arg(redaction.toolLabel);
            case RedactionState::NoHits:
                return uiText("未检出可打码的敏感信息。请自行检查后再分享。");
            case RedactionState::Unavailable:
                return uiText("不可用（%1）。本次不保存截图，请自行检查文字内容后再分享。").arg(redaction.unavailableReason);
        }
        return {};
    }
} // namespace

namespace GyanisScribe::Core::BugReport
{
    QString buildBugReportMarkdown(const BugReportEnvironment &environment, const BugReportFields &fields, const BugReportRedaction &redaction, const BugReportOptions &options,
                                   const QString &screenshotFileName)
    {
        QStringList lines;
        lines << QStringLiteral("# ") + normalizedTitle(fields.title) << QString{};

        if (options.includeSystem)
        {
            QStringList environmentLines;
            const auto  appParts = QStringList{} << environment.appName << environment.appVersion;
            const auto  app      = appParts.join(QStringLiteral(" ")).trimmed();
            if (!app.isEmpty())
            {
                environmentLines << QStringLiteral("- ") + uiText("应用：%1").arg(app);
            }
            if (const auto system = systemLine(environment); !system.isEmpty())
            {
                environmentLines << QStringLiteral("- ") + system;
            }
            if (!environment.qtVersion.isEmpty())
            {
                environmentLines << QStringLiteral("- ") + uiText("Qt：%1").arg(environment.qtVersion);
            }
            if (!environment.screenName.isEmpty())
            {
                environmentLines << QStringLiteral("- ") + uiText("屏幕：%1").arg(environment.screenName);
            }
            if (const auto size = sizeLine(environment); !size.isEmpty())
            {
                environmentLines << QStringLiteral("- ") + size;
            }
            if (environment.capturedAt.isValid())
            {
                environmentLines << QStringLiteral("- ") + uiText("截图时间：%1").arg(formatBugReportTime(environment.capturedAt));
            }
            if (!environmentLines.isEmpty())
            {
                lines << uiText("## 环境");
                lines += environmentLines;
                lines << QString{};
            }
        }

        if (options.includeWindow)
        {
            QStringList windowLines;
            // 窗口标题来自别的进程，可能带换行：折成单行，否则会把 Markdown 的列表项断成两行
            if (const auto windowTitle = normalizedInlineText(environment.windowTitle); !windowTitle.isEmpty())
            {
                windowLines << QStringLiteral("- ") + uiText("前台窗口（截图时）：%1").arg(windowTitle);
            }
            if (!environment.processName.isEmpty())
            {
                windowLines << QStringLiteral("- ") + uiText("进程：%1").arg(environment.processName);
            }
            if (!windowLines.isEmpty())
            {
                lines << uiText("## 窗口");
                lines += windowLines;
                lines << QString{};
            }
        }

        lines << uiText("## 复现步骤") << filledOrPlaceholder(fields.steps) << QString{};
        lines << uiText("## 期望结果") << filledOrPlaceholder(fields.expected) << QString{};
        lines << uiText("## 实际结果") << filledOrPlaceholder(fields.actual) << QString{};

        const auto notes = normalizedText(fields.notes);
        if (!notes.isEmpty())
        {
            lines << uiText("## 补充说明") << notes << QString{};
        }

        lines << uiText("## 截图");
        if (screenshotFileName.isEmpty())
        {
            lines << uiText("截图：未附带（本段只含文字）。");
        } else
        {
            lines << uiText("![截图](<%1>)").arg(screenshotFileName);
        }
        lines << QString{};
        lines << uiText("自动打码：%1").arg(redactionSummary(redaction));
        return lines.join(QLatin1Char('\n')) + QLatin1Char('\n');
    }

    QString formatBugReportTime(const QDateTime &dateTime)
    {
        if (!dateTime.isValid())
        {
            return {};
        }
        const int  offsetSeconds = dateTime.offsetFromUtc();
        const int  absolute      = std::abs(offsetSeconds);
        const int  hours         = absolute / 3600;
        const int  minutes       = (absolute % 3600) / 60;
        const auto sign          = offsetSeconds < 0 ? QLatin1Char('-') : QLatin1Char('+');
        return dateTime.toString(QStringLiteral("yyyy-MM-dd HH:mm:ss")) +
               QStringLiteral(" UTC%1%2:%3").arg(sign).arg(hours, 2, 10, QLatin1Char('0')).arg(minutes, 2, 10, QLatin1Char('0'));
    }

    std::filesystem::path bugReportDefaultPath(const std::filesystem::path &directory, const QDateTime &now)
    {
        const auto stamp = now.toString(QStringLiteral("yyyyMMdd-HHmmss"));
        for (int index = 1;; ++index)
        {
            const auto suffix = index == 1 ? QString{} : QStringLiteral("-%1").arg(index);
            auto       path   = directory / (QStringLiteral("bug-report-%1%2.md").arg(stamp, suffix)).toStdWString();
            auto       png    = path;
            png.replace_extension(QStringLiteral(".png").toStdWString());
            std::error_code ignored;
            if (!std::filesystem::exists(path, ignored) && !std::filesystem::exists(png, ignored))
            {
                return path;
            }
        }
    }

    BugReportPaths bugReportPathsForDialog(const std::filesystem::path &chosenPath)
    {
        auto       markdown = chosenPath;
        const auto suffix   = QString::fromStdWString(markdown.extension().wstring()).toLower();
        if (suffix != QStringLiteral(".md") && suffix != QStringLiteral(".markdown"))
        {
            markdown.replace_extension(QStringLiteral(".md").toStdWString());
        }
        auto screenshot = markdown;
        screenshot.replace_extension(QStringLiteral(".png").toStdWString());
        return BugReportPaths{markdown, screenshot};
    }

    QSize bugReportDialogInitialSize(const QSize availableArea)
    {
        // 1000×760 是左预览右表单并排的版面下限；可用区域更小时先让尺寸，最低让到 640×480。
        // 低于它表单会挤成读不出的一列，这时宁可让用户自己拖窗
        constexpr int kTargetWidth  = 1000;
        constexpr int kTargetHeight = 760;
        constexpr int kMargin       = 48;
        constexpr int kFloorWidth   = 640;
        constexpr int kFloorHeight  = 480;
        const int     width         = qMin(kTargetWidth, qMax(kFloorWidth, availableArea.width() - kMargin * 2));
        const int     height        = qMin(kTargetHeight, qMax(kFloorHeight, availableArea.height() - kMargin * 2));
        return QSize{width, height};
    }
} // namespace GyanisScribe::Core::BugReport
