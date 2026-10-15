#include "Core/Ocr/OcrLanguage.h"

namespace GyanisScribe::Core
{

    QStringList supportedOcrLanguageTags()
    {
        // 标签是本程序对外的词表，后端各自的叫法在引擎内部映射，界面不接触映射表
        return {QStringLiteral("zh-Hans"), QStringLiteral("en")};
    }

} // namespace GyanisScribe::Core
