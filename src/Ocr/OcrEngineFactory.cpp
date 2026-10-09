#include "Ocr/OcrEngineFactory.h"

#include "Core/Logger.h"
#include "Core/Paths.h"
#include "Ocr/UnavailableOcrEngine.h"
#include "Platform/IPlatformIntegration.h"
#include "Settings/ISettingsStore.h"
#include "Settings/SettingsKeys.h"

#include <filesystem>
#include <format>

namespace GyanisScribe::Ocr
{
    namespace
    {
        /// 语言包目录的候选顺序里那一个固定子目录名
        constexpr const char *kTessDataFolder = "tessdata";

        // 三个候选位置一次说全：只报「没有语言包」用户不知道该往哪放
        QString describeMissingLanguagePacks(const std::filesystem::path &configuredKey, const std::filesystem::path &bundled)
        {
            return QString::fromUtf8(std::format("设置项 {}、环境变量 TESSDATA_PREFIX、以及 {} 三处都没有语言包。"
                                                 "请把 eng.traineddata（要认中文再放 chi_sim.traineddata）放进其中一处，"
                                                 "或在设置的 OCR 页把语言包目录指过去",
                                                 configuredKey.string(), bundled.string())
                                             .c_str());
        }

        // 语言包在、后端不在：这是构建配置的事，不能让用户以为放好语言包就能认字
        QString describeMissingBackend(const std::filesystem::path &tessData)
        {
            return QString::fromUtf8(std::format("语言包已就位于 {}，但本次构建没有编入任何识别后端。"
                                                 "见 docs/OcrBackends.md：把 Tesseract 加进依赖清单并重新配置构建即可接入",
                                                 tessData.string())
                                             .c_str());
        }
    } // namespace

    std::filesystem::path locateTessDataDirectory(const Settings::ISettingsStore &settings)
    {
        const auto hasModels = [](const std::filesystem::path &candidate)
        {
            if (candidate.empty() || !std::filesystem::is_directory(candidate))
            {
                return false;
            }
            // 空目录不算「有语言包」：Tesseract 在其中一个模型也认不出来，不如在这里就判不可用
            for (const auto &entry: std::filesystem::directory_iterator(candidate))
            {
                if (entry.is_regular_file() && entry.path().extension() == ".traineddata")
                {
                    return true;
                }
            }
            return false;
        };

        const auto configured = settings.stringValue(Settings::kOcrTessDataDirectory, QString()).trimmed().toStdString();
        if (!configured.empty() && hasModels(std::filesystem::path(configured)))
        {
            return std::filesystem::path(configured);
        }

        const auto fromEnvironment = qEnvironmentVariable("TESSDATA_PREFIX");
        if (!fromEnvironment.isEmpty() && hasModels(std::filesystem::path(fromEnvironment.toStdString())))
        {
            return std::filesystem::path(fromEnvironment.toStdString());
        }

        const auto bundled = Core::Paths::applicationDataDirectory() / kTessDataFolder;
        if (hasModels(bundled))
        {
            return bundled;
        }
        return {};
    }

    std::unique_ptr<IOcrEngine> createOcrEngine(const Settings::ISettingsStore &settings, const Platform::IPlatformIntegration &integration)
    {
        const auto tessData = locateTessDataDirectory(settings);
        const auto reason = tessData.empty() ? describeMissingLanguagePacks(std::string(Settings::kOcrTessDataDirectory), Core::Paths::applicationDataDirectory() / kTessDataFolder)
                                             : describeMissingBackend(tessData);
        Core::Logger::warning(std::format("文字识别不可用（平台 {}）：{}", integration.displayName().toStdString(), reason.toStdString()));
        return std::make_unique<UnavailableOcrEngine>(reason);
    }

    QStringList supportedOcrLanguageTags()
    {
        // 标签是本程序对外的词表，后端各自的叫法在引擎内部映射，界面不接触映射表
        return {QStringLiteral("zh-Hans"), QStringLiteral("en")};
    }

} // namespace GyanisScribe::Ocr
