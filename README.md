# GyanisScribe / 智录

Windows 优先、可跨平台的桌面工具：截图、标注、贴图、取色、录屏、文字识别。
交互参考 Snipaste，但不使用其任何代码、图标、界面素材或商标。

- **技术栈**：C++23 · Qt 6.11（Widgets/Svg/Network）· CMake ≥ 3.28 · Conan 2 · MSVC 2022；Linux 侧在 Docker Ubuntu 24.04 容器内构建与验证
- **当前状态**：六大功能已可用；本机 debug 与 release 两档各 243 条用例全绿；容器 asan 与 coverage 两档
  238 条通过、1 条跳过（容器那份 Qt 6.4 没有 lrelease，「真实语言包装载」那条只能在有包的一档成立）；
  行覆盖率 67.0%（阈值 55%）；打包脚本产出绿色目录 + zip + NSIS 安装器，英文语言包随包分发
- **需求与硬约束**：[docs/ProjectRequirements.md](docs/ProjectRequirements.md)

## 热键与功能

| 动作 | 默认组合 | 说明 |
| --- | --- | --- |
| 框选截图 | `Ctrl+Alt+A` | 截完进标注窗；工具八件，色板、线宽、字号、撤销重做、复制/保存/贴图/识别 |
| 截取当前窗口 | 托盘菜单 | 按平台拿前台窗口再去抓帧；Linux 拿不出句柄，这一项在菜单里根本不出现（不是留着让你点出一条报错） |
| 录屏 | `Ctrl+Alt+R` | 再按一次停止并编码；控制条显示状态、时长与 3-2-1 倒计时，可暂停、贴边、最小化到托盘（收起后托盘那一行报「正在录制 / 已暂停 + 时长」）；开了「光标高亮」时用一圈把光标位置画在帧上（整屏与选区生效）；录制范围与输出格式在设置的录屏页选，取声也是（Windows） |
| 文字识别 | `Ctrl+Alt+O` | 截完直接进结果窗口：左侧原图、右侧文本，点一行就在图上圈出那一行，可导出 TXT/JSON、换语言重认；托盘还有一条「识别图片文件…」，挑盘上的一张图直接认 |
| 屏幕取色 | `Ctrl+Alt+P` | 全屏冻帧 + 9×9 放大镜，点一下把颜色按 HEX/RGB/HSL 复制走 |

设置窗口左侧按类别竖排：通用 / 截图 / 标注 / 贴图 / 录屏 / 文字识别 / 快捷键 / 主题 / 关于 / 历史，
每页顶部一句说明写清这一类管什么。除界面语言（重启后生效）外，改完立即写入配置；
标注默认线宽与字号、贴图默认不透明度、录屏的录制范围与输出格式、默认帧率与光标高亮，从下一次新建标注窗、新贴一张图、
下一次开始录屏时起用。历史留多少条、留多少天在「通用」里改，填到 0 就是不限；
同一页还能把全部设置导出成一个 JSON 文件或从那份文件读回来（换机器与备份用）。
托盘菜单是找不到窗口时的入口，还有一条「关闭全部贴图」用来一次清场。
退出前把还在桌上的贴图连像素一起记下（像素落在缓存目录，配置里只存路径与几何），下次启动摆回原位；
某张图被清掉时只跳过那一张，剩下的照旧摆回来。

## 在 Windows 上构建

前置：Visual Studio 2022（MSVC）、CMake ≥ 3.28、Ninja、Conan 2、Qt 6.11（MSVC 套件，含 Svg/Network）、`ffmpeg`（仅录屏编码需要）。

```bat
set QTDIR=G:\Tools\Qt\6.11.1\msvc2022_64
scripts\WindowsBuild.bat release
build\release\bin\GyanisScribe.exe --selftest
```

`WindowsBuild.bat` 会自己调 `vcvars64`、按预设配置/编译并跑 `ctest`。预设两档：`debug` 与 `release`——
Windows 上两档都带系统识别后端（`Windows.Media.Ocr`，用系统自己装的语言包），只有 `release` 额外领 Tesseract
（上游 Debug 包的问题见 [docs/OcrBackends.md](docs/OcrBackends.md)）。机器相关的路径只出现在这个脚本里，工程不写死绝对路径。
Conan 两侧都是仓库里的一份档位（`Conan/Profiles/WindowsMsvc2022` 与 `LinuxGcc13`），
不是每次现检测的产物：Windows 侧由脚本把 `-DCONAN_HOST_PROFILE` 指过去，容器侧由 `env.sh` 设
`CONAN_DEFAULT_PROFILE_PATH`。档位里唯一按机器改的一行是 `compiler.version`；
`build_type` 刻意不写进文件，它跟着预设走，而「Debug 不领 Tesseract」正是按 `build_type` 分岔的。

