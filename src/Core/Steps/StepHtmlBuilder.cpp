#include "Core/Steps/StepHtmlBuilder.h"

#include "Base/Paths.h"
#include "Base/Text.h"
#include "Core/Steps/StepImageMarker.h"

#include <QBuffer>
#include <QByteArray>
#include <QDateTime>
#include <QImage>
#include <QSaveFile>
#include <QStringList>

#include <algorithm>
#include <string>

namespace GyanisScribe::Core
{
    namespace
    {
        /// 单文件教程的内联样式；不引用任何外部资源
        constexpr auto kStepHtmlStyle = R"css(
:root { color-scheme: light; }
* { box-sizing: border-box; }
body { margin: 0; padding: 32px 16px; background: #f5f6f8; color: #1f2328; font-family: "Segoe UI", "Microsoft YaHei", sans-serif; line-height: 1.6; }
main { max-width: 960px; margin: 0 auto; }
.tutorial h1 { margin: 0 0 8px; font-size: 28px; }
.tutorial p { margin: 0; color: #57606a; }
.step { position: relative; margin: 0 0 24px; padding: 20px 20px 20px 72px; background: #ffffff; border-radius: 12px; box-shadow: 0 1px 3px rgba(31, 35, 40, 0.12); }
.step .num { position: absolute; left: 20px; top: 20px; width: 36px; height: 36px; border-radius: 50%; background: #e5484d; color: #ffffff; font-weight: 700; line-height: 36px; text-align: center; }
.step h2 { margin: 0 0 8px; font-size: 20px; }
.step .meta { margin: 0 0 12px; color: #57606a; font-size: 14px; }
.step img { display: block; max-width: 100%; height: auto; border: 1px solid #d0d7de; border-radius: 8px; }
.step .placeholder { margin: 12px 0 0; padding: 16px; background: #f6f8fa; border: 1px dashed #d0d7de; border-radius: 8px; color: #57606a; }
)css";

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

        /// 空说明只写中性的类型标签：再拿元素名或窗口标题补回来，等于把用户删掉的敏感文本又导了出去
        [[nodiscard]] QString stepTitle(const RecordedStep &step)
        {
            const QString description = step.description.trimmed();
            if (!description.isEmpty())
            {
                return description;
            }
            switch (step.kind)
            {
                case StepKind::Click:
                    return Base::uiText("点击");
                case StepKind::WindowChange:
                    return Base::uiText("切换窗口");
                case StepKind::Manual:
                    return manualStepDescription();
            }
            return QString();
        }

        /// 把一张图缩到最长边上限后编码成 JPEG data URI；空图返回空
        [[nodiscard]] QString imageDataUri(const QImage &source)
        {
            if (source.isNull())
            {
                return {};
            }
            QImage image = source;
            if (std::max(image.width(), image.height()) > kStepHtmlImageEdge)
            {
                image = image.scaled(kStepHtmlImageEdge, kStepHtmlImageEdge, Qt::KeepAspectRatio, Qt::SmoothTransformation);
                if (image.isNull())
                {
                    return {};
                }
            }

            QByteArray encoded;
            QBuffer    buffer{&encoded};
            if (!buffer.open(QIODevice::WriteOnly) || !image.save(&buffer, "JPG", kStepHtmlJpegQuality))
            {
                return {};
            }
            return QStringLiteral("data:image/jpeg;base64,") + QString::fromLatin1(encoded.toBase64());
        }

        /// 每一步的时刻与窗口标题，两段都取不到时返回空串
        [[nodiscard]] QString stepMeta(const RecordedStep &step)
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

        /// 有图时内嵌 data URI，没有图时给占位；抓屏失败原因一并写出来
        [[nodiscard]] QString stepImageBlock(const RecordedStep &step, const QString &number, const int displayNumber)
        {
            const QString dataUri = imageDataUri(annotatedStepImage(step, displayNumber));
            if (!dataUri.isEmpty())
            {
                const QString alt = escapeHtml(Base::uiText("第 %1 步截图").arg(number));
                return QStringLiteral("<img src=\"%1\" alt=\"%2\">").arg(dataUri, alt);
            }

            const QString reason = step.captureError.trimmed();
            if (!reason.isEmpty())
            {
                return QStringLiteral("<p class=\"placeholder\">%1</p>").arg(escapeHtml(Base::uiText("这一张没截到：%1").arg(reason)));
            }
            return QStringLiteral("<p class=\"placeholder\">%1</p>").arg(escapeHtml(Base::uiText("这一张没截到")));
        }
    } // namespace

    QString buildStepHtml(const std::vector<RecordedStep> &steps, const QDateTime &generatedAt)
    {
        if (steps.empty())
        {
            return {};
        }

        const QString tutorialTitle = escapeHtml(Base::uiText("GyanisScribe 步骤教程"));
        const QString generatedText = generatedAt.isValid() ? generatedAt.toString(QStringLiteral("yyyy-MM-dd HH:mm:ss")) : Base::uiText("未知时间");
        const QString summary       = escapeHtml(Base::uiText("生成于 %1 · 共 %2 步").arg(generatedText).arg(static_cast<qulonglong>(steps.size())));

        QString html;
        html.reserve(static_cast<qsizetype>(steps.size()) * 4096 + 4096);
        html.append(
                QStringLiteral("<!doctype html>\n<html lang=\"zh-CN\">\n<head>\n<meta charset=\"utf-8\">\n<title>%1</title>\n<style>%2</style>\n</head>\n<body>\n<main>\n<header "
                               "class=\"tutorial\">\n<h1>%1</h1>\n<p>%3</p>\n</header>\n")
                        .arg(tutorialTitle, QString::fromLatin1(kStepHtmlStyle), summary));

        for (std::size_t position = 0; position < steps.size(); ++position)
        {
            const RecordedStep &step          = steps[position];
            const int           displayNumber = static_cast<int>(position) + 1;
            const QString       number        = QStringLiteral("%1").arg(displayNumber, 2, 10, QChar('0'));
            html.append(QStringLiteral("<section class=\"step\">\n<div class=\"num\">%1</div>\n<h2>%2</h2>\n").arg(number, escapeHtml(stepTitle(step))));

            const QString meta = stepMeta(step);
            if (!meta.isEmpty())
            {
                html.append(QStringLiteral("<p class=\"meta\">%1</p>\n").arg(meta));
            }
            html.append(stepImageBlock(step, number, displayNumber));
            html.append(QStringLiteral("\n</section>\n"));
        }

        html.append(QStringLiteral("</main>\n</body>\n</html>\n"));
        return html;
    }

    Base::Expected<void> writeStepHtml(const std::filesystem::path &filePath, const std::vector<RecordedStep> &steps, const QDateTime &generatedAt)
    {
        if (steps.empty())
        {
            return std::unexpected(Base::Error{Base::ErrorCode::InvalidArgument, Base::uiTextUtf8("还没有步骤可以导出")});
        }

        const QString html = buildStepHtml(steps, generatedAt);
        if (html.isEmpty())
        {
            return std::unexpected(Base::Error{Base::ErrorCode::InternalError, Base::uiTextUtf8("步骤教程没有生成内容，请重试")});
        }

        const auto displayPath = Base::Paths::toDisplayText(filePath);
        if (const auto parent = filePath.parent_path(); !parent.empty())
        {
            if (const auto ensured = Base::Paths::ensureDirectory(parent); !ensured)
            {
                return std::unexpected(ensured.error());
            }
        }

        QSaveFile output{displayPath};
        if (!output.open(QIODevice::WriteOnly))
        {
            return std::unexpected(Base::Error{Base::ErrorCode::FileWriteFailed, Base::uiFormat(Base::uiTextUtf8("步骤教程写不出去：{}（{}）。请换一个可写的目录后重试"),
                                                                                                displayPath.toStdString(), output.errorString().toStdString())});
        }

        const QByteArray bytes = html.toUtf8();
        // 写失败不再调 cancelWriting：它会把出错原因换成「取消写入」，析构时本来也会丢弃临时文件
        if (output.write(bytes) != static_cast<qint64>(bytes.size()))
        {
            return std::unexpected(Base::Error{Base::ErrorCode::FileWriteFailed, Base::uiFormat(Base::uiTextUtf8("步骤教程未保存：{}（{}）。请检查磁盘空间后重试"),
                                                                                                displayPath.toStdString(), output.errorString().toStdString())});
        }
        if (!output.commit())
        {
            return std::unexpected(Base::Error{Base::ErrorCode::FileWriteFailed, Base::uiFormat(Base::uiTextUtf8("步骤教程未保存：{}（{}）。请检查磁盘空间后重试"),
                                                                                                displayPath.toStdString(), output.errorString().toStdString())});
        }
        return {};
    }

} // namespace GyanisScribe::Core
