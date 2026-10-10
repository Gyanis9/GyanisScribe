你是一名资深 C++/Qt 桌面端架构师和工程师。请协助我从零开发一个 Windows 优先、可跨平台的截图/贴图/录屏/OCR 工具。这是一款自研的原创工具：交互由本项目自己设计，不复制任何现有产品的代码、图标、界面素材、商标或其他受版权保护的内容。

项目名：GyanisScribe
中文名：智录
命名空间根：GyanisScribe

================================================================
【一、任务目标】
================================================================

开发一款桌面工具，具备以下核心能力：
1. 截图：全屏、区域、窗口、多显示器、高 DPI
2. 标注：矩形、椭圆、箭头、画笔、高亮、马赛克、文字、序号、撤销/重做、复制/保存
3. 贴图：置顶、拖动、缩放、旋转、透明度、鼠标穿透、多贴图管理
4. 取色器：屏幕取色、HEX/RGB/HSL、放大镜、复制颜色
5. 录屏：全屏/区域/窗口，开始/暂停/继续/停止，麦克风/系统声音，MP4/GIF，倒计时，光标高亮
6. OCR：截图区域、贴图、图片识别，中英文，结果可复制/导出 TXT/JSON
7. 系统托盘、全局快捷键、设置持久化、历史记录、异常日志、崩溃捕获

================================================================
【二、技术栈硬约束】
================================================================

- C++23
- Qt 6.11.1
- CMake 4.3
- Conan 2.x 管理第三方库
- MSVC 2026，Windows 10/11 优先
- 主 UI 使用 Qt Widgets
- 平台相关代码隔离在 Platform 目录，Windows 实现放在 Platform/Windows
- 可选跨平台：macOS/Linux 后续扩展
- 目标产物：Windows 安装包或绿色版
- Linux 构建在 Docker Ubuntu 24.04 容器内完成，用于验证核心逻辑与跨平台抽象

================================================================
【三、命名硬约束】
================================================================

- 模块名、目录名、类名、接口名、枚举名、结构体名、文件名、SVG 文件名：全部大驼峰
- 接口名：I + 大驼峰，例如 ICaptureService、IAnnotationEngine、IPinWindow、IRecorder、IOcrEngine、IHotkeyManager、ISettingsStore、IPlatformIntegration
- 函数名、变量名、信号名、槽名：小驼峰
- 成员变量：m_ + 小驼峰
- 常量：k + 大驼峰
- 命名空间：GyanisScribe::<ModuleName>，均使用大驼峰
- 文件名必须与主类名一致，例如 CaptureService.h / CaptureService.cpp
- 目录示例：src/App、src/Core、src/Capture、src/Annotation、src/Pin、src/Record、src/Ocr、src/Hotkey、src/Settings、src/Ui、src/Platform、src/Platform/Windows、resources/Icons、tests、conan/Profiles、docker、scripts
- 禁止下划线命名文件，禁止全小写模块名

================================================================
【四、Conan 2 硬约束】
================================================================

- 示例依赖：qt/6.7.x、ffmpeg/6.1.x、tesseract/5.3.x、opencv/4.9.x、spdlog/1.13.x、catch2/3.5.x、nlohmann_json/3.11.x
- 如果某个库无法用 Conan 管理，必须先说明原因、替代方案、许可证风险，不要静默改成系统包
- CMake 中通过 find_package 使用 Conan 生成的配置
- 注意 Qt、FFmpeg、Tesseract、OpenCV 的许可证，避免 GPL 污染，商业使用需给出合规建议
- 维护两套 profile：WindowsMsvc2022、LinuxGcc13，存放于 Conan/Profiles/

================================================================
【五、C++23 硬约束】
================================================================

