# 第三方组件与许可证

这份清单是「随软件分发或与之一起安装」的全部第三方件。图标、界面文案与代码都是本项目原创，
不含任何第三方美术资源；交互参考同类工具的做法，但没有复制其代码、图标、界面素材或商标。

## 随构建链接的组件

| 组件 | 版本 | 许可证 | 用途 | 分发方式 |
| --- | --- | --- | --- | --- |
| Qt 6（Core / Gui / Widgets / Concurrent / Svg / Network） | 主机 6.11.1，容器 6.4.2 | LGPL-3.0（或商业授权） | 界面、图像、SVG 图标、把一次文字识别搬到工作线程、本地套接字 | 动态链接 DLL / .so |
| spdlog | conandata.yml 里那份 | MIT | 日志 | 静态或动态由 Conan 决定 |
| GoogleTest | conandata.yml 里那份 | BSD-3-Clause | 单元测试 | 只进测试可执行体，不随产品分发 |
| Tesseract | 5.5.2（仅 Release 档） | Apache-2.0 | 文字识别后端 | 动态链接 |
| Leptonica | Tesseract 带进来的传递依赖 | BSD-2-Clause | Tesseract 的图像处理 | 动态链接 |
| zxing-cpp | 3.1.1（conandata.yml 里那份） | Apache-2.0 | 二维码/条形码识别 | 静态链接（许可证正文见 `licenses/Zxing-Apache-2.0.txt`） |
| C++/WinRT 投影头 | cppwinrt v2.0.250303.5 从 Windows SDK 10.0.26100.0 元数据生成 | MIT | Windows 系统识别的编译期投影 | vendor 在 `Vendor/WinRt/`，只进编译，不作为独立件分发 |
| Visual C++ 运行库（`vcruntime140.dll`、`vcruntime140_1.dll`、`msvcp140.dll`、`msvcp140_1.dll`、`msvcp140_2.dll`） | 与编译器同版本（本机 14.51.36231，按版本号取） | Microsoft 再分发条款：允许随应用程序一起分发运行库 | 我们的可执行体与 Qt 那几件都导入它 | 打包时从工具链的 `VC\Redist\MSVC\<版本>\onecore\x64\Microsoft.VC145.CRT` 取五份，摆在与可执行体同一目录（app-local，Windows 先搜程序目录） |

Qt 走 LGPL-3.0 的「动态链接 + 可替换」路线：用户拿到的是 Qt 的 DLL，可自行换成同版本库；
构建脚本不静态链接 Qt，也不做只读合并。产品若要在禁用逆向工程的场合分发，需要商业授权，
这一条属于发布方的决定，代码侧不做假设。

## 识别语言包

`eng.traineddata` 与 `chi_sim.traineddata`（Tesseract 官方的 `tessdata_fast` 系列，Apache-2.0）
**随安装包分发**，放在程序目录的 `tessdata` 子目录里——软件要求「装完即用」，
不能把放语言包这一步留给用户。查找顺序写在 `docs/OcrBackends.md`：
设置项 `Ocr/TessDataDirectory` → 环境变量 `TESSDATA_PREFIX` → **程序目录下的 `tessdata`** → 应用数据目录下的 `tessdata`。
许可证正文在 `licenses/TessData-Apache-2.0.txt`。
缺语言包（例如用户手动删过安装目录）时识别功能会明确报「不可用」并列出找过的位置，不装成能用而返回空结果。
Windows 上这一节只在选用 Tesseract 时才用得到：系统识别走 Windows 自己的语言包
（设置 → 时间和语言 → 语音 里的「光学字符识别」），不需要往任何目录放文件。

## 录屏用的 FFmpeg（随包分发，但不链接）

录屏的编码一步调用 `ffmpeg` 可执行文件（`QProcess` 起进程）。查找顺序是
**程序目录里随包那一份 → PATH**：安装包自带它，用户不需要再装任何东西。
本项目不链接 FFmpeg / x264 的任何库，也不把它的代码并入本程序——这是「聚合分发」而不是衍生作品，
所以 GPL 不传染本体的源码；但既然随包分发了 GPL 构建，就得按 GPL 的第三条交付对应源码：

