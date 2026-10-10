#include "Ocr/OcrEngineFactory.h"

#include "Core/Logger.h"
#include "Core/Paths.h"
#include "Core/Text.h"
#if defined(GYANISSCRIBE_HAS_TESSERACT)
#include "Ocr/TesseractOcrEngine.h"
#endif
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
            return QString::fromUtf8(
                    GyanisScribe::Core::uiFormat(GyanisScribe::Core::uiTextUtf8("设置项 {}、环境变量 TESSDATA_PREFIX、以及 {} 三处都没有语言包。请把 eng.traineddata（要认中文再放 "
                                                                                "chi_sim.traineddata）放进其中一处，或在设置的 OCR 页把语言包目录指过去"),
                                                 configuredKey.string(), bundled.string())
                            .c_str());
        }

        // 语言包在、这个构建里却没有后端：这是构建配置的事，不能让用户以为放好语言包就能认字。
        // 只有没编入 Tesseract 的那一种形状需要它，所以它跟着编译宏一起条件存在，
        // 否则在带后端的构建里就是一个没人调用的函数（/W4 /WX 会当场报未引用）
#if !defined(GYANISSCRIBE_HAS_TESSERACT)
        QString describeMissingBackend(const std::filesystem::path &tessData)
        {
            return QString::fromUtf8(
                    GyanisScribe::Core::uiFormat(
                            GyanisScribe::Core::uiTextUtf8("语言包已就位于 {}，但本次构建没有编入任何识别后端。见 docs/OcrBackends.md：这是 Debug 档的形状，Release 档带真后端"),
                            tessData.string())
                            .c_str());
        }
#endif
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

        // 两处路径都从界面/环境变量里的文本换成路径要走 wstring：窄串按本地码页解释，装不下就抛
        const auto configuredText = settings.stringValue(Settings::kOcrTessDataDirectory, QString()).trimmed();
        if (!configuredText.isEmpty())
        {
            const auto configured = Core::Paths::fromDisplayText(configuredText);
            if (hasModels(configured))
            {
                return configured;
            }
        }

        const auto environmentText = qEnvironmentVariable("TESSDATA_PREFIX");
        if (!environmentText.isEmpty())
        {
            const auto fromEnvironment = Core::Paths::fromDisplayText(environmentText);
            if (hasModels(fromEnvironment))
            {
                return fromEnvironment;
            }
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
        if (tessData.empty())
        {
            // 三处都没有语言包：这一格与「后端没编进来」要分开说，用户能自己做的只有放文件
            const auto reason = describeMissingLanguagePacks(std::string(Settings::kOcrTessDataDirectory), Core::Paths::applicationDataDirectory() / kTessDataFolder);
            Core::Logger::warning(std::format("文字识别不可用（平台 {}）：{}", integration.displayName().toStdString(), reason.toStdString()));
            return std::make_unique<UnavailableOcrEngine>(reason);
        }

#if defined(GYANISSCRIBE_HAS_TESSERACT)
        // 语言包在位就交给真后端；它自己会核对目录里有没有认识的语言，
        // 目录里全是别的 traineddata 时那份原因比这里更具体
        auto tesseract = std::make_unique<TesseractOcrEngine>(tessData);
        if (tesseract->isAvailable())
        {
            Core::Logger::info(std::format("文字识别后端 Tesseract 就绪（平台 {}），语言包目录 {}", integration.displayName().toStdString(), tessData.string()));
            return tesseract;
        }
        const auto unavailable = tesseract->unavailableReason();
#else
        const auto unavailable = describeMissingBackend(tessData);
#endif
        Core::Logger::warning(std::format("文字识别不可用（平台 {}）：{}", integration.displayName().toStdString(), unavailable.toStdString()));
        return std::make_unique<UnavailableOcrEngine>(unavailable);
    }

    QStringList supportedOcrLanguageTags()
    {
        // 标签是本程序对外的词表，后端各自的叫法在引擎内部映射，界面不接触映射表
        return {QStringLiteral("zh-Hans"), QStringLiteral("en")};
    }

} // namespace GyanisScribe::Ocr
