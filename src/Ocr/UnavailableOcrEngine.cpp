#include "Ocr/UnavailableOcrEngine.h"

#include "Core/Text.h"

namespace GyanisScribe::Ocr
{

    UnavailableOcrEngine::UnavailableOcrEngine(QString reason) : m_reason(std::move(reason))
    {
    }

    UnavailableOcrEngine::~UnavailableOcrEngine() = default;

    QString UnavailableOcrEngine::name() const
    {
        return GyanisScribe::Core::uiText("无可用识别后端");
    }

    bool UnavailableOcrEngine::isAvailable() const
    {
        return false;
    }

    QString UnavailableOcrEngine::unavailableReason() const
    {
        return m_reason;
    }

    QStringList UnavailableOcrEngine::supportedLanguages() const
    {
        return {};
    }

    Core::Expected<OcrResult> UnavailableOcrEngine::recognize(const QImage &, const QString &)
    {
        return std::unexpected(Core::Error{Core::ErrorCode::DependencyMissing, m_reason.toStdString()});
    }

} // namespace GyanisScribe::Ocr