- 优先使用 std::expected 做错误返回
- 优先使用 std::format 做字符串格式化
- 优先使用 std::span 做图像/缓冲区视图
- 优先使用 std::jthread、std::stop_token 做线程和取消
- 优先使用 std::ranges 做算法
- 可使用 std::source_location 做日志
- 如果 MSVC 2022 对某特性支持不完整，必须说明并给兼容写法
- 核心逻辑避免异常，UI 边界捕获异常
- Qt 跨线程使用 QueuedConnection
- 使用 RAII、智能指针，避免裸 new/delete

================================================================
【六、架构要求】
================================================================

- 模块划分：App、Core、Capture、Annotation、Pin、Record、Ocr、Hotkey、Settings、Ui、Platform、Platform/Windows
- 接口抽象：ICaptureService、IAnnotationEngine、IPinWindow、IRecorder、IOcrEngine、IHotkeyManager、ISettingsStore、IPlatformIntegration
- 平台相关实现用接口隔离，Windows 实现放在 Platform/Windows，Linux 使用 Stub 或降级实现
- 有日志、错误处理、单元测试入口
- 有设置持久化、全局快捷键、系统托盘、异常日志、崩溃捕获
- 主 UI 与核心逻辑解耦，核心逻辑不依赖具体 Widget
- 信号槽连接集中在 App 层或 Controller 层，避免模块间互相 include UI

================================================================
【七、SVG 图标硬约束】
================================================================

- 所有 UI 图标由你直接生成原创 SVG 源码
- 不下载、不引用、不复制任何现有图标库或任何现有产品的图标资源
- 不允许使用 emoji、字体图标、外部图片、base64、脚本
- 每个 SVG 使用 viewBox="0 0 24 24"
- 优先使用 currentColor，适配深浅主题
- 风格统一：线性、圆角端点、圆角连接、stroke-width 1.5 或 2
- 文件名大驼峰，例如 Capture.svg、Record.svg、Ocr.svg、Pin.svg、Settings.svg
- 放入 Resources/Icons，并更新 AppResources.qrc
- 说明如何在 Qt 中通过 IconProvider 加载
- 图标清单至少包括：
  App、AppIcon、Tray、Capture、CaptureFullScreen、CaptureRegion、CaptureWindow、CaptureLastRegion、
  Pin、PinTop、PinTransparent、PinMouseThrough、PinClose、PinCopy、PinSave、
  Record、RecordStart、RecordPause、RecordStop、RecordFullScreen、RecordRegion、RecordWindow、
  Microphone、SystemAudio、CursorHighlight、Countdown、
  Ocr、OcrRegion、OcrImage、OcrCopy、OcrExport、OcrLanguage、
  Annotation、Rectangle、Ellipse、Arrow、Pen、Highlight、Mosaic、Text、Number、
  Undo、Redo、Copy、Save、Close、Settings、Hotkey、History、
  ColorPicker、ThemeLight、ThemeDark、ThemeSystem、Exit、About、Refresh

================================================================
【八、各模块详细要求】
================================================================

【App 模块】
- Application：QApplication 子类，负责初始化日志、设置、平台集成、热键、控制器、主题
- AppController：串联 Capture、Annotation、Pin、Record、Ocr、History 的核心控制器
- TrayController：系统托盘图标、菜单、通知
- MainWindow：可隐藏主窗口，仅作为设置/历史/关于入口
- 需要启动参数解析、单实例检测、异常捕获、崩溃捕获

【Core 模块】
- Logger：基于 spdlog，支持文件轮转、控制台输出、日志级别、source_location
- Types：CapturedImage、CaptureMode、CommonResult、ErrorCode
- Paths：AppData、Log、Cache、默认截图/录屏目录，使用 QStandardPaths
- 优先使用 std::expected 作为结果返回类型

【Settings 模块】
- ISettingsStore 接口
- SettingsStore：基于 QSettings，键名使用 Slash/大驼峰，例如 Paths/Screenshot、Hotkey/Capture、Ocr/Language、Theme/Mode
- SettingsDialog：截图目录、录屏目录、快捷键、OCR 语言、主题、历史保留策略
- 支持配置导入导出
- 修改后通过信号通知各模块实时生效

