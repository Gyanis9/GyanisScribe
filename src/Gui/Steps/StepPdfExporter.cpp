#include "Gui/Steps/StepPdfExporter.h"

#include "Base/Paths.h"
#include "Base/Text.h"
#include "Core/Steps/StepHtmlBuilder.h"
#include "Core/Steps/StepImageMarker.h"

#include <QImage>
#include <QMarginsF>
#include <QPageLayout>
#include <QPageSize>
#include <QPrinter>
#include <QString>
#include <QStringList>
#include <QTextDocument>
#include <QUrl>

#include <algorithm>
#include <system_error>

namespace GyanisScribe::Gui
{
    namespace
    {
        /// 页边距，毫米
        constexpr int kPdfPageMarginMillimeters = 15;

        /// 图片在正文里的显示宽度，CSS 像素；乘打印机 96 到 1200 dpi 的换算后约占 159 毫米
        constexpr int kPdfImageWidth = 600;

        /// 图片显示高度上限，CSS 像素；给标题与说明留出同一页的空间
        constexpr int kPdfImageMaxHeight = 760;

        /// 把 HTML 特殊字符换成实体；& 先换，避免后面换出的实体被二次转义
        [[nodiscard]] QString escapeHtml(const QString &text)
        {
            QString escaped = text;
            escaped.replace(QLatin1Char('&'), QStringLiteral("&amp;"));
            escaped.replace(QLatin1Char('<'), QStringLiteral("&lt;"));
            escaped.replace(QLatin1Char('>'), QStringLiteral("&gt;"));
            escaped.replace(QLatin1Char('"'), QStringLiteral("&quot;"));
            escaped.replace(QLatin1Char('\''), QStringLiteral("&#39;"));
            return escaped;
        }

        /// 空说明只写中性的类型标签，不拿元素名或窗口标题补回来
        [[nodiscard]] QString stepTitle(const Core::RecordedStep &step)
        {
            const QString description = step.description.trimmed();
            if (!description.isEmpty())
            {
                return description;
            }
            switch (step.kind)
            {
                case Core::StepKind::Click:
                    return Base::uiText("点击");
                case Core::StepKind::WindowChange:
                    return Base::uiText("切换窗口");
                case Core::StepKind::Manual:
                    return Core::manualStepDescription();
            }
            return QString();
        }

        /// 每一步的时刻与窗口标题，两段都取不到时返回空串
        [[nodiscard]] QString stepMeta(const Core::RecordedStep &step)
        {
            QStringList parts;
            if (step.at.isValid())
            {
                parts.append(step.at.toString(QStringLiteral("yyyy-MM-dd HH:mm:ss")));
            }
            const QString title = step.windowTitle.trimmed();
            if (!title.isEmpty())
            {
                parts.append(Base::uiText("窗口：%1").arg(escapeHtml(title)));
            }
            return parts.join(QStringLiteral(" · "));
        }

        /// 有图时注册资源并写 img，没有图时给占位
        [[nodiscard]] QString stepImageBlock(QTextDocument &document, const Core::RecordedStep &step, const int displayNumber)
        {
            const QImage annotated = Core::annotatedStepImage(step, displayNumber);
            if (!annotated.isNull())
            {
                QImage printable = annotated;
                if (std::max(printable.width(), printable.height()) > Core::kStepHtmlImageEdge)
                {
                    printable = printable.scaled(Core::kStepHtmlImageEdge, Core::kStepHtmlImageEdge, Qt::KeepAspectRatio, Qt::SmoothTransformation);
                }
                if (!printable.isNull())
                {
                    const QSize displaySize = printable.size().scaled(QSize{kPdfImageWidth, kPdfImageMaxHeight}, Qt::KeepAspectRatio);
                    const QUrl  resourceName{QStringLiteral("step-%1").arg(displayNumber, 3, 10, QLatin1Char('0'))};
                    document.addResource(QTextDocument::ImageResource, resourceName, printable);
                    return QStringLiteral("<p><img src=\"%1\" width=\"%2\" height=\"%3\"></p>").arg(resourceName.toString()).arg(displaySize.width()).arg(displaySize.height());
                }
            }

            const QString reason = step.captureError.trimmed();
            if (!reason.isEmpty())
            {
                return QStringLiteral("<p>%1</p>").arg(escapeHtml(Base::uiText("这一张没截到：%1").arg(reason)));
            }
            return QStringLiteral("<p>%1</p>").arg(escapeHtml(Base::uiText("这一张没截到")));
        }

