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

    /// 上一次成功截图的选区，文本形如「x,y,width,height」；写方与读方都在 Capture 模块
    inline constexpr std::string_view kCaptureLastRegion = "Capture/LastRegion";

    /// 上一次成功截图所在屏幕的名字，「上次区域」要回到那一块屏上
    inline constexpr std::string_view kCaptureLastScreen = "Capture/LastScreen";

    /// 贴图保存的默认目录，为空时用系统图片目录下的 GyanisScribe
    inline constexpr std::string_view kPathScreenshot = "Paths/Screenshot";

    /// 贴图的布局记录（JSON 数组文本），重启后按它恢复摆位、缩放、旋转与透明度
    inline constexpr std::string_view kPinLayout = "Pin/Layout";

    /// OCR 语言标签，取值见 Ocr::supportedOcrLanguageTags()
    inline constexpr std::string_view kOcrLanguage = "Ocr/Language";

    /// OCR 语言包（tessdata）目录；为空时按环境变量与程序数据目录找
    inline constexpr std::string_view kOcrTessDataDirectory = "Ocr/TessDataDirectory";

} // namespace GyanisScribe::Settings