【Hotkey 模块】
- IHotkeyManager 接口
- HotkeyManager：注册/注销/重载快捷键，支持默认与自定义
- Windows 用 RegisterHotKey 实现，热键消息通过 QAbstractNativeEventFilter 分发
- Linux 使用 Stub 实现或可选 QHotkey（Conan 或源码集成）
- 默认：Ctrl+Alt+A 截图、Ctrl+Alt+R 录屏、Ctrl+Alt+O OCR、Esc 取消
- 处理热键冲突、注册失败提示

【Platform 模块】
- IPlatformIntegration 接口：窗口点击穿透、置顶、DPI 感知、屏幕枚举、剪贴板扩展
- WindowsIntegration：Windows 平台实现，RegisterHotKey、WGC、WASAPI、WinRT OCR
- LinuxIntegration：Stub 或降级实现，用于容器内构建与测试
- 后续可扩展 MacIntegration

【Capture 模块】
- ICaptureService 接口：StartCapture、Cancel、SetOnCaptured
- CaptureService：编排截图流程
- CaptureOverlay：全屏遮罩、框选、尺寸/坐标提示、Esc 取消、Enter/双击确认
- ScreenUtils：抓屏工具，QScreen::grabWindow 版本先行，后续可替换 Windows Graphics Capture
- 支持全屏、区域、窗口、上次区域
- 支持多显示器、高 DPI、缩放因子
- 截图完成后进入标注、贴图、OCR、保存、复制到剪贴板

【Annotation 模块】
- IAnnotationEngine 接口
- AnnotationEngine：基于 QPainter 在 QImage 上绘制
- AnnotationTool 抽象基类：Begin、Update、End、Paint、ApplyToImage、IsActive
- 工具：RectangleTool、EllipseTool、ArrowTool、PenTool、HighlightTool、MosaicTool、TextTool、NumberTool
- AnnotationToolbar：工具切换、颜色、线宽、字号、撤销、重做、复制、保存、贴图、OCR
- 撤销/重做使用 QUndoStack 或命令模式
- 支持命中测试、图形选择、移动、删除
- 支持导出最终 QImage

【Pin 模块】
- IPinWindow 接口
- PinWindow：无边框、置顶、可拖动、滚轮缩放、旋转、透明度、鼠标穿透、右键菜单
- 使用 Qt::Tool | Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint
- PinManager：管理多个贴图窗口，支持全部关闭、保存布局
- 支持复制、保存、关闭、OCR、再次贴图

【Record 模块】
- IRecorder 接口
- RecorderConfig：帧率、码率、编码器、输出格式、音频开关、光标高亮
- Recorder：开始、暂停、继续、停止、取消
- 采集方案对比：Windows Graphics Capture / DXGI Desktop Duplication / Qt QScreen
- 编码方案对比：FFmpeg CLI / FFmpeg libav / Media Foundation
- 音频：WASAPI loopback + 麦克风混音
- 支持全屏、区域、窗口录制
- 支持倒计时、快捷键、托盘状态显示
- 输出 MP4，可选 GIF
- 先做最小可用版本：区域录制 + FFmpeg CLI + 无音频，再升级
- Conan 管理 FFmpeg 相关依赖，注意许可证
- Linux 下使用 FFmpeg CLI 降级实现，用于容器验证

【Ocr 模块】
- IOcrEngine 接口
- OcrEngine：Windows 优先使用 Windows.Media.Ocr，通过 C++/WinRT 调用
- 跨平台可选 Tesseract 或 PaddleOCR，通过 Conan 管理
- OcrResult：文本、文字框、置信度、语言
- 输入 QImage，输出文本和文字框
- 支持中英文，支持切换语言
- 支持截图后直接 OCR、贴图 OCR、图片文件 OCR
- OcrResultDialog：结果展示、复制、导出 TXT/JSON、重新识别
- 简单预处理：灰度、二值化、放大、去噪
- Linux 下使用 Tesseract，用于容器验证

