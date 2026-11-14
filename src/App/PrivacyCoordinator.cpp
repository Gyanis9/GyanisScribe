#include "App/PrivacyCoordinator.h"

#include "Base/Text.h"
#include "Core/Ocr/IOcrEngine.h"
#include "Core/Ocr/OcrLanguage.h"
#include "Core/Ocr/RecognitionRunner.h"
#include "Core/Settings/ISettingsStore.h"
#include "Core/Settings/SettingsKeys.h"
#include "Platform/IPlatformIntegration.h"
#include "Plugins/OcrEngines/OcrEngineFactory.h"

namespace GyanisScribe::App
{
    namespace
    {
        /**
         * @brief 没有后端对象时的兜底原因
         * @return 与识别链同一句中文「原因 + 替代做法」
         */
        QString fallbackUnavailableReason()
        {
            return GyanisScribe::Base::uiText("本机没有可用的文字识别后端：请检查设置中的 OCR 模型目录，或重新安装本程序后重试");
        }
    } // namespace

    PrivacyCoordinator::PrivacyCoordinator(GyanisScribe::Core::ISettingsStore &settings, GyanisScribe::Platform::IPlatformIntegration &integration, QObject *parent) :
        QObject(parent), m_settings(settings)
    {
        m_engine = GyanisScribe::Plugins::createOcrEngine(m_settings, integration);
        initializeRunner();
    }

    PrivacyCoordinator::PrivacyCoordinator(GyanisScribe::Core::ISettingsStore &settings, std::unique_ptr<GyanisScribe::Core::IOcrEngine> engine, QObject *parent) :
        QObject(parent), m_settings(settings), m_engine(std::move(engine))
    {
        initializeRunner();
    }

    PrivacyCoordinator::~PrivacyCoordinator() = default;

    void PrivacyCoordinator::initializeRunner()
    {
        // 引擎为空时不建搬运工：requestScan 会按「后端不可用」给出可操作原因
        if (m_engine == nullptr)
        {
            return;
        }

        // Runner 拿着引擎的引用，成员析构按声明反序：m_runner 必须先于 m_engine 回收
        m_runner = std::make_unique<GyanisScribe::Core::RecognitionRunner>(*m_engine);
        QObject::connect(m_runner.get(), &GyanisScribe::Core::RecognitionRunner::succeeded, this, &PrivacyCoordinator::handleSucceeded);
        QObject::connect(m_runner.get(), &GyanisScribe::Core::RecognitionRunner::failed, this, &PrivacyCoordinator::handleFailed);
    }

    void PrivacyCoordinator::requestScan(const QImage &image)
    {
        // 空图属调用方没准备好：一句提示即可，不进入单飞状态，也不发 scanFailed
        if (image.isNull())
        {
            emit messageForUser(GyanisScribe::Base::uiText("还没有可检查的图：先截一张图或打开一张图片，再发起隐私打码"), GyanisScribe::Gui::ToastKind::Information);
            return;
        }

        if (m_engine == nullptr || m_runner == nullptr || !m_engine->isAvailable())
        {
            const auto reason  = m_engine != nullptr ? m_engine->unavailableReason() : QString{};
            const auto message = reason.isEmpty() ? fallbackUnavailableReason() : reason;
            emit scanFailed(GyanisScribe::Base::Error{GyanisScribe::Base::ErrorCode::PlatformUnsupported, message.toStdString()});
            emit messageForUser(message, GyanisScribe::Gui::ToastKind::Danger);
            return;
        }

        // 单飞：上一次没回来时不再排第二张，只重复一句「正在检查」
        if (m_scanning)
        {
            emit messageForUser(GyanisScribe::Base::uiText("正在检查敏感信息…"), GyanisScribe::Gui::ToastKind::Information);
            return;
        }

        // 先置标记再交给搬运工：识别在别处跑，结果可能很快回到界面线程
        m_scanning = true;
        emit messageForUser(GyanisScribe::Base::uiText("正在检查敏感信息…"), GyanisScribe::Gui::ToastKind::Information);

        const auto language = m_settings.stringValue(GyanisScribe::Core::kOcrLanguage, QString::fromUtf8(GyanisScribe::Core::kDefaultOcrLanguage));
        m_runner->request(image, language);
    }

    bool PrivacyCoordinator::isScanning() const noexcept
    {
        return m_scanning;
    }

    void PrivacyCoordinator::handleSucceeded(const QImage &image, const QString &, const GyanisScribe::Core::OcrResult &result)
    {
        m_scanning = false;
        // 只把图与结果交回控制器：这里不开结果窗、不记语言，也不写识别文本
        emit scanSucceeded(image, result);
    }

    void PrivacyCoordinator::handleFailed(const GyanisScribe::Base::Error &error)
    {
        m_scanning = false;
        const auto message =
                QString::fromUtf8(GyanisScribe::Base::uiFormat(GyanisScribe::Base::uiTextUtf8("敏感信息检查失败：{}。请重试一次，或换一种语言再试"), error.message).c_str());
        emit scanFailed(error);
        emit messageForUser(message, GyanisScribe::Gui::ToastKind::Danger);
    }

} // namespace GyanisScribe::App
