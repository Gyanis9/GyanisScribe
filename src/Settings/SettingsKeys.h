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

    /// 截完之后做什么，取值词表在 App::kCaptureIntentChoices；读端在 AppController，写端在设置的截图页
    inline constexpr std::string_view kCaptureAfterAction = "Capture/AfterAction";

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

    /// 新建贴图的默认不透明度（1 到 100 的百分比），读端在 Pin::PinWindow，写端在设置的贴图页
    inline constexpr std::string_view kPinDefaultOpacity = "Pin/DefaultOpacity";

    /// 录屏的默认帧率（1 到 Record::kMaximumFrameRate），读端在 AppController 造参数那一处，写端在设置的录屏页
    inline constexpr std::string_view kRecordDefaultFrameRate = "Record/DefaultFrameRate";

    /// 录屏输出容器：Mp4 / Gif，取值词表在 Record::kRecordFormatChoices；
    /// 读端在 AppController 造参数那一处，写端在设置的录屏页
    inline constexpr std::string_view kRecordFormat = "Record/Format";

    /// 录屏范围：整屏 / 上次选区 / 当前窗口，取值词表在 Record::kRecordScopeChoices；
    /// 读端在 AppController 造参数那一处，写端在设置的录屏页
    inline constexpr std::string_view kRecordScope = "Record/Scope";

    /// 录屏取声的麦克风那一路（dshow 的稳定标识），空表示不取这一路；
    /// 读端在 AppController 造参数那一处，写端在设置的录屏页
    inline constexpr std::string_view kRecordAudioMicrophone = "Record/AudioMicrophoneDevice";

    /// 录屏取声的系统声音那一路，取值形状与上一项相同
    inline constexpr std::string_view kRecordAudioSystemSound = "Record/AudioSystemSoundDevice";

    /// 录屏的画面码率，kbps（0 表示按质量优先，交给编码器的 CRF）；
    /// 读端在 AppController 造参数那一处，写端在设置的录屏页
    inline constexpr std::string_view kRecordVideoBitrate = "Record/VideoBitrateKbps";

    /// 录屏时是否把光标位置圈在帧上，读端在 AppController 造参数那一处，写端在设置的录屏页
    inline constexpr std::string_view kRecordCursorHighlight = "Record/CursorHighlight";

} // namespace GyanisScribe::Settings