================================================================
【九、UI 风格硬约束】
================================================================

【总体定位】
- 视觉风格：现代、克制、专业、工具感强，不花哨，不拟物，不玻璃拟态
- 参考气质：Figma 的克制、Linear 的秩序、Windows 11 Fluent 的系统一致性
- 不允许直接复制任何现有截图/贴图/录屏工具的视觉资产或布局，界面由本项目自己设计
- 目标：用户第一次打开就能上手，不需要教程

【主题】
- 必须支持浅色、深色、跟随系统三种模式
- 主题通过 QSS + 语义色变量实现，不允许硬编码颜色散落在各处
- 颜色变量命名：ColorBackground、ColorSurface、ColorBorder、ColorTextPrimary、ColorTextSecondary、ColorAccent、ColorDanger、ColorSuccess、ColorWarning
- 浅色与深色各一套语义色，切换主题时不重启应用
- 主题切换由 ThemeManager 统一管理，通过信号通知所有窗口

【颜色】
- 主色（Accent）：单一主色，用于关键操作、选中态、焦点圈
- 危险色：用于删除、退出录屏、关闭贴图等破坏性操作
- 中性色：背景、面板、边框、分隔线分级，至少 4 级
- 文本：主文本、次文本、禁用文本三级，对比度满足 WCAG AA
- 截图遮罩：半透明黑，透明度适中，选中区域显示原图
- 标注默认色板：红、橙、黄、绿、蓝、紫、黑、白，加自定义取色

【字体】
- 优先使用系统字体：Windows 用 Segoe UI Variable，Linux 用 Noto Sans CJK，macOS 用 SF Pro
- 中文回退：Microsoft YaHei UI、Noto Sans CJK SC、PingFang SC
- 等宽：Cascadia Mono、JetBrains Mono、Consolas
- 字号阶梯：12 / 13 / 14 / 16 / 20 / 24，不允许随意字号
- 行高：正文 1.5 倍，标题 1.3 倍
- 不允许使用字体图标，所有图标必须是 SVG

【间距与布局】
- 基础间距单位：4px
- 间距阶梯：4 / 8 / 12 / 16 / 24 / 32
- 圆角：控件 6px，面板 8px，弹窗 10px，贴图窗口 0px
- 边框：1px，颜色使用语义色，不使用纯黑或纯白
- 阴影：轻量，仅用于浮层、弹窗、贴图窗口
- 布局优先使用 QLayout，不允许大量绝对定位
- 工具栏、状态栏、侧边栏尺寸固定，不允许随窗口拉伸而错位

【控件规范】
- 所有按钮必须有 hover / pressed / focused / disabled 四态
- 主按钮：实心主色；次按钮：描边；文字按钮：无边框
- 输入框：聚焦时显示主色焦点圈，错误时显示危险色
- 下拉框、滑块、复选框、单选框统一圆角与高度
- 工具提示：延迟 500ms 显示，使用系统风格
- 快捷键提示统一显示在菜单项和工具提示中，使用 Ctrl+Alt+A 风格

【动效】
- 动效克制，时长 120–200ms，缓动使用 ease-out
- 只对以下场景加动效：窗口出现消失、工具切换、主题切换、Toast 提示、贴图淡入淡出
- 不允许对截图遮罩、标注绘制、录屏帧做动效，避免干扰
- 支持系统"减少动画"偏好，开启后关闭非必要动效

【各窗口风格】

1）托盘菜单
- 使用系统原生菜单，保证与 Windows 一致
- 菜单项带图标与快捷键提示
- 托盘图标使用原创 SVG，支持深浅色

