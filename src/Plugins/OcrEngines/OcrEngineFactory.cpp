#include "Plugins/OcrEngines/OcrEngineFactory.h"

#include "Base/Logger.h"
#include "Base/Paths.h"
#include "Base/Text.h"
#if defined(GYANISSCRIBE_HAS_WINRT_OCR)
#include "Plugins/OcrEngines/WinRTImageOcrEngine.h"
#endif
#include "Plugins/OcrEngines/PpOcrOnnxEngine.h"
#include "Plugins/OcrEngines/UnavailableOcrEngine.h"
#include "Platform/IPlatformIntegration.h"
#include "Core/Settings/ISettingsStore.h"
#include "Core/Settings/SettingsKeys.h"

#include <filesystem>
#include <format>
#include <string_view>
#include <vector>

namespace GyanisScribe::Plugins
{
    namespace
    {
        /// 模型目录的候选顺序里那一个固定子目录名
        constexpr const char *kPpOcrFolder = "ppocr";

        /// 三件模型文件里缺了的那几件：不存在与空文件都算缺
        QStringList missingModelFiles(const std::filesystem::path &directory)
        {
            QStringList missing;
            for (const char *fileName: {kPpOcrDetModelFileName, kPpOcrRecModelFileName, kPpOcrDictFileName})
            {
                std::error_code error;
                const auto      size = std::filesystem::file_size(directory / fileName, error);
                if (error || size == 0)
                {
                    missing << QString::fromLatin1(fileName);
                }
            }
            return missing;
        }

        // 找过的地方与各处的缺口一次说全：只报「没找到」用户不知道该往哪放
        QString describeMissingPpOcrModels(const std::vector<std::filesystem::path> &candidates)
        {
            QStringList searched;
            for (const auto &candidate: candidates)
            {
                searched << QString::fromUtf8(
                        GyanisScribe::Base::uiFormat(GyanisScribe::Base::uiTextUtf8("{}（缺少 {}）"), Base::Paths::toDisplayText(candidate).toStdString(),
                                                     missingModelFiles(candidate).join(QStringLiteral("、")).toStdString())
                                .c_str());
            }
            return QString::fromUtf8(
                    GyanisScribe::Base::uiFormat(GyanisScribe::Base::uiTextUtf8("没有找到完整的 PaddleOCR 模型目录。找过的位置与缺少的文件：{}。"
                                                                                "模型本该随安装包一起装好，请先试重新安装本程序；也可以在设置的 OCR 页把模型目录指过去"),
                                                 searched.join(QStringLiteral("；")).toStdString())
                            .c_str());
        }
    } // namespace

    std::vector<std::filesystem::path> ppOcrModelCandidateDirectories(const Core::ISettingsStore &settings, const std::filesystem::path &applicationDirectory)
    {
        std::vector<std::filesystem::path> candidates;

        // 界面里的文本换成路径要走 wstring：窄串按本地码页解释，装不下就抛
        const auto configuredText = settings.stringValue(Core::kOcrModelDirectory, QString()).trimmed();
        if (!configuredText.isEmpty())
        {
            candidates.push_back(Base::Paths::fromDisplayText(configuredText));
        }

        // 随包那份排在用户数据目录之前：装完即用是本软件的要求，模型在安装包里
        if (!applicationDirectory.empty())
        {
            candidates.push_back(applicationDirectory / kPpOcrFolder);
        }
        candidates.push_back(Base::Paths::applicationDataDirectory() / kPpOcrFolder);
        return candidates;
    }

    std::filesystem::path firstDirectoryWithModels(const std::vector<std::filesystem::path> &candidates)
    {
        for (const auto &candidate: candidates)
        {
            if (!candidate.empty() && missingModelFiles(candidate).isEmpty())
            {
                return candidate;
            }
        }
        return {};
    }

    std::filesystem::path locatePpOcrModelDirectory(const Core::ISettingsStore &settings, const std::filesystem::path &applicationDirectory)
    {
        const auto installDirectory = applicationDirectory.empty() ? Base::Paths::applicationDirectory() : applicationDirectory;
        return firstDirectoryWithModels(ppOcrModelCandidateDirectories(settings, installDirectory));
    }

    std::unique_ptr<Core::IOcrEngine> createOcrEngine(const Core::ISettingsStore &settings, const Platform::IPlatformIntegration &integration,
                                                      const std::filesystem::path &applicationDirectory)
    {
        const auto installDirectory = applicationDirectory.empty() ? Base::Paths::applicationDirectory() : applicationDirectory;
        const auto candidates       = ppOcrModelCandidateDirectories(settings, installDirectory);
        const auto modelDirectory   = firstDirectoryWithModels(candidates);

        auto ppocr = std::make_unique<PpOcrOnnxEngine>(modelDirectory);
        if (ppocr->isAvailable())
        {
            Base::Logger::info(std::format("文字识别后端 PaddleOCR 就绪（平台 {}），模型目录 {}", integration.displayName().toStdString(),
                                           Base::Paths::toDisplayText(modelDirectory).toStdString()));
            return ppocr;
        }

        // 模型目录齐了、运行库却没起来时，后端自己的原因比「缺哪个文件」具体
        const QString ppocrReason = modelDirectory.empty() ? describeMissingPpOcrModels(candidates) : ppocr->unavailableReason();

#if defined(GYANISSCRIBE_HAS_WINRT_OCR)
        // PaddleOCR 不可用时才轮到系统识别；两条原因分开记，用户能自己做的只有前者
        auto systemEngine = std::make_unique<WinRTImageOcrEngine>();
        if (systemEngine->isAvailable())
        {
            Base::Logger::info(std::format("PaddleOCR 不可用（{}），改用 Windows 系统识别（平台 {}）", ppocrReason.toStdString(),
                                           integration.displayName().toStdString()));
            return systemEngine;
        }

        const auto combinedReason = QStringLiteral("%1\n%2").arg(ppocrReason, systemEngine->unavailableReason());
        Base::Logger::warning(std::format("文字识别不可用（平台 {}）：{}", integration.displayName().toStdString(), combinedReason.toStdString()));
        return std::make_unique<UnavailableOcrEngine>(combinedReason);
#else
        Base::Logger::warning(std::format("文字识别不可用（平台 {}）：{}", integration.displayName().toStdString(), ppocrReason.toStdString()));
        return std::make_unique<UnavailableOcrEngine>(ppocrReason);
#endif
    }

} // namespace GyanisScribe::Plugins
