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

    /// 滚动长截图的全局热键
    inline constexpr std::string_view kHotkeyCaptureLong = "Hotkey/CaptureLong";

    /// 收起/摆回全部贴图的全局热键
    inline constexpr std::string_view kHotkeyTogglePins = "Hotkey/TogglePins";

    /// 切到下一组贴图的全局热键
    inline constexpr std::string_view kHotkeyNextPinGroup = "Hotkey/NextPinGroup";

    /// 快速保存的全局热键：截图后直接存成文件并复制，不走「截完之后」那一档
    inline constexpr std::string_view kHotkeyQuickSave = "Hotkey/QuickSave";

    /// 主题档位，取值只能是 Light、Dark、System、HighContrast
    inline constexpr std::string_view kThemeMode = "Theme/Mode";

    /// 界面语言，取值只能是 zh 与 en；en 需要随包的语言包，见 Ui/Language.h
    inline constexpr std::string_view kUiLanguage = "Ui/Language";

    /// 上一次成功截图的选区，文本形如「x,y,width,height」；写方与读方都在 Capture 模块
    inline constexpr std::string_view kCaptureLastRegion = "Capture/LastRegion";

    /// 上一次成功截图所在屏幕的名字，「上次区域」要回到那一块屏上
    inline constexpr std::string_view kCaptureLastScreen = "Capture/LastScreen";

    /**
     * @brief 最近若干次截图的选区历史
     * @details 每条「x,y,width,height,屏名」，段间以 ';' 相连、最近的一条在最前；上限与去重见 Capture/RegionHistory.h。
     *          框选时的 R / Shift+R 在这份历史里循环挑，「重复上次选区」与录屏的「上次选区」取第一条。
     *          上面那两把单值键由同一个写点照写一份（记录 = 历史首条），老读法不受升级影响。
     */
    inline constexpr std::string_view kCaptureRegionHistory = "Capture/RegionHistory";

    /**
     * @brief 框选时锁住的长宽比，形如「16:9」；空串表示没锁
     * @details 遮罩上按 A 把当前选区的比例锁上、再按一次解开；锁着的时候它跨轮次记住
     *          （下次开截图还是这一档），解开就清空——用户明确松开的档不该在下一轮里复活。
     *          读端在 Capture::CaptureService（开遮罩时装进去），写端跟着遮罩的信号落盘。
     */
    inline constexpr std::string_view kCaptureAspectRatio = "Capture/AspectRatio";

    /// 截完之后做什么，取值词表在 App::kCaptureIntentChoices；读端在 AppController，写端在设置的截图页
    inline constexpr std::string_view kCaptureAfterAction = "Capture/AfterAction";

    /// 按下截图后等几秒再开始（0 到 60，0 表示不延时）：用来截菜单、提示这类一碰就没的状态
    inline constexpr std::string_view kCaptureDelaySeconds = "Capture/DelaySeconds";

    /// 截图要不要把鼠标指针画进去，读端在 Capture::CaptureService，写端在设置的截图页；
    /// 只有整屏与区域截图吃这一格，滚动长截图按帧接图，画进去会在长图上留下一串指针
    inline constexpr std::string_view kCaptureIncludeCursor = "Capture/IncludeCursor";

    /// 「没填」那一档取不含指针：区域截图刚松手时指针就在选区里，默认画进去会让成品多出一只箭头
    inline constexpr bool kDefaultCaptureIncludeCursor = false;

    /// 区域截图的成品要不要把四角切圆（读端在 Capture::CaptureService，写端在设置的截图页）；
    /// 只作用在区域这一路：整屏、窗口与滚动长截图的成品形状改起来各有各的意外，不做
    inline constexpr std::string_view kCaptureRoundCorners = "Capture/RoundCorners";

    /// 「圆角截图」没填时的默认档：不切
    inline constexpr bool kDefaultCaptureRoundCorners = false;

    /// 每张截图都顺手存一份进截图目录（不进标注、不弹窗）；默认关，开了之后每次截图都会在盘上留一份
    inline constexpr std::string_view kCaptureAutoSave = "Capture/AutoSave";

    /// 截图完成时响一声系统提示音；默认关
    inline constexpr std::string_view kCaptureSound = "Capture/Sound";

    /// 成品出程序（存文件或交剪贴板）时加的装饰，两格独立；默认都关，装饰是「要的时候才有」的东西
    inline constexpr std::string_view kOutputBorder = "Save/OutputBorder";
    inline constexpr std::string_view kOutputShadow = "Save/OutputShadow";

    /// 成品图片存成什么格式，取值词表在 Core::kImageFormatChoices；读端在 AppController 与 Pin 的保存那两处
    inline constexpr std::string_view kSaveImageFormat = "Save/ImageFormat";

    /// 有损档（JPG 与 WebP）的质量，取值区间见 Core::kMinimumImageQuality 到 kMaximumImageQuality；
    /// PNG 是无损的，这一格对它没有作用，读端与「保存格式」那两格同处
    inline constexpr std::string_view kSaveImageQuality = "Save/ImageQuality";

    /// 成品文件名的模板，变量只有 %date%、%time%、%ms%（Core::Paths::parseNameTemplate 校验）；
    /// 留空表示默认档，读端见 Settings::nameTemplateOrDefault
    inline constexpr std::string_view kSaveNameTemplate = "Save/NameTemplate";

    /// 贴图保存的默认目录，为空时用系统图片目录下的 GyanisScribe
    inline constexpr std::string_view kPathScreenshot = "Paths/Screenshot";

    /// 录屏成品的默认保存目录
    inline constexpr std::string_view kPathRecording = "Paths/Recording";

    /// 贴图的布局记录（JSON 数组文本），重启后按它恢复摆位、缩放、旋转与透明度
    inline constexpr std::string_view kPinLayout = "Pin/Layout";

    /// 当前显示的是第几个贴图分组（从 1 起），与布局一起读写：重启后回到同一组
    inline constexpr std::string_view kPinActiveGroup = "Pin/ActiveGroup";

    /// 贴图分组的名字（JSON 对象文本，形如 {"2":"参照"}），与布局一起读写：没起过名字时这一格是空的
    inline constexpr std::string_view kPinGroupNames = "Pin/GroupNames";

    /// 「收起 / 摆回全部贴图」的那一档状态，与布局一起读写：退出时收着一整桌，回来还是收着
    inline constexpr std::string_view kPinAllConcealed = "Pin/AllConcealed";

    /// OCR 语言标签，取值见 Ocr::supportedOcrLanguageTags()
    inline constexpr std::string_view kOcrLanguage = "Ocr/Language";

    /// OCR 语言包（tessdata）目录；为空时按环境变量与程序数据目录找
    inline constexpr std::string_view kOcrTessDataDirectory = "Ocr/TessDataDirectory";

    /// 取色复制用的写法：Hex、Rgb、Hsl、Hsv、Hsi 或 Decimal（词表在 Pick/ColorFormat.h）
    inline constexpr std::string_view kPickFormat = "Pick/Format";

    /// 最近取过的颜色（#RRGGBB 列表，最新在前，上限见 Pick::kPickColorHistoryCapacity）；
    /// 屏幕取色与贴图取色共用这一份，取色遮罩底部的色块行按它摆
    inline constexpr std::string_view kPickRecentColors = "Pick/RecentColors";

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

    /// 开始录屏前的倒计时秒数（0 到 10，0 表示立即开始）；读端在 AppController 起录那一处，写端在设置的录屏页
    inline constexpr std::string_view kRecordCountdownSeconds = "Record/CountdownSeconds";

    /// 钉死的视频编码器名（取值来自 Record::kPreferredVideoEncoders），"Auto" 或空表示逐个探测自动选；
    /// 读端在 AppController 造参数那一处，写端在设置的录屏页，选型规则见 FfmpegVideoEncoders
    inline constexpr std::string_view kRecordVideoEncoder = "Record/VideoEncoder";

    /// 编码器下拉里「自动」那一项的取值：与「没填」区分开，用户能显式选回自动
    inline constexpr std::string_view kRecordVideoEncoderAutoToken = "Auto";

} // namespace GyanisScribe::Settings