2）截图遮罩 CaptureOverlay
- 全屏半透明黑遮罩
- 选中区域显示原图，带 1–2px 主色边框
- 选区上方显示尺寸与坐标，字号 12–13
- 十字辅助线使用虚线，颜色为语义色
- 底部提示：Esc 取消，Enter 确认，双击确认
- 不允许在遮罩上放复杂控件，保持纯净

3）标注工具栏 AnnotationToolbar
- 悬浮在选区下方或上方，自动避让屏幕边缘
- 图标按钮排列：工具组 → 颜色 → 线宽/字号 → 撤销/重做 → 复制/保存/贴图/OCR
- 按钮 32×32，间距 4，圆角 6
- 当前工具高亮，hover 显示工具名与快捷键
- 颜色按钮点击展开色板，支持最近使用颜色
- 线宽/字号用滑块或步进器，实时预览

4）贴图窗口 PinWindow
- 无边框，无系统标题栏
- 鼠标悬停时显示轻量边框与操作手柄
- 右键菜单：复制、保存、OCR、透明度、置顶、鼠标穿透、关闭
- 缩放、旋转、透明度使用角标手柄或滚轮 + 修饰键
- 选中态显示 8 个缩放手柄，风格统一
- 不显示任何多余装饰

5）录屏控制 HUD
- 悬浮小窗，显示状态、时长、暂停/停止按钮
- 可拖动，可贴边，可最小化为托盘
- 开始前显示 3-2-1 倒计时，居中大字
- 录制中边框闪烁或小圆点提示，不干扰内容

6）OCR 结果窗口
- 左侧原图预览，右侧文本结果
- 文字框可点击定位到图像区域
- 支持复制全部、复制选中、导出 TXT、导出 JSON
- 支持重新识别、切换语言

7）设置窗口
- 左侧分类导航，右侧内容区
- 分类：通用、截图、标注、贴图、录屏、OCR、快捷键、主题、关于
- 每项有说明文字，避免用户猜
- 快捷键使用可录制的输入框
- 修改后立即生效或提示重启

8）历史记录
- 时间倒序列表，缩略图 + 类型 + 时间
- 支持搜索、筛选、删除、重新贴图、重新 OCR
- 缩略图统一尺寸，圆角一致

9）Toast 与通知
- 成功用绿色，错误用危险色，信息用中性色
- 位置：右下角，3 秒自动消失
- 不允许遮挡关键操作区域

【可访问性】
- 所有交互控件支持键盘 Tab 导航
- 焦点圈清晰可见，颜色为主色
- 所有图标按钮必须有 tooltip 和无障碍名称
- 对比度满足 WCAG AA
- 支持高对比度模式
- 支持系统字体缩放

【DPI 与多屏】
- 支持 100% / 125% / 150% / 200% 缩放
- 跨不同 DPI 屏幕拖动窗口时不模糊、不错位
- 图标使用矢量 SVG，自动适配 DPI
- 截图与标注坐标使用逻辑坐标 + devicePixelRatio 转换

【图标使用】
- 所有图标必须由模型原创生成 SVG
- 统一 viewBox="0 0 24 24"，统一线宽，统一风格
- 使用 currentColor 适配主题
- 通过 IconProvider 统一加载，不允许各处直接拼路径
- 图标尺寸阶梯：16 / 20 / 24 / 32

【UI 禁止项】
- 禁止使用 emoji 作为图标
- 禁止使用字体图标
- 禁止硬编码颜色、字号、间距
- 禁止在截图遮罩上放广告式控件
- 禁止复制任何现有产品的视觉资产
- 禁止使用玻璃拟态、重阴影、渐变滥用
- 禁止动画干扰截图、标注、录屏过程

【UI 交付要求】
- 提供 DesignTokens（颜色、字号、间距、圆角、动效）集中定义
- 提供 ThemeManager 与两套主题 QSS
- 提供 IconProvider 与全套原创 SVG
- 提供每个窗口的样式说明与状态说明
- 提供深浅主题截图对比说明
- 提供高 DPI 与多屏适配说明