- 随包的是**未经修改**的官方构建（gyan.dev，版本见 `scripts/FetchFfmpeg.ps1` 里钉的 `$version`），
  其对应源码由上游公开发布，可从 FFmpeg 官网的 releases 页取到同一版本；
- 许可证正文在 `licenses/FFmpeg-GPL-3.0.txt`；
- 取件与校验：`scripts/FetchFfmpeg.ps1` 按固定 URL 下载并核对 SHA-256，之后才允许打包——
  校验不过或取不到就中止打包，不允许出一个「能装但录不了屏」的包。

这样做的代价是包体积（ffmpeg 单文件约百 MB 级）。换来的是产品在任何一台干净机器上
装完就能录屏，且用户换 FFmpeg 版本不影响本程序构建。

## 容器与开发工具（不进产品包）

| 组件 | 许可证 | 说明 |
| --- | --- | --- |
| Ubuntu 24.04 基础镜像 | 各自包的许可证 | 仅用于 Linux 侧构建与验证 |
| GCC 13 / Clang 18 | GPL-3 / Apache-2.0 with LLVM-exception | 编译器不随产品分发 |
| CMake、Ninja、Conan 2 | BSD-3 / Apache-2.0 等 | 构建工具 |
| Xvfb、Noto Sans CJK | MIT / OFL-1.1 | 容器内无界面测试与中文字体；Noto 只在容器里，Windows 包用系统字体 |

## 图标与美术资源

`Resources/Icons/*.svg` 全部由本项目生成并拥有：统一的 24×24 视框、1.5/2 描边、圆头圆角，
颜色由 `IconProvider` 在运行时按主题替换 `currentColor`。不含表情符号、字体图标、
外部图片或 base64 嵌入。若日后引入任何第三方图标，必须在本页新增一行并写明许可证。

## 本页之外的义务

- 若发布静态链接的组件，需要按对应许可证提供目标文件或再分发权；
- 修改过的第三方源码必须在本页注明来源与改动位置；
- 随包发出的内容（`scripts/WindowsPackage.ps1` 负责摆好，缺任一件就中止打包）：
  `LICENSE.txt`（本软件自己的最终用户许可协议，安装器的许可页就是它）、`licenses/` 下各第三方
  许可证正文、`README.md`、`docs/` 里的三份参考页（ThirdPartyLicenses / OcrBackends / UiStyleGuide，
  内部需求文档不发）、Qt 运行时与插件、英文语言包 `.qm`、`tessdata/`、`ffmpeg.exe`、
  VC++ 运行库那五份（与可执行体同目录）。
- VC++ 运行库不带许可证正文文件：本机工具链的 Redist 目录里没有可 vendored 的正文（实测
  `find … -iname "*license*"` 零命中），再分发条款允许随应用一起分发运行库但不给一个可拷的文本；
  所以这一项只在本页记条款依据与取件路径，不编造 URL，也不把 VS 的最终用户许可协议整份塞进包。
- `licenses/` 里的正文都 vendored 进仓库，打包不依赖联网取件。URL 全部钉到具体版本，
  缺件或取回的不是许可证时由 `scripts/FetchLicenses.ps1` 重取——它按每份许可证的一行标记判，
  所以「下载失败留下的 404 页面」会被当成缺陷而不是一个看起来正常的文件。
  对应关系：Qt → `Qt-LGPL-3.0.txt`、FFmpeg → `FFmpeg-GPL-3.0.txt`、Tesseract 与语言包 →
  `Tesseract-Apache-2.0.txt` 与 `TessData-Apache-2.0.txt`、Leptonica 1.87.0 → `Leptonica-BSD-2.txt`、
  spdlog 1.17.0 → `Spdlog-MIT.txt`、GoogleTest 1.17.0 → `GoogleTest-BSD-3-Clause.txt`、
  C++/WinRT 投影 → `WinRtProjection-MIT.txt`（钉到 tag 2.0.250303.1：我们生成的那份投影是
  `2.0.250303.5`，它只作为 NuGet 包发布、没有对应 tag，而两个版本里的 MIT 正文逐字节相同，
  这一点由 `cmp` 现测得出）。后两件不进产品包（只进测试可执行体、只进编译），正文一并放在这里
  是为了让清单上点到的每个组件都能查到原文。
- 本页只讲第三方；本软件自身的授权条款在仓库根的 `LICENSE.txt`。
