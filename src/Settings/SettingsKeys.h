// 键名常量表：设置里的键只在「有真实消费方」时才登记在这一处，调用点不许写字面量
#pragma once

#include <string_view>

namespace GyanisScribe::Settings
{

    /// 截图的全局热键文本，例如 Ctrl+Alt+A
    inline constexpr std::string_view kHotkeyCapture = "Hotkey/Capture";

    /// 录屏的全局热键文本，例如 Ctrl+Alt+R
    inline constexpr std::string_view kHotkeyRecord = "Hotkey/Record";

    /// OCR 的全局热键文本，例如 Ctrl+Alt+O
    inline constexpr std::string_view kHotkeyOcr = "Hotkey/Ocr";

    /// 屏幕取色的全局热键
    inline constexpr std::string_view kHotkeyPick = "Hotkey/Pick";

    /// 主题档位，取值只能是 Light、Dark、System
    inline constexpr std::string_view kThemeMode = "Theme/Mode";

    /// 界面语言，取值只能是 zh 与 en；en 需要随包的语言包，见 Ui/Language.h
    inline constexpr std::string_view kUiLanguage = "Ui/Language";

    /// 上一次成功截图的选区，文本形如「x,y,width,height」；写方与读方都在 Capture 模块
    inline constexpr std::string_view kCaptureLastRegion = "Capture/LastRegion";

    /// 上一次成功截图所在屏幕的名字，「上次区域」要回到那一块屏上
    inline constexpr std::string_view kCaptureLastScreen = "Capture/LastScreen";

    /// 贴图保存的默认目录，为空时用系统图片目录下的 GyanisScribe
    inline constexpr std::string_view kPathScreenshot = "Paths/Screenshot";

    /// 录屏成品的默认保存目录
    inline constexpr std::string_view kPathRecording = "Paths/Recording";

    /// 贴图的布局记录（JSON 数组文本），重启后按它恢复摆位、缩放、旋转与透明度
    inline constexpr std::string_view kPinLayout = "Pin/Layout";

    /// OCR 语言标签，取值见 Ocr::supportedOcrLanguageTags()
    inline constexpr std::string_view kOcrLanguage = "Ocr/Language";

    /// OCR 语言包（tessdata）目录；为空时按环境变量与程序数据目录找
    inline constexpr std::string_view kOcrTessDataDirectory = "Ocr/TessDataDirectory";

    /// 取色复制用的写法：Hex、Rgb 或 Hsl
    inline constexpr std::string_view kPickFormat = "Pick/Format";

    /// 历史最多留多少条，0 表示不限条数
    inline constexpr std::string_view kHistoryMaximumEntries = "History/MaximumEntries";

    /// 历史最久留多少天，0 表示不限时长
    inline constexpr std::string_view kHistoryRetentionDays = "History/RetentionDays";

    /// 标注色板的最近使用颜色（#RRGGBB 列表，最新的在前），读写都在 Annotation::AnnotationToolbar
    inline constexpr std::string_view kAnnotationRecentColors = "Annotation/RecentColors";

    /// 标注的默认线宽，取值必须是 Ui::kAnnotationWidths 里的一档；工具栏起点与持久化都读它
    inline constexpr std::string_view kAnnotationDefaultWidth = "Annotation/DefaultWidth";

    /// 标注的默认字号，取值必须是 Ui::kFontSizes 里的一档
    inline constexpr std::string_view kAnnotationDefaultFontSize = "Annotation/DefaultFontSize";

} // namespace GyanisScribe::Settings