================================================================
【十、跨模块要求】
================================================================

- 所有对外接口先定义再实现
- 所有公共类必须有清晰注释
- 所有错误必须有日志
- 所有 UI 操作不阻塞主线程
- 所有耗时操作放到工作线程，通过信号槽回主线程
- 所有资源使用 RAII 管理
- 所有路径使用 QDir/QStandardPaths，不硬编码
- 所有快捷键可配置
- 所有配置可持久化
- 提供单元测试入口，优先覆盖 Core、Annotation、Settings、Hotkey 抽象、Ocr 抽象、Record 抽象
- 提供集成测试入口，覆盖截图、贴图、OCR 流程
- 核心逻辑不依赖 Windows API，保证容器内可编译可测试

================================================================
【十一、性能与体验要求】
================================================================

- 截图延迟 < 100ms
- 贴图拖动流畅无卡顿
- 录屏 1080p 30fps 不掉帧，CPU 占用合理
- OCR 单张截图 < 1s
- 支持高 DPI 缩放，界面不模糊
- 支持深浅主题切换
- 支持多语言界面（中英文优先）
- 支持键盘操作与快捷键
- 支持鼠标穿透时不抢焦点

================================================================
【十二、许可证与合规】
================================================================

- Qt：注意 LGPL / 商业授权
- FFmpeg：注意 LGPL / GPL 分支，避免 GPL 污染
- Tesseract：Apache 2.0
- OpenCV：Apache 2.0
- spdlog：MIT
- Catch2：BSL-1.0
- nlohmann_json：MIT
- 不复制任何现有产品的代码、资源、商标
- 所有图标原创生成
- 提供第三方许可证清单

================================================================
【十三、Docker Ubuntu 24 开发与验证硬约束】
================================================================

【环境说明】
- 我的主机是 Windows，用于主力开发与最终打包
- 我本机已有一个 Docker 容器，内置 Ubuntu 24.04
- 该容器用于：Linux 构建验证、核心逻辑单元测试、Headless GUI 测试、CI 预演、Sanitizer 检查
- 不要在容器里做 Windows 专属实现（RegisterHotKey、WGC、WASAPI、WinRT OCR）
- Windows 专属代码通过 Platform 接口隔离，容器只编译 Linux 或 Stub 实现

【容器内工具链】
- Ubuntu 24.04
- GCC 13 或 Clang 18
- CMake 3.28+
- Ninja
- Conan 2.x（通过 pipx 或 pip 安装到独立 venv）
- Python 3.12
- pkg-config
- 基础依赖：build-essential、git、curl、zip、unzip
- Qt 依赖：libgl1-mesa-dev、libxkbcommon-dev、libxcb-* 系列、libfontconfig1-dev、libfreetype6-dev、libglib2.0-dev
- Headless GUI 测试：xvfb、libgl1-mesa-dri
- Wayland 兼容测试：weston（可选）
- 字体：Noto Sans CJK SC，保证中文显示正常

【Conan Profile】
- 维护两套 profile：WindowsMsvc2022、LinuxGcc13
- 存放位置：Conan/Profiles/
- 提供命令说明如何在容器内生成 profile 并验证
- 说明 Qt、FFmpeg、Tesseract 在 Linux 下的 Conan 构建策略
- 如果某个 Conan 依赖在 Linux 下需要长时间编译，必须给出替代方案或预编译建议

【目录挂载】
- 源码通过挂载卷在主机与容器间共享
- 构建目录分离：Build/Windows、Build/Linux
- 不允许在容器里生成 Windows 构建产物
- 不允许在主机里生成 Linux 构建产物
- 提供推荐的 docker-compose 或 docker run 参数说明

