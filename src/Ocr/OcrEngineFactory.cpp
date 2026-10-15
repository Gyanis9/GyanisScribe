#include "Ocr/OcrEngineFactory.h"

#include "Base/Logger.h"
#include "Base/Paths.h"
#include "Base/Text.h"
#if defined(GYANISSCRIBE_HAS_TESSERACT)
#include "Ocr/TesseractOcrEngine.h"
#endif
#if defined(GYANISSCRIBE_HAS_WINRT_OCR)
#include "Ocr/WinRTImageOcrEngine.h"
#endif
#include "Ocr/UnavailableOcrEngine.h"
#include "Platform/IPlatformIntegration.h"
#include "Settings/ISettingsStore.h"
#include "Settings/SettingsKeys.h"

#include <filesystem>
#include <format>
#include <vector>
#include <string_view>

namespace GyanisScribe::Ocr
{
    namespace
    {
        /// 语言包目录的候选顺序里那一个固定子目录名
        constexpr const char *kTessDataFolder = "tessdata";

        // 找过的地方一次说全：只报「没有语言包」用户不知道该往哪放
        QString describeMissingLanguagePacks(const std::string_view configuredKey, const std::vector<std::filesystem::path> &candidates)
        {
            QStringList searched;
            for (const auto &candidate: candidates)
            {
                if (!candidate.empty())
                {
                    searched << Base::Paths::toDisplayText(candidate);
                }
            }
            return QString::fromUtf8(
                    GyanisScribe::Base::uiFormat(GyanisScribe::Base::uiTextUtf8("找遍了设置项 {}、环境变量 TESSDATA_PREFIX 与这些目录都没有语言包：{}。语言包本该随安装包一起装好，"
                                                                                "请先试重新安装本程序；也可以把 eng.traineddata（要认中文再放 chi_sim.traineddata）放进其中一处，"
                                                                                "或在设置的 OCR 页把语言包目录指过去"),
                                                 std::string{configuredKey}, searched.join(QStringLiteral("；")).toStdString())
                            .c_str());
        }

        // 语言包在、这个构建里却没有后端：这是构建配置的事，不能让用户以为放好语言包就能认字。
        // 只有没编入 Tesseract 的那一种形状需要它，所以它跟着编译宏一起条件存在，
        // 否则在带后端的构建里就是一个没人调用的函数（/W4 /WX 会当场报未引用）
#if !defined(GYANISSCRIBE_HAS_TESSERACT)
        QString describeMissingBackend(const std::filesystem::path &tessData)
        {
            return QString::fromUtf8(
                    GyanisScribe::Base::uiFormat(
                            GyanisScribe::Base::uiTextUtf8("语言包已就位于 {}，但本次构建没有编入任何识别后端。见 docs/OcrBackends.md：这是 Debug 档的形状，Release 档带真后端"),
                            Base::Paths::toDisplayText(tessData).toStdString())
                            .c_str());
        }
#endif
    } // namespace

    std::vector<std::filesystem::path> tessDataCandidateDirectories(const Settings::ISettingsStore &settings, const std::filesystem::path &applicationDirectory)
    {
        std::vector<std::filesystem::path> candidates;

        // 界面与环境变量里的文本换成路径要走 wstring：窄串按本地码页解释，装不下就抛
        const auto configuredText = settings.stringValue(Settings::kOcrTessDataDirectory, QString()).trimmed();
        if (!configuredText.isEmpty())
        {
            candidates.push_back(Base::Paths::fromDisplayText(configuredText));
        }

        const auto environmentText = qEnvironmentVariable("TESSDATA_PREFIX");
        if (!environmentText.isEmpty())
        {
            candidates.push_back(Base::Paths::fromDisplayText(environmentText));
        }

        // 随包那份排在用户数据目录之前：装完即用是本软件的要求，语言包在安装包里
        if (!applicationDirectory.empty())
        {
            candidates.push_back(applicationDirectory / kTessDataFolder);
        }
        candidates.push_back(Base::Paths::applicationDataDirectory() / kTessDataFolder);
        return candidates;
    }

