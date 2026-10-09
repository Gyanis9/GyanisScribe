#include "Ocr/OcrTextExporter.h"

#include "Core/Paths.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>

#include <format>

namespace GyanisScribe::Ocr
{

    QString toTxt(const OcrResult &result)
    {
        return result.text();
    }

    QString toJson(const OcrResult &result, const QSize &imageSize)
    {
        QJsonArray lineEntries;
        for (const auto &line: result.lines)
        {
            QJsonArray wordEntries;
            for (const auto &word: line.words)
            {
                QJsonObject wordEntry;
                wordEntry.insert(QStringLiteral("text"), word.text);
                wordEntry.insert(QStringLiteral("box"), QJsonObject{
                                                                {QStringLiteral("x"), word.boundingBox.x()},
                                                                {QStringLiteral("y"), word.boundingBox.y()},
                                                                {QStringLiteral("width"), word.boundingBox.width()},
                                                                {QStringLiteral("height"), word.boundingBox.height()},
                                                        });
                wordEntries.append(wordEntry);
            }

            QJsonObject lineEntry;
            lineEntry.insert(QStringLiteral("text"), line.text());
            lineEntry.insert(QStringLiteral("words"), wordEntries);
            lineEntries.append(lineEntry);
        }

        QJsonObject root;
        root.insert(QStringLiteral("language"), result.language);
        root.insert(QStringLiteral("averageConfidence"), result.averageConfidence);
        // 方框坐标只在原图那一套坐标里成立，尺寸一起存出去才可能核对
        root.insert(QStringLiteral("imageSize"), QJsonObject{
                                                         {QStringLiteral("width"), imageSize.width()},
                                                         {QStringLiteral("height"), imageSize.height()},
                                                 });
        root.insert(QStringLiteral("lines"), lineEntries);

        return QString::fromUtf8(QJsonDocument(root).toJson(QJsonDocument::Indented));
    }

    Core::Expected<void> writeTextFile(const std::filesystem::path &filePath, const QString &text)
    {
        if (const auto parent = filePath.parent_path(); !parent.empty())
        {
            if (const auto ensured = Core::Paths::ensureDirectory(parent); !ensured)
            {
                return std::unexpected(ensured.error());
            }
        }

        QSaveFile output(QString::fromLocal8Bit(filePath.string().c_str()));
        if (!output.open(QIODevice::WriteOnly))
        {
            return std::unexpected(Core::Error{Core::ErrorCode::FileWriteFailed, std::format("识别结果写不出去：{}（{}）。请换一个可写的目录，或直接复制到剪贴板",
                                                                                             filePath.string(), output.errorString().toStdString())});
        }

        output.write(text.toUtf8());
        if (!output.commit())
        {
            return std::unexpected(Core::Error{Core::ErrorCode::FileWriteFailed,
                                               std::format("识别结果没落盘：{}（{}）。请检查磁盘空间后重试", filePath.string(), output.errorString().toStdString())});
        }
        return {};
    }

} // namespace GyanisScribe::Ocr