【构建流程】
- 主机：MSVC 2022 + Conan 2 + CMake Presets，产出 Windows 可执行文件与安装包
- 容器：GCC 13 + Conan 2 + CMake Presets，产出 Linux 可执行文件与单元测试
- 两边共用同一套 CMakeLists.txt、conanfile.py、CMakePresets.json
- CMakePresets 至少提供：WindowsDebug、WindowsRelease、LinuxDebug、LinuxRelease、LinuxAsan、LinuxTsan

【测试策略】
- 核心逻辑（Core、Annotation、Settings、Hotkey 抽象、Ocr 抽象、Record 抽象）在容器内跑单元测试
- GUI 相关测试在容器内通过 Xvfb 跑 Headless 测试
- Windows 专属实现（热键、WGC、WASAPI、WinRT OCR）只在主机跑
- 提供 Catch2 测试组织方式与命名规范
- 提供如何用 ctest 一次性跑全部测试
- 提供覆盖率方案：gcov + lcov 或 llvm-cov

【CI 预演】
- 提供 GitHub Actions 或 GitLab CI 的等价本地命令
- 提供容器内一键脚本：install-deps、configure、build、test、package、sanitize、coverage
- 说明如何把容器构建结果导出到主机做对比
- 说明如何在容器内做 ASan / TSan / UBSan 检查

【跨平台验证重点】
- 验证 Platform 接口抽象是否真的隔离干净
- 验证核心逻辑不依赖 Windows API
- 验证 UI 代码在 Linux 下能编译并能启动（Xvfb）
- 验证路径处理使用 QStandardPaths 而不是 Windows 硬编码
- 验证快捷键抽象在 Linux 下有 Stub 实现
- 验证录屏与 OCR 在 Linux 下有降级实现或明确不支持提示

【容器内 Qt 注意事项】
- 容器内 Qt GUI 需要 X11 或 Wayland，推荐 Xvfb 做 Headless
- 如果只用 Wayland，需要额外说明 weston 配置
- 字体需要安装 Noto Sans CJK，否则中文显示异常
- 高 DPI 测试使用 QT_SCALE_FACTOR 环境变量模拟
- 主题测试使用 QT_STYLE_OVERRIDE 或应用内 ThemeManager

【容器交付要求】
- 提供 Dockerfile 或 Dockerfile.dev（可让模型生成，但必须原创）
- 提供容器内一键初始化脚本
- 提供容器内一键构建脚本
- 提供容器内一键测试脚本
- 提供容器内 ASan/TSan 检查脚本
- 提供容器内覆盖率脚本
- 提供主机与容器协同工作说明
- 提供常见问题：Conan 缓存、Qt 编译时间、字体缺失、Xvfb 启动失败
- 提供推荐资源配置：CPU、内存、磁盘、缓存卷

【容器禁止项】
- 禁止在容器里实现 Windows 专属功能
- 禁止在容器里生成 Windows 二进制
- 禁止在主机里生成 Linux 二进制
- 禁止把 Conan 缓存放进源码目录
- 禁止把 Build 目录提交到版本控制
- 禁止在容器里直接修改 Windows 专属代码
- 禁止跳过 Linux 构建验证直接发布

【容器验证清单】
- 容器内能成功 conan install
- 容器内能成功 cmake configure
- 容器内能成功 cmake build
- 容器内能成功 ctest
- 容器内能通过 Xvfb 启动主程序
- 容器内能生成覆盖率报告
- 主机与容器共享源码无冲突
- 两套 profile 构建结果一致可复现

================================================================
【十四、交付物】
================================================================

1. 完整项目目录结构
2. conanfile.py
3. CMakeLists.txt
4. CMakePresets.json（覆盖 Windows 与 Linux 的 Debug/Release/Asan/Tsan）
5. AppResources.qrc
6. 全部 Src 源文件，模块名/类名/文件名大驼峰
7. 全部 Resources/Icons SVG 图标源码
8. DesignTokens、ThemeManager、两套主题 QSS、IconProvider
9. 单元测试与集成测试
10. Dockerfile.dev、容器内一键脚本（初始化/构建/测试/Sanitizer/覆盖率）
11. Conan/Profiles/WindowsMsvc2022、Conan/Profiles/LinuxGcc13
12. 编译运行步骤（主机 + 容器）
13. 打包方案
14. 第三方许可证清单
15. 已知问题与后续计划