    std::filesystem::path firstDirectoryWithLanguagePacks(const std::vector<std::filesystem::path> &candidates)
    {
        for (const auto &candidate: candidates)
        {
            if (candidate.empty() || !std::filesystem::is_directory(candidate))
            {
                continue;
            }
            // 空目录不算「有语言包」：Tesseract 在其中一个模型也认不出来，不如在这里就判不可用
            for (const auto &entry: std::filesystem::directory_iterator(candidate))
            {
                if (entry.is_regular_file() && entry.path().extension() == ".traineddata")
                {
                    return candidate;
                }
            }
        }
        return {};
    }

    std::filesystem::path locateTessDataDirectory(const Settings::ISettingsStore &settings)
    {
        return firstDirectoryWithLanguagePacks(tessDataCandidateDirectories(settings, Base::Paths::applicationDirectory()));
    }

    std::unique_ptr<IOcrEngine> createOcrEngine(const Settings::ISettingsStore &settings, const Platform::IPlatformIntegration &integration)
    {
#if defined(GYANISSCRIBE_HAS_WINRT_OCR)
        // 系统识别排第一：它不要求用户放语言包，语言包由 Windows 自己装；
        // 它不可用时才往下找 Tesseract，两条原因分开记，用户能自己做的只有后一条
        auto systemEngine = std::make_unique<WinRTImageOcrEngine>();
        if (systemEngine->isAvailable())
        {
            Base::Logger::info(std::format("文字识别后端 Windows 系统识别就绪（平台 {}），可用语言 {}", integration.displayName().toStdString(),
                                           systemEngine->supportedLanguages().join(QStringLiteral("、")).toStdString()));
            return systemEngine;
        }
        Base::Logger::info(std::format("Windows 系统识别不可用，继续找 Tesseract：{}", systemEngine->unavailableReason().toStdString()));
#endif

        const auto tessData = locateTessDataDirectory(settings);
        if (tessData.empty())
        {
            // 一处都没有语言包：这一格与「后端没编进来」要分开说，用户能自己做的只有重装或放文件
            const auto reason = describeMissingLanguagePacks(Settings::kOcrTessDataDirectory, tessDataCandidateDirectories(settings, Base::Paths::applicationDirectory()));
            Base::Logger::warning(std::format("文字识别不可用（平台 {}）：{}", integration.displayName().toStdString(), reason.toStdString()));
            return std::make_unique<UnavailableOcrEngine>(reason);
        }

#if defined(GYANISSCRIBE_HAS_TESSERACT)
        // 语言包在位就交给真后端；它自己会核对目录里有没有认识的语言，
        // 目录里全是别的 traineddata 时那份原因比这里更具体
        auto tesseract = std::make_unique<TesseractOcrEngine>(tessData);
        if (tesseract->isAvailable())
        {
            Base::Logger::info(std::format("文字识别后端 Tesseract 就绪（平台 {}），语言包目录 {}", integration.displayName().toStdString(),
                                           Base::Paths::toDisplayText(tessData).toStdString()));
            return tesseract;
        }
        const auto unavailable = tesseract->unavailableReason();
#else
        const auto unavailable = describeMissingBackend(tessData);
#endif
        Base::Logger::warning(std::format("文字识别不可用（平台 {}）：{}", integration.displayName().toStdString(), unavailable.toStdString()));
        return std::make_unique<UnavailableOcrEngine>(unavailable);
    }

    QStringList supportedOcrLanguageTags()
    {
        // 标签是本程序对外的词表，后端各自的叫法在引擎内部映射，界面不接触映射表
        return {QStringLiteral("zh-Hans"), QStringLiteral("en")};
    }

} // namespace GyanisScribe::Ocr
