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

} // namespace GyanisScribe::Settings