打包（绿色目录 + zip + NSIS 安装器）：

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File scripts\WindowsPackage.ps1
```

它会用 `windeployqt` 按可执行体的实际导入摆好 Qt 运行时，然后**在 staging 目录里跑一次 `--selftest`**——
只有这样才能证明包是完整的，而不是"本机能跑"。产物在 `dist/`。

## 在容器里构建（Linux 侧验证）

```bash
docker build -f Dockerfile.dev -t gyanisscribe-dev .
docker run --rm -it -v "$PWD:/work" gyanisscribe-dev bash
```

容器内五件一键脚本（`scripts/container/`）：

```bash
scripts/container/init.sh          # 补包并体检工具链（幂等，可 SKIP_APT=1 只体检）
scripts/container/build.sh debug   # 也支持 release / asan / tsan / coverage
scripts/container/test.sh debug    # 有 Xvfb 就在虚拟屏下跑，界面用例才算真被跑到
scripts/container/sanitize.sh      # ASan(+UBSan)；tsan 档另说，见下方边界
scripts/container/coverage.sh 55   # gcovr 汇总，阈值不过就红（判定由 gcovr 的 --fail-under-line 做）
```

`asan` 档既可以用 `sanitize.sh` 一把跑完，也可以 `build.sh asan` 再 `test.sh asan`：sanitizer 的运行期参数
（含 LSan 那张只放过系统库字体缓存的抑制表）统一在 `env.sh` 里导出，两条入口给的是同一个结果。
早先这张表只挂在 `sanitize.sh` 上，换 `test.sh` 跑就会多出两条与代码无关的红。

Linux 侧的已知边界：没有全局热键能力（登记会给出中文提示并降级）、无系统托盘时菜单改由主窗口提供、
`Qt::ColorScheme` 要 6.8 才有而容器是 6.4（跟随系统会解算到兜底档）、**TSan 这一档在当前容器里跑不了**——
这台内核（WSL2 6.18）下 libtsan 必须关 ASLR 才起得来进程，而 `setarch -R` 走的 `personality` 被 Docker 自带
seccomp 档拒绝（实测 EPERM）。脚本会在建之前先探这一点并当场报红，不会把「没跑」当成「跑过」；
要跑它得用 `docker run --security-opt seccomp=unconfined` 起容器。

## 目录

```
src/Core        值类型、日志、目录 provider、图片等比适配几何
src/Ui          设计令牌、主题、图标供给、Toast、快捷键输入框
src/Platform    平台能力抽象（Windows / Linux 两套后端）
src/Settings    配置读写与迁移       src/Hotkey    组合键解析与全局登记
src/Capture     抓帧、选区、遮罩     src/Annotation 八个标注工具与标注窗
src/Pin         贴图窗口与贴图管理   src/Record      录屏会话、帧时钟、ffmpeg 编码、控制条
src/Ocr         预处理、后端选型、结果窗口与导出
src/History     历史清单、保留策略、缩略图列表
src/Pick        取色器：颜色写法与放大镜几何
src/App         应用对象、核心控制器、托盘、主窗口、单实例、崩溃兜底
Vendor/WinRt    Windows 系统识别要用的 C++/WinRT 投影头（MIT，生成与裁剪办法见 docs/OcrBackends.md）
tests/          逐级镜像 src 的目录结构
```

图标全部为原创 SVG（统一 24 视框、`currentColor`、1.5/2 描边），由 `IconProvider` 按主题着色；
用例按模块单测，另有一条跨模块流程的集成入口 `tests/App/TestModuleFlow.cpp`（抓帧 → 标注 → 贴图 → 识别）：
它按生产形状造替身，钉的是模块接在一起时坐标系与语言词表还对不对——这类漂移在单模块用例里看不见。
第三方组件与许可证见 [docs/ThirdPartyLicenses.md](docs/ThirdPartyLicenses.md)，
识别后端与语言包的落地细节见 [docs/OcrBackends.md](docs/OcrBackends.md)，
界面令牌、每个窗口的样式与状态、深浅主题对照与高 DPI 适配见 [docs/UiStyleGuide.md](docs/UiStyleGuide.md)。

## 界面文案与语言切换

界面源码文案是中文。任何要显示给用户（含报错提示）的文本都走 `Core::uiText("中文")` /
`Core::uiTextUtf8("中文{}")`（`src/Core/Text.h`），日志 `Core::Logger::*` 刻意保持中文——排障的人
不该因为界面语言而读不到日志。

- 语言在「设置 → 通用」里选，配置键 `Ui/Language`（`zh` / `en`）；切换在重启后生效。
- 英文档需要 `translations/GyanisScribe_en.qm`，随包分发；缺包时应用按中文继续并给出放置位置。
- 翻译目录 `Translations/GyanisScribe_en.ts` 是唯一真源，`.qm` 由构建期的 lrelease 产出
  （挂在 `DeployTranslations` 上）。容器里那份 Qt 6.4 没有 lrelease，那一档不出包。
- 改完文案要重新生成目录：`python scripts/check-translation-catalog.py --generate`，把 `<translation type="unfinished">` 补成英文再构建。
  ctest 用例 `Style.TranslationCatalog` 会断言「源码里用到的每条文案都在目录里且有非空译文」——
  漏掉的那几处在英文档下会安静地显示中文，没人会逐条菜单点出来，所以钉成门禁。
  脚本用 Python 而不是 CMake：穷举要把被 clang-format 断成相邻字面量的长文案按编译期规则拼回去，
  CMake 的正则在跨语句时给出了错误的匹配（实测把十条不相关的文案拼成一条），门禁一旦误判就是永久假绿。

## 启动参数

`GyanisScribe --help` / `--version` / `--selftest`。解析走 `QCommandLineParser`：
`--selftest` 把启动路径跑完就退（日志接管成功即退出码 0），打包脚本与 CI 用它当「起得来」的判据；
不认识的参数当场退出码 1，不会装成「没带参数」安静地起一个界面。Qt 自己的参数（`-platform` 一类）
照旧认得，实测 `GyanisScribe.exe -platform offscreen --version` 正常退出 0。
`--help` 的说明文字进文案目录，装了英文包时命令行帮助也走英文。

## 性能与测量

需求 §十一 的指标与它们的实际测量方法——没测过的直接写「未测」，不拿推测充数：

| 指标 | 本机实测 | 方法 |
| --- | --- | --- |
| 截图延迟 < 100ms | 中位 30–31ms（10 次采样，min 26 / max 35） | 独立探针走产品同一条路（`QScreen::grabWindow` → `toImage` → `convertToFormat(ARGB32_Premultiplied)`），丢掉首轮的一次性开销，两次独立运行给出 30 与 31ms。机器：2560×1440、dpr 1.0、单屏、Release Qt |
| OCR 单张 < 1s | 几十毫秒量级（640×160 的一次识别连同后端构造与 PNG 编解码，单用例计时 43ms） | ctest 的 per-test 计时；跨模块那条用 900×600 的图，337ms 里含 Qt 应用与引擎初始化 |
| 录屏 1080p30 不掉帧、CPU 占用合理 | **未测** | 需要真机跑满 60s 再统计帧间隔分布与峰值 CPU；而现在帧序列按 PNG 逐帧落盘，1080p 十分钟就是 GB 级，这条要等 `rawvideo` 管道那一项落地后一起测（见下方后续计划） |
| 贴图拖动流畅、鼠标穿透不抢焦点 | 定性验证：拖动走 `PinTransform` 的增量换算（有用例钉数值），穿透由平台侧 `setMouseThrough` 落地 | 主观流畅度无法用断言钉；这里只承诺「没有动画在拖动的路上插队」 |

## 已知问题

- Windows 上识别首选系统组件（`Windows.Media.Ocr`），它用的是 Windows 自己装的语言包，不需要用户放文件；
  只有选 Tesseract（以及 Linux 侧）时才需要自备 `eng.traineddata` 等——缺语言包会明确报"不可用"并给出放置位置，
  不会返回空结果冒充"这张图里没字"。
- 录屏依赖外部 `ffmpeg`：不链接、不随包分发（避开 GPL 传染），没装就在编码那一步给出中文提示。
- 安装器的"装完能起、卸载能清"这一步还没在真机上跑过 smoke test：脚本目前验证到
  staging 目录自检通过 + 安装器带卸载段。
- Debug 档不领 Tesseract：上游 leptonica 的 Debug 包库名与其 `cpp_info` 不一致，
  `conanfile.py` 里按 `build_type` 分岔并写明撤除判据。Windows 的 Debug 档仍能真识别——工厂先问系统识别。

## 后续计划

按需求条目核对过，剩下这些没做，都在等一个能验证它们的环境：

- **Linux 侧录屏取声**：Windows 那一路已经落地（见下方「录屏带声音」），Linux 的入口是 ALSA/Pulse，
  容器与 CI 都没有音频设备，写进去就是一段没人验过的代码。现在那一侧列出设备与取声都直接给出中文降级原因。
- **录屏带声音**（Windows 已做）：设置的录屏页里麦克风与系统声音各一格，选中即取那一路，两路都选时 `amix` 混音；
  采到中间 wav，编码那一步并进 MP4（GIF 容器无音轨，不动画）。取声起不来就拒绝开始录屏，
  中途丢了声音则成品照出并当面说一句。判据：`GYANISSCRIBE_FFMPEG` 与 `GYANISSCRIBE_AUDIO_DEVICE` 都给齐时
  端到端跑一条真采集（本机实测 2 秒出 381774 字节的 48k 双声道 wav），缺任一就 SKIP。
- **长录屏**：帧序列按 PNG 逐帧落盘，1080p 十分钟就是 GB 级；换成喂 ffmpeg `rawvideo` 管道才能放开时长上限。
- **Linux 全局热键**：`Platform` 抽象下只有 Windows 后端有真登记，Linux 侧给出中文降级提示；
  要做得接 X11/Wayland 各自的快捷键协议。
- **窗口录屏的光标位置**：整屏与选区都能圈出光标，录单个窗口时圈不出来——帧里只有窗口像素，
  窗口左上角的全局读数要等平台侧的窗口几何接口。`RecordingService` 在第一帧就把这一点写进日志，不画错位的圈。
- **安装器真机 smoke test**：脚本验到 staging 自检通过 + 安装器带卸载段，「装完能起、卸载能清」还没在真机上跑过。
- **主线程还会被长任务占着**：需求的硬约束要求「耗时操作放工作线程」，而现在三处仍在 GUI 线程上跑——
  录屏逐帧写 PNG（`PngSequenceSink::write`，一帧的写盘时间直接算进下一格的等待）、
  停止录屏后的 ffmpeg 编码（`RecordingService::stop` 里同步等）、以及文字识别（`AppController::recognizeWithLanguage`
  同步调后端）。识别这一处本机已经量得着：Windows 两档都带系统后端，不再靠降级桩；Tesseract 那一路仍只在 Release 档。
  **恢复条件**：并发改动要有 TSan 才算判据（本机 MSVC 没有 TSan，而容器里 `setarch -R` 被 Docker 自带 seccomp 拒绝），
  先用 `docker run --security-opt seccomp=unconfined` 起一档、证明旧写法报得出竞态，再动这三处。
  没有这一档之前把跨线程手抖写进去，只是把可复现的卡顿换成不可复现的竞态。
- **两处路径仍按本地码页交给第三方库**（各自在同一句里写明理由，`Style.SourceConventions` 认这些理由）：
  spdlog 的日志文件名（本构建没开 `SPDLOG_WCHAR_FILENAMES`，窄串要按本地码页给）与 Tesseract/Leptonica 的
  `TessBaseAPIInit3`（C 接口拿 `const char*` 开文件）。要彻底了断 spdlog 那一处，得在 Conan 侧把 wchar
  文件名选项打开，再回到 `toDisplayText` 那条路上。
- **TSan 一档**：容器里跑不了（`setarch -R` 的 `personality` 被 Docker 自带 seccomp 拒绝，实测 EPERM），
  要用 `docker run --security-opt seccomp=unconfined` 起容器才能补上这一轮跨线程判据。
