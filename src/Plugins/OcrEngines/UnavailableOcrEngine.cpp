#include "Plugins/OcrEngines/UnavailableOcrEngine.h"

#include "Base/Text.h"

namespace GyanisScribe::Plugins
{

    UnavailableOcrEngine::UnavailableOcrEngine(QString reason) : m_reason(std::move(reason))
    {
    }

    UnavailableOcrEngine::~UnavailableOcrEngine() = default;

    QString UnavailableOcrEngine::name() const
    {
        return GyanisScribe::Base::uiText("无可用识别后端");
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

    Base::Expected<Core::OcrResult> UnavailableOcrEngine::recognize(const QImage &, const QString &)
    {
        return std::unexpected(Base::Error{Base::ErrorCode::DependencyMissing, m_reason.toStdString()});
    }

} // namespace GyanisScribe::Plugins
