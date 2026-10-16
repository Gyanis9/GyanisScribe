#include "App/OcrCoordinator.h"

#include "Base/Logger.h"
#include "Base/Paths.h"
#include "Base/Text.h"
#include "Core/Image/ImageLoading.h"
#include "Core/Ocr/IOcrEngine.h"
#include "Core/Ocr/OcrTypes.h"
#include "Core/Ocr/RecognitionRunner.h"
#include "Core/Settings/ISettingsStore.h"
#include "Core/Settings/NamingRules.h"
#include "Core/Settings/SettingsKeys.h"
#include "Gui/Ocr/OcrResultWindow.h"
#include "Gui/Resources/IconProvider.h"
#include "Gui/Theme/ThemeManager.h"
#include "Platform/IPlatformIntegration.h"
#include "Plugins/OcrEngines/OcrEngineFactory.h"

#include <QDir>
#include <QFileDialog>

#include <format>

namespace GyanisScribe::App
{
    OcrCoordinator::OcrCoordinator(GyanisScribe::Core::ISettingsStore &settings, GyanisScribe::Platform::IPlatformIntegration &integration, GyanisScribe::Gui::IconProvider &icons,
                                   GyanisScribe::Gui::ThemeManager &theme, QObject *parent) : QObject(parent), m_settings(settings), m_icons(icons), m_theme(theme)
    {
        m_ocr = GyanisScribe::Plugins::createOcrEngine(m_settings, integration);
        // Runner 拿着引擎的引用，它的析构必须排在引擎之前：成员的析构按声明反序，头文件里那份声明就排在 m_ocr 后面
        m_runner = std::make_unique<GyanisScribe::Core::RecognitionRunner>(*m_ocr);

        QObject::connect(m_runner.get(), &GyanisScribe::Core::RecognitionRunner::succeeded, this, &OcrCoordinator::handleSucceeded);
        QObject::connect(m_runner.get(), &GyanisScribe::Core::RecognitionRunner::failed, this, &OcrCoordinator::handleFailed);
    }

    OcrCoordinator::~OcrCoordinator() = default;

    void OcrCoordinator::requestRecognition(const QImage &image)
    {
        m_image = image;
        // 语言取上一次的选择：换过一次就该记住，而不是每轮都退回默认档
        recognizeWithLanguage(m_settings.stringValue(GyanisScribe::Core::kOcrLanguage, QString::fromUtf8(GyanisScribe::Core::kDefaultOcrLanguage)));
    }

    void OcrCoordinator::recognizeFile(const std::filesystem::path &filePath)
    {
        auto image = GyanisScribe::Core::loadImageFile(filePath);
        if (!image)
        {
            emit messageForUser(QString::fromUtf8(image.error().message.c_str()), GyanisScribe::Gui::ToastKind::Danger);
            return;
        }
        requestRecognition(*image);
    }

    void OcrCoordinator::pickImageFileAndRecognize()
    {
        // 没有窗口可当父对象（托盘动作发生时主窗口多半是藏起来的），对话框按应用级模态开
        const auto picked = QFileDialog::getOpenFileName(nullptr, GyanisScribe::Base::uiText("识别图片文件"), QDir::homePath(),
                                                         GyanisScribe::Base::uiText("图片文件 (*.png *.jpg *.jpeg *.bmp *.webp *.gif)"));
        if (picked.isEmpty())
        {
            // 取消不是失败：一句都不多说，也不落日志
            return;
        }
        recognizeFile(GyanisScribe::Base::Paths::fromDisplayText(picked));
    }

    void OcrCoordinator::recognizeWithLanguage(const QString &language)
    {
        if (!m_ocr->isAvailable())
        {
            const auto reason = m_ocr->unavailableReason();
            GyanisScribe::Base::Logger::error(std::format("文字识别没能开始：{}", reason.toStdString()));
            emit messageForUser(reason, GyanisScribe::Gui::ToastKind::Danger);
            return;
        }

        if (m_image.isNull())
        {
            emit messageForUser(GyanisScribe::Base::uiText("还没有可识别的图：先按 Ctrl+Alt+A 框选一块屏幕"), GyanisScribe::Gui::ToastKind::Information);
            return;
        }

        // 一次识别要跑几十到上千毫秒，放在这条线程上整个程序就停在那儿：交出去之前先说一句，
        // 否则结果回来之前界面看着像「点了没反应」
        emit messageForUser(GyanisScribe::Base::uiText("正在识别这一张…"), GyanisScribe::Gui::ToastKind::Information);
        m_runner->request(m_image, language);
    }

    void OcrCoordinator::handleSucceeded(const QImage &image, const QString &language, const GyanisScribe::Core::OcrResult &result)
    {
        // 认出多少算多少：空结果也要开窗，摘要里那句「这一张没认出文字」比一条三秒就消失的提示有用
        if (auto stored = m_settings.setValue(GyanisScribe::Core::kOcrLanguage, language); !stored)
        {
            GyanisScribe::Base::Logger::warning(std::format("识别语言没能记住：{}", stored.error().message));
        }

        // 底图用发出请求时那一张：期间用户可能又截了新图，而方框是按送去认的那一张算的，换了图就整体错位
        if (m_window == nullptr)
        {
            m_window = new GyanisScribe::Gui::OcrResultWindow{image, result, m_icons, m_theme, GyanisScribe::Core::nameTemplateOrDefault(m_settings)};
            // 关掉就自毁，协调器只持 QPointer：下一次识别会重新建一个，不会往已析构的对象里塞结果
            m_window->setAttribute(Qt::WA_DeleteOnClose);
            QObject::connect(m_window, &GyanisScribe::Gui::OcrResultWindow::recognizeRequested, this, &OcrCoordinator::recognizeWithLanguage);
            QObject::connect(m_window, &GyanisScribe::Gui::OcrResultWindow::messageForUser, this, &OcrCoordinator::messageForUser);
        } else
        {
            m_window->setResult(image, result);
        }

        m_window->show();
        m_window->raise();
        m_window->activateWindow();
        GyanisScribe::Base::Logger::info(std::format("文字识别完成：{} 行，语言 {}", result.lines.size(), language.toStdString()));
    }

    void OcrCoordinator::handleFailed(const GyanisScribe::Base::Error &error)
    {
        const auto message =
                QString::fromUtf8(GyanisScribe::Base::uiFormat(GyanisScribe::Base::uiTextUtf8("文字识别失败：{}。请重试一次，或换一种语言再试"), error.message).c_str());
        GyanisScribe::Base::Logger::error(message.toStdString());
        emit messageForUser(message, GyanisScribe::Gui::ToastKind::Danger);
    }

} // namespace GyanisScribe::App