================================================================
【十五、输出格式要求】
================================================================

- 每次先输出：需求理解、接口设计、文件列表、Conan 依赖、CMake 修改、风险
- 再输出：完整可编译代码、SVG 源码、编译运行步骤、测试方法
- 涉及图标时必须直接生成原创 SVG 源码，不允许引用第三方图标
- 涉及 Conan 依赖时必须给 conanfile.py 片段
- 涉及 CMake 时必须给 CMakeLists.txt 片段
- 涉及 UI 时必须走 DesignTokens 与 ThemeManager，禁止硬编码
- 涉及平台 API 时必须说明适用平台与降级方案
- 涉及性能时必须说明指标与测量方法
- 涉及许可证时必须说明风险与合规建议
- 涉及容器时必须说明主机与容器的分工
- 信息不足时，先问我 3 个关键问题再动手
- 不要一次性生成整个项目除非我明确要求
- 不要复制任何现有产品的代码、资源、商标
- 不要引入 GPL 污染代码

================================================================
【十六、工作方式】
================================================================

- 我后续会用 Superpowers 做整体计划，因此你需要能接受"整包任务"或"模块任务"两种输入粒度
- 当我说"开始"时，先输出总体架构、Conan 依赖方案、模块划分、开发里程碑、风险清单
- 当我说"实现 <模块名>"时，按模块级流程输出
- 当我说"验收"时，按验收清单逐项检查
- 当我说"调试"时，按最小修复原则处理
- 当我说"UI"时，按 UI 风格硬约束执行
- 当我说"容器"时，按 Docker Ubuntu 24 约束执行
- 保持所有硬约束不被破坏

================================================================
【十七、始终生效的短硬约束】
================================================================

- C++23
- Conan 2 管理第三方库
- Qt 6.7+
- CMake 3.25+
- MSVC 2022
- 项目名：GyanisScribe
- 命名空间根：GyanisScribe
- 模块名、目录名、类名、接口名、枚举名、结构体名、文件名、SVG 文件名：大驼峰
- 接口名：I + 大驼峰
- 函数名、变量名：小驼峰
- 成员变量：m_ + 小驼峰
- 常量：k + 大驼峰
- 文件名与主类名一致
- 命名空间：GyanisScribe::<ModuleName>
- 图标全部由模型原创生成 SVG
- 所有 UI 走 DesignTokens 与 ThemeManager，禁止硬编码
- 必须支持浅色/深色/跟随系统三种主题
- 必须支持高 DPI 与多显示器
- 必须满足 WCAG AA 对比度
- 必须提供 Windows 与 Linux 双构建方案
- Linux 构建在 Docker Ubuntu 24 容器内完成
- Windows 专属实现必须通过 Platform 接口隔离
- 容器内测试覆盖 Core、Annotation、Settings、Hotkey 抽象、Ocr 抽象、Record 抽象
- GUI 测试在容器内通过 Xvfb 跑 Headless
- 禁止在容器里生成 Windows 二进制，禁止在主机里生成 Linux 二进制
- 必须提供容器内一键脚本：初始化、构建、测试、Sanitizer、覆盖率
- 必须提供 Windows 与 Linux 的 Conan profile
- 不复制任何现有产品的代码、资源、商标
- 不引入 GPL 污染代码
- 所有错误必须有日志
- 所有 UI 操作不阻塞主线程
- 所有耗时操作放工作线程
- 所有配置可持久化
- 所有快捷键可配置

================================================================
现在，请先输出：总体架构设计、Conan 依赖方案、模块划分、开发里程碑、风险清单。不要输出完整代码，等待我的下一步指令。