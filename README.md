# GyanisScribe / 智录

Windows 优先、可跨平台的桌面工具：截图、标注、贴图、取色、录屏、文字识别。
交互参考 Snipaste，但不使用其任何代码、图标、界面素材或商标。

- **技术栈**：C++23 · Qt 6.11（Widgets/Svg/Network）· CMake ≥ 3.28 · Conan 2 · MSVC 2022；Linux 侧在 Docker Ubuntu 24.04 容器内构建与验证
- **当前状态**：六大功能已可用；本机 debug 与 release 两档各 234 条用例全绿；容器 asan 与 coverage 两档
  233 条通过、1 条跳过（容器那份 Qt 6.4 没有 lrelease，「真实语言包装载」那条只能在有包的一档成立）；
  行覆盖率 63.4%（阈值 55%）；打包脚本产出绿色目录 + zip + NSIS 安装器，英文语言包随包分发
- **需求与硬约束**：[docs/ProjectRequirements.md](docs/ProjectRequirements.md)

## 热键与功能

| 动作 | 默认组合 | 说明 |
| --- | --- | --- |
| 框选截图 | `Ctrl+Alt+A` | 截完进标注窗；工具八件，色板、线宽、字号、撤销重做、复制/保存/贴图/识别 |
| 录屏 | `Ctrl+Alt+R` | 再按一次停止并编码；控制条显示状态、时长与 3-2-1 倒计时，可暂停、贴边、最小化到托盘；开了「光标高亮」时用一圈把光标位置画在帧上（整屏与选区生效） |
| 文字识别 | `Ctrl+Alt+O` | 截完直接进结果窗口：左侧原图、右侧文本，点一行就在图上圈出那一行，可导出 TXT/JSON、换语言重认；托盘还有一条「识别图片文件…」，挑盘上的一张图直接认 |
| 屏幕取色 | `Ctrl+Alt+P` | 全屏冻帧 + 9×9 放大镜，点一下把颜色按 HEX/RGB/HSL 复制走 |

设置窗口左侧按类别竖排：通用 / 截图 / 标注 / 贴图 / 录屏 / 文字识别 / 快捷键 / 主题 / 关于 / 历史，
每页顶部一句说明写清这一类管什么。除界面语言（重启后生效）外，改完立即写入配置；
标注默认线宽与字号、贴图默认不透明度、录屏默认帧率与光标高亮，从下一次新建标注窗、新贴一张图、
下一次开始录屏时起用。历史留多少条、留多少天在「通用」里改，填到 0 就是不限；
同一页还能把全部设置导出成一个 JSON 文件或从那份文件读回来（换机器与备份用）。
托盘菜单是找不到窗口时的入口。

## 在 Windows 上构建

前置：Visual Studio 2022（MSVC）、CMake ≥ 3.28、Ninja、Conan 2、Qt 6.11（MSVC 套件，含 Svg/Network）、`ffmpeg`（仅录屏编码需要）。

```bat
set QTDIR=G:\Tools\Qt\6.11.1\msvc2022_64
scripts\WindowsBuild.bat release
build\release\bin\GyanisScribe.exe --selftest
```

`WindowsBuild.bat` 会自己调 `vcvars64`、按预设配置/编译并跑 `ctest`。预设两档：`debug`（识别走降级桩）与
`release`（带 Tesseract 后端）。机器相关的路径只出现在这个脚本里，工程不写死绝对路径。
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
tests/          逐级镜像 src 的目录结构
```

图标全部为原创 SVG（统一 24 视框、`currentColor`、1.5/2 描边），由 `IconProvider` 按主题着色；
第三方组件与许可证见 [docs/ThirdPartyLicenses.md](docs/ThirdPartyLicenses.md)，
识别后端与语言包的落地细节见 [docs/OcrBackends.md](docs/OcrBackends.md)。

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

## 已知问题

- 识别需要用户自备语言包（`eng.traineddata` 等）；缺语言包时功能明确报"不可用"并给出放置位置，
  不会返回空结果冒充"这张图里没字"。
- 录屏依赖外部 `ffmpeg`：不链接、不随包分发（避开 GPL 传染），没装就在编码那一步给出中文提示。
- 安装器的"装完能起、卸载能清"这一步还没在真机上跑过 smoke test：脚本目前验证到
  staging 目录自检通过 + 安装器带卸载段。
- Debug 档不领 Tesseract：上游 leptonica 的 Debug 包库名与其 `cpp_info` 不一致，
  `conanfile.py` 里按 `build_type` 分岔并写明撤除判据。

## 后续计划

按需求条目核对过，剩下这些没做，都在等一个能验证它们的环境：

- **录屏带声音**（需求功能清单第 5 项的「麦克风/系统声音」）：没实现。取音要枚举系统设备并让用户选一路，
  Windows 侧走 `dshow`/WASAPI 环回、Linux 侧另一套，而本机 CI 与容器都没有音频设备——
  现在写进去就是一段没人验过的代码。资源里 `Microphone.svg` / `SystemAudio.svg` 是需求第七节点名的图标，先随包备着。
- **长录屏**：帧序列按 PNG 逐帧落盘，1080p 十分钟就是 GB 级；换成喂 ffmpeg `rawvideo` 管道才能放开时长上限。
- **Linux 全局热键**：`Platform` 抽象下只有 Windows 后端有真登记，Linux 侧给出中文降级提示；
  要做得接 X11/Wayland 各自的快捷键协议。
- **窗口录屏的光标位置**：整屏与选区都能圈出光标，录单个窗口时圈不出来——帧里只有窗口像素，
  窗口左上角的全局读数要等平台侧的窗口几何接口。`RecordingService` 在第一帧就把这一点写进日志，不画错位的圈。
- **安装器真机 smoke test**：脚本验到 staging 自检通过 + 安装器带卸载段，「装完能起、卸载能清」还没在真机上跑过。
- **主线程还会被长任务占着**：需求的硬约束要求「耗时操作放工作线程」，而现在三处仍在 GUI 线程上跑——
  录屏逐帧写 PNG（`PngSequenceSink::write`，一帧的写盘时间直接算进下一格的等待）、
  停止录屏后的 ffmpeg 编码（`RecordingService::stop` 里同步等）、以及文字识别（`AppController::recognizeWithLanguage`
  同步调后端）。本机量不出识别的卡顿：这台机器没有语言包，Debug 档又走降级桩。
  **恢复条件**：并发改动要有 TSan 才算判据（本机 MSVC 没有 TSan，而容器里 `setarch -R` 被 Docker 自带 seccomp 拒绝），
  先用 `docker run --security-opt seccomp=unconfined` 起一档、证明旧写法报得出竞态，再动这三处。
  没有这一档之前把跨线程手抖写进去，只是把可复现的卡顿换成不可复现的竞态。
- **两处路径仍按本地码页交给第三方库**（各自在同一句里写明理由，`Style.SourceConventions` 认这些理由）：
  spdlog 的日志文件名（本构建没开 `SPDLOG_WCHAR_FILENAMES`，窄串要按本地码页给）与 Tesseract/Leptonica 的
  `TessBaseAPIInit3`（C 接口拿 `const char*` 开文件）。要彻底了断 spdlog 那一处，得在 Conan 侧把 wchar
  文件名选项打开，再回到 `toDisplayText` 那条路上。
- **TSan 一档**：容器里跑不了（`setarch -R` 的 `personality` 被 Docker 自带 seccomp 拒绝，实测 EPERM），
  要用 `docker run --security-opt seccomp=unconfined` 起容器才能补上这一轮跨线程判据。