        /// 按显示顺序拼出整份文档；图片资源同时注册进 document
        [[nodiscard]] QString buildPdfHtml(QTextDocument &document, const std::vector<Core::RecordedStep> &steps, const QDateTime &generatedAt)
        {
            const QString tutorialTitle = escapeHtml(Base::uiText("GyanisScribe 步骤教程"));
            const QString generatedText = generatedAt.isValid() ? generatedAt.toString(QStringLiteral("yyyy-MM-dd HH:mm:ss")) : Base::uiText("未知时间");
            const QString summary       = escapeHtml(Base::uiText("生成于 %1 · 共 %2 步").arg(generatedText).arg(static_cast<int>(steps.size())));

            QString html;
            html.reserve(static_cast<qsizetype>(steps.size()) * 1024 + 1024);
            html.append(QStringLiteral("<h1>%1</h1><p>%2</p>").arg(tutorialTitle, summary));

            for (std::size_t position = 0; position < steps.size(); ++position)
            {
                const Core::RecordedStep &step          = steps[position];
                const int                 displayNumber = static_cast<int>(position) + 1;
                // 每步之间只留间距，正文按 A4 可用高度流式续排，不强行每步新开一页
                html.append(QStringLiteral("<h2 style=\"margin-top: 16px;\">%1. %2</h2>").arg(displayNumber).arg(escapeHtml(stepTitle(step))));

                const QString meta = stepMeta(step);
                if (!meta.isEmpty())
                {
                    html.append(QStringLiteral("<p>%1</p>").arg(meta));
                }
                html.append(stepImageBlock(document, step, displayNumber));
            }
            return html;
        }
    } // namespace

    Base::Expected<void> writeStepPdf(const std::filesystem::path &filePath, const std::vector<Core::RecordedStep> &steps, const QDateTime &generatedAt)
    {
        if (steps.empty())
        {
            return std::unexpected(Base::Error{Base::ErrorCode::InvalidArgument, Base::uiTextUtf8("还没有步骤可以导出")});
        }
        if (filePath.empty())
        {
            return std::unexpected(Base::Error{Base::ErrorCode::InvalidArgument, Base::uiTextUtf8("导出路径为空，无法写入 PDF。请选择保存位置后重试")});
        }

        const auto displayPath = Base::Paths::toDisplayText(filePath);
        if (const auto parent = filePath.parent_path(); !parent.empty())
        {
            if (const auto ensured = Base::Paths::ensureDirectory(parent); !ensured)
            {
                return std::unexpected(ensured.error());
            }
        }

        // 先删旧文件：QPrinter 打不开时旧文件会原样留着，事后的「存在且非空」会把别人的文件当成自己写的
        std::error_code removeError;
        std::filesystem::remove(filePath, removeError);
        if (removeError)
        {
            return std::unexpected(
                    Base::Error{Base::ErrorCode::FileWriteFailed, Base::uiFormat(Base::uiTextUtf8("无法覆盖已有文件 {}（{}）。请关闭占用它的程序或换一个文件名后重试"),
                                                                                 displayPath.toStdString(), removeError.message())});
        }

        // QPrinter 在析构时才把 PDF 收尾落盘，先让这一层退栈再核对文件
        {
            QPrinter printer{QPrinter::HighResolution};
            printer.setOutputFormat(QPrinter::PdfFormat);
            printer.setOutputFileName(displayPath);
            printer.setPageSize(QPageSize{QPageSize::A4});
            printer.setPageMargins(QMarginsF{kPdfPageMarginMillimeters, kPdfPageMarginMillimeters, kPdfPageMarginMillimeters, kPdfPageMarginMillimeters}, QPageLayout::Millimeter);

            QTextDocument document;
            document.setHtml(buildPdfHtml(document, steps, generatedAt));
            document.print(&printer);
        }

        std::error_code error;
        const auto      writtenSize = std::filesystem::file_size(filePath, error);
        if (error || writtenSize == 0)
        {
            std::error_code ignored;
            std::filesystem::remove(filePath, ignored);
            return std::unexpected(Base::Error{Base::ErrorCode::FileWriteFailed,
                                               Base::uiFormat(Base::uiTextUtf8("步骤 PDF 未写出：{}。请检查磁盘空间与目录权限后重试"), displayPath.toStdString())});
        }
        return {};
    }

} // namespace GyanisScribe::Gui
