# 智录 · GyanisScribe

<p align="center">
  <img src="https://img.shields.io/badge/C%2B%2B-C%2B%2B23-00599C?logo=c%2B%2B&logoColor=white" alt="C++23">
  <img src="https://img.shields.io/badge/Qt-6.11-41CD52?logo=qt&logoColor=black" alt="Qt 6.11">
  <img src="https://img.shields.io/badge/CMake-3.28%20%2B%20Conan%202-064F5C" alt="CMake and Conan">
  <img src="https://img.shields.io/badge/Platform-Windows%20%C2%B7%20Linux-informational" alt="Windows and Linux">
  <img src="https://img.shields.io/badge/version-0.0.1-lightgrey" alt="version 0.0.1">
</p>

Windows 优先、可跨平台的桌面效率工具：**截图 · 标注 · 贴图 · 取色 · 录屏 · 文字识别**。
C++23 + Qt 6 Widgets 实现，界面、图标与代码全部为本项目原创，不含任何现有产品的素材或商标。

- 常驻托盘，六个全局热键覆盖日常动作，不需要先开主窗口
- 一个可执行体加随包摆好的 Qt 运行时，绿色目录即可运行；同时产出 zip 与 NSIS 安装器
- 浅色、深色、高对比度三套主题，另有跟随系统；高 DPI 与多屏；中英双语界面
- 录屏编码走 `ffmpeg` 子进程，**随包分发、不链接**：装完即用，也不让 GPL 传染本体
- 每个模块都有单测，另有一条跨模块流程用例；四条验证线（本机两档 + 容器两档）都是必建必测的门禁

## 目录

- [功能](#功能)
- [快速开始](#快速开始)
- [快捷键](#快捷键)
- [设置与配置文件](#设置与配置文件)
- [从源码构建](#从源码构建)
- [打包](#打包)
- [验证与质量门禁](#验证与质量门禁)
- [项目结构](#项目结构)
- [性能实测](#性能实测)
- [已知问题](#已知问题)
- [路线图（后续计划）](#路线图后续计划)
- [参与贡献](#参与贡献)
- [许可与第三方声明](#许可与第三方声明)

## 功能

**截图**
全屏 / 区域框选 / 当前窗口 / 多显示器 / 高 DPI。按下热键后整屏冻帧，直接在上面拖框；
遮罩同时认出光标压在的是哪一扇窗口并亮出它实际可见的那圈边界（比窗口矩形准，不含看不见的可调边框），
原地按一下就整扇选中，拖过之后要改就接着拖；Enter 或双击确认，Esc 取消，选完立刻进标注窗。
框好之后还想改不必重框：选区落定后四角与四边中点各出现一枚抓点，按住它就是改这一块（鼠标形状会跟着变，
内部按住是整块平移）；方向键整块平移一格，按住 `Shift` 挪右下角一格。拖过头不会把选区翻面，贴到屏边就停下。
拖框途中光标旁边跟着一枚 9×9 像素放大镜，下面一行报这一点的全局坐标与颜色（HEX），
卡像素边界时不必先截下来再放大看；贴到屏角时它自己翻到光标另一侧，不会跑出这块屏。
平台列不出窗口时（如 Linux）退回纯手动框选，不假装这一格有。
截完之后做什么由设置里那一档决定：默认进标注窗，也可以选复制到剪贴板、直接存成文件、
直接贴到屏幕上或直接识别文字——这四档都不弹窗，按下去就落到成品上，
成不成都有句提示（剪贴板与贴图这两步本来没有任何界面变化，不说就分不清「按了没反应」与「已经进去了」）。
另一格「延时截图」等 0 到 60 秒再开始，用来截菜单、悬停提示这类一碰就没的画面；等的时候会说一声，
延时期内再按一次只回一句「上一次还在等」，不叠第二层遮罩。
存成文件时写哪一种格式也可选：PNG（无损，默认档）、JPG、WebP，另有一格质量管着后两种
（「直接存成文件」与贴图右键的「保存」都吃这一档；退出前存的那份恢复数据永远是 PNG，
拿有损档存恢复数据会让桌面每次重启都糊一点，而没人要求过这种事）。图像编码器是插件，
这台机器写不出来的那一档不会摆进设置页——容器那份 Qt 就没有 WebP 的写插件，摆出来只会
让人选一次、失败一次。
「鼠标指针」那一格（默认不含）把指针画进成品：形状取的是系统此刻正在画的那一枚，配色与 DPI 都跟着系统走，
按指针的热点落位，尖端指哪儿就在哪儿。区域截图是先画进整帧再裁选区，选区里带不带指针就看它落没落在里面。
滚动长截图那一趟刻意不带——按帧接图会把一枚指针复制成一串。非 Windows 平台取不到系统指针的形状，
开着也画不出来，只在日志里说明这次不含指针。
滚动长截图（`Ctrl+Alt+L`、托盘菜单与主窗口动作条上都有这一条）：先框出一条要滚的区域，之后按固定节拍反复抓这一块，
每帧自己认出与已接部分的重叠、只把新的一段接下去；过程中浮一条控制条报「已接多少像素」，
按完成就当成一次普通截图交出去（去向仍由设置里那一档决定），按取消整段扔掉。
接的过程中再按一次截图会被当场拒掉并说明「先按完成或取消」——铺第二层遮罩会让手里那张接了一半的图
既没交也没作废。
页面每几行重复一次时接缝会同时对上几个长度，取的是最长的那一个——收短的会把同一段接两遍；
只对上几行（两页开头都是空白）就当没接上，中途换屏或改缩放让宽度对不上时当场报错，
而不是悄悄接出一张歪图。

**标注**
工具九件：矩形、椭圆、箭头、直线、画笔、高亮、马赛克、文字、序号。色板、线宽、字号各自可选，
撤销重做、复制、保存、贴到屏幕上、送去识别都在同一排按钮上，选中的标注可以拖动与删除；
拖完还想挪一像素用方向键（按住 `Shift` 走十像素），不必重画。
打错了字也不必删了重打：双击那条文字，原地弹出带着原文的输入框，改完回车就换掉——
这是一步独立操作，`Ctrl+Z` 回到改之前那一份文字，而不是把整条抹掉。

**贴图**
把图钉在屏幕最上层对着用。滚轮缩放（一格 1.1 倍）、`Ctrl`+滚轮旋转、`Shift`+滚轮调透明度，
左键拖动移动，`R` 转正一挡（`Shift+R` 反向，一挡 90°）、`0` 或左键双击回到原始比例与角度（透明度不动，
那是用户刻意调的一档），右键菜单复制 / 保存 / 识别文字 / 保持置顶 / 鼠标穿透 / 关闭，`Esc` 关闭本张。
键盘与双击走的是同一个复位出口，两条路不会将来只改一处。
退出前把还在桌上的贴图连像素一起记下，下次启动摆回原位；某张图被清掉时只跳过那一张。
开着鼠标穿透的贴图收不到右键，也就关不掉自己那一档——托盘上「取消全部贴图穿透」就是这条死路的出口（没有这能力的平台上这一条根本不放），
按下去说清恢复了几张，一张都没开时也说「没有贴图开着鼠标穿透」，不让人以为程序卡住了。
要临时把桌面清干净不必关掉贴图：托盘与 `Ctrl+Alt+H` 那一条把整桌一起收起来，再按一次原样摆回——
收着的图还在册（张数、摆位、缩放、透明度都不动），只是不在屏上；因为收着的时候点不到它，摆回这一格也只放在托盘与热键上。
托盘上还有一条「把剪贴板里的图贴到屏幕上」：复制一张图（别的程序里复制的、截下来复制的都算）按一下就在光标处落一张贴图；
剪贴板里不是图像（比如只有一段文字）时给一句原因与替代做法，而不是静默什么都不做——文本与颜色转贴图还没有做，见路线图。

**屏幕取色**
全屏冻帧 + 9×9 放大镜，点一下按选定格式（HEX / RGB / HSL）把颜色复制走。

**录屏**
范围可选整屏 / 上次选区 / 当前窗口，输出可选 MP4 / GIF，帧率、画面码率、光标高亮、
麦克风与系统声音各自可开（两路都开时混音）。控制条显示状态、时长与 3-2-1 倒计时，
可暂停、可取消（这一段整段扔掉：不封盘、不起编码、临时帧当场删掉），可贴边、可最小化到托盘——
收起后托盘那一行仍报「正在录制 / 已暂停 + 时长」。停止之后再按取消会被拒，理由说在提示里
（那一份帧正是后台编码在读的输入）。
停止后编码在后台线程进行，界面不冻；编码器在编码前用真 `ffmpeg` 探一遍，取第一个开得起来的。

**文字识别**
`Windows.Media.Ocr`（用系统自己装的语言包，无需放文件）与 Tesseract 两个后端，按平台与可用性自动选型。
结果窗口左侧原图、右侧文本，点一行就在图上圈出那一行；可导出 TXT / JSON，可换语言重认。
托盘还有一条「识别图片文件…」，挑盘上的一张图直接认。

**历史记录**
写出过文件的成品进列表（缩略图 + 时间 + 类别 + 尺寸）：「直接存成文件」、标注窗的「保存」与录屏编出的成品都算，
只复制到剪贴板或只贴到屏幕上的那一次没有文件，也就不进列表。录屏那一类不解视频帧，缩略图取不出画面时摆一枚类别图标。
尺寸读不出时列表文案不带那一格，而不是打一个「0×0」冒充读数。可按文件名、类别或日期搜索，可按类别筛选，
次序固定时间倒序。每条可以贴回屏幕上、送去识别、打开它所在的那个目录，或单独移除；「清空列表」一次去掉全部记录，
会先问一句，且动的只是清单——成品文件留在各自目录里，软件不替用户做不可逆的那一步。按条数与天数保留（填 0 即不限）。

**其他**
中英文界面（英文档随包带语言包）、浅色/深色/高对比度三套主题与跟随系统、单实例检测、未捕获异常兜底
（写一条中文日志再以退出码 70 退出）、托盘作为找不到窗口时的入口。

## 快速开始

已经构建好（或拿到打包产物）时：

```powershell
# 绿色目录或安装后的可执行体
GyanisScribe.exe            # 起托盘，按 Ctrl+Alt+A 截第一张图
GyanisScribe.exe --help     # 启动参数
GyanisScribe.exe --selftest # 把启动路径跑完就退：日志接管成功即退出码 0；热键登记的结果只打进日志，撞了不把这一格判红
```

第一次运行要知道的三件事：

1. 程序常驻托盘，关掉主窗口不会退出进程，退出只走托盘菜单那一条路。托盘装不上的环境没有这条路
   （无托盘的桌面、容器里）：主窗口会在启动时自己现身，关掉它就是退出程序。
2. 全局热键默认 `Ctrl+Alt+A`（截图）、`Ctrl+Alt+R`（录屏，再按一次停止并保存）、`Ctrl+Alt+O`（识别）、
   `Ctrl+Alt+P`（取色），撞了就撞了——设置里可改，改完立刻重新登记，不用重启。
   哪一条没能登记上，启动时会在气泡里连它各自的原因一起说出来，不留「按了没反应」让人猜。
3. 录屏用的 `ffmpeg` 与识别语言包都在安装包里，程序目录里就能看见；不需要再装任何东西，
   也不需要往哪个目录放 `traineddata`。

Linux 侧当前是「可构建、可运行、能力降级」：抓帧、标注、贴图、识别可用，
全局热键与录屏取声没有平台后端——热键页那六行会按能力位灰掉并在页首写明改了也不会生效，
取声那两格给出中文降级原因，都不会静默失败；托盘同样没有，所以主窗口在启动时就现身。

## 快捷键

全局热键（可在设置的「快捷键」页改）：

| 动作 | 默认组合 | 说明 |
| --- | --- | --- |
| 框选截图 | `Ctrl+Alt+A` | 截完进标注窗 |
| 录屏 / 停止并保存 | `Ctrl+Alt+R` | 同一个键进出 |
| 文字识别 | `Ctrl+Alt+O` | 开一次框选，截完直接进结果窗、不弹标注窗；贴图右键、历史那页与标注窗上的「识别」吃的才是已经有的那一张图 |
| 屏幕取色 | `Ctrl+Alt+P` | 冻帧 + 放大镜 |
| 截取当前窗口 | 托盘菜单 | Linux 拿不出窗口句柄，这一项在菜单里根本不出现 |
| 滚动长截图 | `Ctrl+Alt+L` | 框住区域后自己按节拍抓帧拼接，控制条上按完成或取消 |
| 收起 / 摆回全部贴图 | `Ctrl+Alt+H` | 整桌贴图一起从屏上收走，再按一次原样摆回；收着不等于关掉，张数与摆位都不动 |

截图遮罩上：

| 键 | 作用 |
| --- | --- |
| `Enter` / 双击 | 确认当前选区 |
| `方向键` | 已框好的选区整块平移一格 |
| `Shift`+`方向键` | 选区右下角挪一格（改大小） |
| `Esc` | 取消 |

标注窗：

| 键 | 作用 |
| --- | --- |
| `Enter` | 复制到剪贴板 |
| `R` `E` `A` `P` `H` `M` `T` `N` `L` | 直接切到那一档工具：矩形 / 椭圆 / 箭头 / 画笔 / 高亮 / 马赛克 / 文字 / 序号 / 直线（取英文名首字母，裸按才认，带 `Ctrl` 的字母仍归原来的组合键） |
| `Ctrl+S` | 保存 |
| `Ctrl+D` | 贴到屏幕上 |
| `Ctrl+O` | 送去文字识别 |
| `Ctrl+Z` / `Ctrl+Shift+Z` 或 `Ctrl+Y` | 撤销 / 重做 |
| `Delete` / `Backspace` | 删掉选中的标注 |
| `方向键` | 选中的标注挪一格 |
| `Shift`+`方向键` | 选中的标注挪十格 |
| `Esc` | 关闭 |

贴图窗：拖动即移动，滚轮缩放，`Ctrl`+滚轮旋转，`Shift`+滚轮调透明度，`R` / `Shift+R` 转正一挡（90°），
`0` 或左键双击回到原始比例与角度（透明度保持不动），`Esc` 关闭，右键出菜单。没绑定的字母一律交回 Qt，
贴图窗不抢输入法与系统快捷键。

## 设置与配置文件

设置窗口左侧按类别竖排：通用 / 截图 / 标注 / 贴图 / 录屏 / 文字识别 / 快捷键 / 主题 / 关于 / 历史，
每页顶部一句说明写清这一类管什么。除界面语言（重启后生效）外，改完立即写入配置：
截图完成后做什么（进标注窗 / 复制到剪贴板 / 直接存成文件 / 直接贴到屏幕上 / 直接识别文字）、延时截图的秒数、
截图含不含鼠标指针、成品存成 PNG / JPG / WebP 与有损档的质量、标注默认线宽与字号、贴图默认不透明度、录屏范围与输出格式、默认帧率与光标高亮，
从下一次截图、新建标注窗、新贴一张图、下一次开始录屏时起用。
「通用」页还管着历史保留多少条、多少天（填 0 即不限），以及「导出配置… / 导入配置…」——
把全部设置写成一个 JSON 文件，换机器与备份时用；导入后订阅配置的热键与主题立刻跟上新值，
而当前那一页显示的还是导入前的值，关掉设置窗重开就对新。

| 用途 | 位置 |
| --- | --- |
| 配置 | Windows 注册表 `HKCU\Software\Gyanis\GyanisScribe`；Linux `~/.config/Gyanis/GyanisScribe.conf` |
| 截图默认目录 | 用户图片目录下的 `GyanisScribe/`（可在设置里改） |
| 录屏默认目录 | 用户视频目录下的 `GyanisScribe/`（可在设置里改） |
| 日志 | `<AppData>/Gyanis/GyanisScribe/logs`，每条都是中文，界面切成英文也不翻译日志 |
| 崩溃转储 | 同一个日志目录下的 `GyanisScribe_<版本>_<时刻>_<进程号>.dmp`；反馈时把日志与这份一起发回来才定位得动 |
| 贴图恢复数据 | 缓存目录里的像素文件，配置里只存路径与几何 |

界面文案是中文，走 `Core::uiText()` / `Core::uiTextUtf8()`；英文档需要
`translations/GyanisScribe_en.qm`（随包分发，缺包时按中文继续并给出放置位置）。
翻译目录 `Translations/GyanisScribe_en.ts` 是唯一真源，`.qm` 由构建期的 lrelease 产出。
改完文案要重新生成目录：

```bash
python scripts/check-translation-catalog.py --generate   # 把新增文案补进 .ts，再逐条填英文
```

## 从源码构建

### 前置要求

| 组件 | 版本 | 说明 |
| --- | --- | --- |
| 编译器 | MSVC（本机实测 14.51）/ GCC 13 | 需要 C++20/23 的 `std::expected` 与 `std::format` |
| CMake | ≥ 3.28 | — |
| Ninja | — | 生成器 |
| Conan | 2 | 依赖：spdlog 1.17.0、GoogleTest 1.17.0，Release 档另领 Tesseract 5.5.2 |
| Qt | 6.11，含 Core / Gui / Widgets / Concurrent / Svg / Network | Linux 容器里用发行版的 6.4 也可编 |
| Python | 3 | 两条静态门禁脚本 |
| ffmpeg | 官方静态构建（gyan.dev） | 打包时需要：`scripts/FetchFfmpeg.ps1` 按钉好的 URL + SHA-256 取进 `Vendor/Ffmpeg/`；开发期 PATH 上有一份也行 |
| NSIS | — | 可选：只在打安装器时需要 |

机器相关的路径只出现在 `scripts/WindowsBuild.bat` 与 `scripts/container/env.sh` 里，工程不写死绝对路径。

### Windows

```bat
set QTDIR=G:\Tools\Qt\6.11.1\msvc2022_64
scripts\WindowsBuild.bat release
build\release\bin\GyanisScribe.exe --selftest
```

`WindowsBuild.bat` 自己调 `vcvars64`、按预设配置、编译并跑 `ctest`，一条命令就是一条门禁。
预设两档：`debug` 与 `release`。Windows 上两档都带系统识别后端（`Windows.Media.Ocr`），
只有 `release` 额外领 Tesseract——上游 leptonica 的 Debug 包与其 `cpp_info` 对不上，
细节与撤除判据见 [docs/OcrBackends.md](docs/OcrBackends.md)。

`CMakePresets.json` 里的档位名不分平台：`debug` / `release` / `asan` / `tsan` / `coverage` 五档，
两侧共用同一份文件。用哪套工具链不由预设名决定，而由进入构建的那一步决定：
Windows 侧 `WindowsBuild.bat` 先调 `vcvars64`，容器侧 `env.sh` 设 `CC/CXX=gcc-13`，
Conan 再按各自平台的 profile（`Conan/Profiles/WindowsMsvc2022` 与 `LinuxGcc13`）指过去。

### Linux（容器内验证）

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
scripts/container/coverage.sh 55   # gcovr 汇总，阈值不过就红（判定由 --fail-under-line 做）
```

sanitizer 的运行期参数（含 LSan 那张只放过系统库字体缓存的抑制表）统一在 `env.sh` 里导出，
所以 `sanitize.sh` 与「`build.sh asan` + `test.sh asan`」两条入口给的是同一个结果。

要跑 TSan 这一档得放开 seccomp：这台内核（WSL2 6.18）下 libtsan 必须关 ASLR 才起得来进程，
而 `setarch -R` 走的 `personality` 被 Docker 默认 seccomp 档拒绝（实测 EPERM）。
脚本会在建之前先探这一点并当场报红，不会把「没跑」当成「跑过」：

```bash
docker run --rm -it --security-opt seccomp=unconfined -v "$PWD:/work" gyanisscribe-dev bash
```

## 打包

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File scripts\WindowsPackage.ps1
```

产出绿色目录 + zip + NSIS 安装器，落在 `dist/`。脚本用 `windeployqt` 按可执行体的**实际导入**摆好
Qt 运行时，然后在 staging 目录里把 `PATH` 收窄到只剩 `System32`、并清掉 `QTDIR` 与
`QT_PLUGIN_PATH` 那几条提示，再跑一次 `--selftest` 与 `ffmpeg.exe -version`。
为什么收窄：开发机的 `PATH` 上本来就有 Qt，缺了哪件 DLL 也会被本机那份悄悄补上，
「本机能跑」就不是「包装完能跑」的证据。这条判据本身也验过能不能红：在同一份 staging 里
把 `Qt6Widgets.dll` 挪开，`--selftest` 的退出码变成 `-1073741515`（`0xc0000135` = 找不到 DLL），
放回又回到 0。安装器带卸载段。

VC++ 运行库也随包带：可执行体导入 `MSVCP140.dll` 与 `VCRUNTIME140.dll`（还有 `_1`、`_2` 两份），
这几件不属于 Windows 自带的系统映像，新装机上要么先装 redist 要么程序起不来。打包脚本按编译器版本
从工具链的 `VC\Redist\MSVC\<版本>\onecore\x64\Microsoft.VC145.CRT` 取那五份摆到可执行体旁边
（Windows 先搜程序目录，加载的就是随包这一份），缺任一件当场中止；UCRT（`api-ms-win-crt-*`）不搬，
那是 10/11 自带的。`windeployqt --compiler-runtime` 在这套 Qt 上一个 CRT 都不落地（实测），所以自己拷。

「装完即用」这一条也真机量过：把 `dist/` 里的安装器静默装进一个空目录（`/S /D=<空目录>`），
装出来的目录里 `GyanisScribe.exe`、`ffmpeg.exe`、`tessdata/` 两份语言包、`licenses/`、
`translations/GyanisScribe_en.qm`、`platforms/qwindows.dll` 与 `Uninstaller.exe` 全在；
就在那个目录里把 PATH 收窄到只剩 `System32` 跑 `--selftest`，退出码 0，
`ffmpeg.exe -version` 也起得来（9.0.2-essentials_build）；随后 `Uninstaller.exe /S` 退出码 0，
装出来的目录和 HKLM 里那条卸载登记键都不在了。

## 验证与质量门禁

四条线，都是必建必测：本机 `debug` / `release` 两档，容器 `asan` / `coverage` 两档。
本轮（2026-10-11）实测的是 `01dd0d1` 之上的一份工作树改动，分三批：批一是缺陷整改（拖动标注的撤销并步、
历史页初读、录屏控制条的暂停键可用态、成品按实际落帧记尺寸、窗口截图接进延时通道、置信度改 `optional`），
批二给九档标注工具配字母快捷键，批三给贴图窗补键盘与双击操作；新增判据 16 条（7 + 3 + 6）。
本机两档就地跑；容器两档把同一份工作树同步进 `/work/scribe-audit` 再跑——同步按
`git ls-files` 加 `git ls-files -o --exclude-standard` 的清单打包，不能只跟 `git archive` 走：
后者只装已跟踪文件，新加的 `tests/Pin/TestPinWindow.cpp` 会被丢在容器外，
而那一条正好被已改过的 `tests/Pin/CMakeLists.txt` 指着，容器要在 generate 阶段才报「找不到源文件」，一次冷编就白烧了。

| 线 | 命令 | 结果 |
| --- | --- | --- |
| Windows Release | `scripts\WindowsBuild.bat release` | 405 条全绿（0 失败），4 条环境门控跳过，名单共 409 条 |
| Windows Debug | `scripts\WindowsBuild.bat debug` | 401 条全绿（0 失败），5 条跳过，名单共 406 条 |
| 容器 ASan(+UBSan) | `scripts/container/sanitize.sh asan` | 390 条全绿（0 失败），10 条跳过，名单共 400 条 |
| 容器覆盖率 | `scripts/container/coverage.sh 55` | 390 条全绿（0 失败），10 条跳过，名单共 400 条；行覆盖 **72.7%**（5142/7070），阈值 55%；主程序在 Xvfb 上 `--selftest` 退出码 0 |

名单从 393/390/384 涨到 409/406/400，涨的就是这 16 条新用例；它们在四条线里逐条点名跑到过（不是被跳过的那一格）。
每条新判据都做过突变证伪，且红集能按改动切开：批一是七处（并步、初读、暂停键、落帧尺寸、置信度两格、文案分格，
同文件的第二处突变在还原后单独一批做，不拿一次覆盖另一次的备份槽）；批二两处——字母不认工具打中
`ToolShortcutTable…` 与 `LetterKeySwitches…`，提示里撤掉键位打中 `ToolButtonTooltipsCarry…` 与
`ExposesEveryRequiredControl…`；批三两批对立方向——「功能撤掉」（一挡转 0 度 + 复位空实现）打中
`RotateKeys…`、`NegativeTurnFolds…`、`ZeroKeyAndDoubleClick…` 三条，而「守卫放宽」（双击不分左右键、
`0` 不看修饰键、复位连透明度一起抹）打中的是另外三条 `RightButtonDoubleClick…`、`UnboundKeysLeave…`
与同一条 `ZeroKeyAndDoubleClick…` 的透明度那一格——两批各自的红集不重叠，才说明这六条不是互相兜底。
每批改完都复跑到全绿再进下一批；批二的字母快捷键在容器 GCC 13 下另跑过一次 ASan（本机 MSVC 绿不构成那一档的证据）。
`asan` 与 `coverage` 两档的 `build_type` 就是 Debug，而 Debug 档不领 Tesseract（理由与撤除判据见 [docs/OcrBackends.md](docs/OcrBackends.md)）。
本机 Debug 那一档同样少这 4 条，另多 1 条「后端没编进来」的对照——两档条数不同是这套条件编译的结果，不是漏跑。
本机这一档跳过的四条全是要真 `ffmpeg` 的端到端（编码 2、设备枚举 1、取声 1）：这个工作树没跑
`scripts/FetchFfmpeg.ps1`，`Vendor/Ffmpeg/` 不在，所以四条一起跳——打过包的本机只跳取声那一条。
跳过的都是环境门控用例，本机两档与容器各跳哪些是清楚的：

| 跳过的用例 | 为什么跳 |
| --- | --- |
| `LanguageFixture.BuiltEnglishPackTranslatesKnownString` | 容器那份 Qt 6.4 没有 lrelease，出不了真实语言包，只能在有包的一档成立 |
| `OcrFixture.ReportsMissingBackendSeparatelyFromMissingLanguagePacks`（仅本机 Debug） | 这台机装了系统识别语言包，工厂先问 Windows 就拿到了后端，走不到「语言包在、后端没编进来」那一格；同一格由容器那两档跑 |
| `FfmpegAudioCaptureEndToEnd.*`、`VideoEncoderEndToEndFixture.*`、`FfmpegAudioDevicesEndToEndFixture.*` | 要真 `ffmpeg`；取声那一条还要一路真设备 |
| `CrashGuardEndToEnd.*` | minidump 走 Windows 的 dbghelp；Linux 侧这一格是空操作（core dump 归宿主配置） |
| `PlatformIntegration.EnumeratedWindowSitsWhereQtPutsIt` | 「屏上有哪些窗口」只有 Windows 后端给得出；能力位为假时这一格按降级出口跳过，回绝文案由同文件另一条用例钉住 |
| `ModuleFlowFixture.TrayEscapeHatchMakesThroughPinsClickableAgain` | 要真鼠标穿透能力才开得出「穿透中」的贴图，Linux 侧那一位为假；没有可复现的现场就跳过，不拿假现场充一条通过 |
| `QScreenCaptureSourceFixture.GrabbedCursorShapeIsSelfConsistent`、`...CursorDrawsIntoARealFrameWithoutBreakingItsGeometry` | 要真桌面才取得到系统指针的形状；容器与离屏档拿不到就跳过，本机在指针此刻隐藏时同样跳（这一档跳过是机器的现场，不是漏跑）。掩码与合成规则不靠这两格——`CursorPixels` 那四条用自造的位平面与自造指针跑，两边都跑得到 |

`scripts\WindowsBuild.bat` 在 `Vendor/Ffmpeg/ffmpeg.exe` 就位时会自动把它交给 `GYANISSCRIBE_FFMPEG`，
所以打过一次包的本机跑门禁时，编码与设备枚举这两条端到端是真的在跑（跳过的只剩取声那一条）。
自己指定仍然优先，本机没有随包那份时这样打开：

```bash
GYANISSCRIBE_FFMPEG=C:/path/to/ffmpeg.exe             # 编码端到端
GYANISSCRIBE_AUDIO_DEVICE=@device_cm_{...}\wave_{...} # 取声端到端（要用 dshow 的 Alternative name，不是友好名）
```

另有三条不编译源码的静态门禁，作为 ctest 用例注册在 `tests/Style/`：

| 用例 | 钉住什么 |
| --- | --- |
| `Style.QtStringPlaceholders` | `QString` 占位符不许 `{}` 与 `%1` 混用——混用编译期无声，运行时把「选择{}的目录」原样摆给用户 |
| `Style.TranslationCatalog` | 源码里每一条走 `uiText` 的文案都必须在 `.ts` 里有非空译文，否则英文档下会安静地显示中文 |
| `Style.SourceConventions` | 路径不得按窄串交给第三方库；按枚举分派的开关要覆盖每个值（MSVC 与 GCC 的默认告警都不报这种漏项） |

用例结构逐级镜像 `src/`，统一 GoogleTest，不引入第二个测试框架。
共用同一份外部资源（真实缓存目录、临时文件）的用例用 CMake 的 `RESOURCE_LOCK` 串行化——
`ctest -j` 把每条用例当独立进程启动，进程内互斥救不了。

## 项目结构

```
src/Core        值类型、日志、目录 provider、图片等比适配几何
src/Ui          设计令牌、主题、图标供给、Toast、快捷键输入框
src/Platform    平台能力抽象（Windows / Linux 两套后端）
src/Settings    配置读写与迁移       src/Hotkey    组合键解析与全局登记
src/Capture     抓帧、选区、遮罩     src/Annotation 标注工具与标注窗
src/Pin         贴图窗口与贴图管理   src/Record      录屏会话、帧时钟、ffmpeg 编码、控制条
src/Ocr         预处理、后端选型、结果窗口与导出
src/History     历史清单、保留策略、缩略图列表
src/Pick        取色器：颜色写法与放大镜几何
src/App         应用对象、核心控制器、托盘、主窗口、单实例、崩溃兜底
Vendor/WinRt    Windows 系统识别用的 C++/WinRT 投影头（MIT，生成与裁剪办法见 docs/OcrBackends.md）
tests/          逐级镜像 src 的目录结构；另有一条跨模块流程入口 tests/App/TestModuleFlow.cpp
Resources/Icons 65 张原创 SVG 图标（未接线的几枚记在路线图「图标接线」那一条）
scripts/        构建、打包、容器一键脚本与静态门禁
docs/           需求、识别后端、第三方许可证、界面样式说明
```

`tests/App/TestModuleFlow.cpp` 按生产形状造替身，钉的是模块接在一起时坐标系与语言词表还对不对——
这类漂移在单模块用例里看不见。

文档索引：

| 文档 | 内容 |
| --- | --- |
| [docs/ProjectRequirements.md](docs/ProjectRequirements.md) | 需求原文与硬约束（功能清单、性能指标、UI 约束、交付物） |
| [docs/OcrBackends.md](docs/OcrBackends.md) | 两个识别后端的选型顺序、语言包查找路径、WinRT 投影头怎么生成 |
| [docs/ThirdPartyLicenses.md](docs/ThirdPartyLicenses.md) | 随包分发的组件、许可证与合规路线 |
| [docs/UiStyleGuide.md](docs/UiStyleGuide.md) | 设计令牌、每个窗口的样式与状态、深浅主题对照、高 DPI 适配 |

## 性能实测

需求 §十一 的指标与它们的实际测量方法——没测过的直接写「未测」，不拿推测充数：

| 指标 | 本机实测 | 方法 |
| --- | --- | --- |
| 截图延迟 < 100ms | 中位 30–31ms（10 次采样，min 26 / max 35） | 独立探针走产品同一条路（`QScreen::grabWindow` → `toImage` → `convertToFormat(ARGB32_Premultiplied)`），丢掉首轮的一次性开销，两次独立运行给出 30 与 31ms。机器：2560×1440、dpr 1.0、单屏、Release Qt |
| OCR 单张 < 1s | 几十毫秒量级（640×160 的一次识别连同后端构造与 PNG 编解码，单用例计时 43ms） | ctest 的 per-test 计时；跨模块那条用 900×600 的图，337ms 里含 Qt 应用与引擎初始化 |
| 录屏 1080p30 不掉帧、CPU 占用合理 | **没达成**：1920×1080 选区、30 帧档，十秒实测只存下 113/300 帧（≈11.2fps），最大帧间隔 148ms，这一轮进程占掉 85.7% 一个核 | 真的 `RecordingService` 在真屏幕上跑十秒，数 `frameStored` 与帧间隔；CPU 取 `GetProcessTimes` 的进程时间除以墙钟。两道墙拆开看：抓一帧（2560×1440）中位 34ms——单这一项就超掉 30fps 的 33ms 预算，要放开帧率得换采集原语（Windows Graphics Capture / DXGI 桌面重复）；PNG 落盘中位 53ms/帧（约 210KB），那一段还留在界面线程上（见[路线图](#路线图后续计划)）。顺一条反直觉的实测：Qt 的 PNG 压缩档不是省时间的开关，`quality=1` 要 168ms，比默认档慢三倍 |
| 录屏成品的时长与录的那一段一致 | 6.00 秒那一轮编出来容器自报 6.12 秒；旧写法同一轮是 2.33 秒的快放 | 命令里的 `-framerate`（以及 GIF 滤镜里的 `fps=`）按「这一轮的帧数 × 1000 / 毫秒」写成有理数，不再用配置里那档目标帧率——短了的帧序列按目标帧率铺就是快放，音轨还会被 `-shortest` 截尾。端到端用例读 ffmpeg 自己报的 `Duration` 来断：三帧摊在两秒上，容器里就该写着 2.00 秒 |
| 编码前的编码器探测耗时 | 本机 1920×1080 单档实测：libx264 60ms（缺件立刻失败）、NVENC 187ms、QSV 1583ms、AMF 574ms、mpeg4 77ms；本机选中 NVENC 走的是前两档共 247ms，五档全试完是 2481ms | 三帧到 `null` 输出的探测命令，参数按真录屏尺寸、帧率与画面码率给；选中即停，只有 MP4 走这一步，GIF 不需要 |
| 贴图拖动流畅、鼠标穿透不抢焦点 | 定性验证：拖动走 `PinTransform` 的增量换算（有用例钉数值），穿透由平台侧 `setMouseThrough` 落地 | 主观流畅度无法用断言钉；这里只承诺「没有动画在拖动的路上插队」 |

## 已知问题

- Windows 上识别首选系统组件（`Windows.Media.Ocr`），它用的是 Windows 自己装的语言包，不需要用户放文件；
  走 Tesseract 那一条也不用放——语言包由安装包带到 `<程序目录>/tessdata`，查找顺序把这一份排在用户数据目录之前。
  两样都缺时明确报「不可用」并给出放置位置，不会返回空结果冒充「这张图里没字」。
- 录屏的编码走 `ffmpeg` 子进程：随包分发那一份，**不链接**（避开 GPL 传染），所以本体源码不受它约束；
  查找顺序是「程序目录里随包那份 → PATH」。装的那份没有 `libx264` 时编码前会自己探一个开得起来的编码器顶上，
  五个候选全都开不起来才报失败。
- Debug 档不领 Tesseract：上游 leptonica 的 Debug 包库名与其 `cpp_info` 不一致，
  `conanfile.py` 里按 `build_type` 分岔并写明撤除判据。Windows 的 Debug 档仍能真识别——工厂先问系统识别。
- Linux 侧能力不全：没有全局热键后端（登记会给出中文提示并降级）、没有录屏取声、
  无系统托盘时菜单改由主窗口提供、`Qt::ColorScheme` 要 6.8 才有而容器是 6.4（跟随系统会解算到兜底档）。
- 滚动长截图的接缝按「整段逐字节相同」认（`FrameStitch.cpp` 里按行 `memcmp`）：选区里有闪烁的光标、在放的视频
  或动图时，那两帧怎么都对不上，于是接不下去——它报「这一帧没接上」而不是悄悄接歪一张。
  要吃得下会动的内容得换成容差匹配，而那条判据先得能证伪「容差把两页开头的空白当成缝」。
- 录屏跟不上目标帧率时，成品按这一轮**实际达成**的那一档帧率铺时间轴：画面不快放，只是变卡，
  音轨也不再被截尾。达成率与墙钟对齐这件事写在[性能实测](#性能实测)那一格里，有端到端用例钉着。
- 两处路径仍按本地码页交给第三方库（各自在同一句里写明理由，`Style.SourceConventions` 认这些理由）：
  spdlog 的日志文件名（本构建没开 `SPDLOG_WCHAR_FILENAMES`，窄串要按本地码页给）与 Tesseract/Leptonica 的
  `TessBaseAPIInit3`（C 接口拿 `const char*` 开文件）。要彻底了断 spdlog 那一处，得在 Conan 侧把 wchar
  文件名选项打开，再回到 `toDisplayText` 那条路上。

## 路线图（后续计划）

按需求条目核对过，剩下这些没做，都在等一个能验证它们的环境：

- **长录屏**：帧序列按 PNG 逐帧落盘，1080p 十分钟就是 GB 级（实测屏幕内容一帧约 210KB，15 帧率十分钟约 1.9GB）；
  换成喂 ffmpeg `rawvideo` 管道才能放开时长上限。
- **录屏逐帧写 PNG 仍在界面线程上**：需求的硬约束要求「耗时操作放工作线程」，现在只剩这一处
  （`PngSequenceSink::write`：一帧的写盘时间直接算进下一格的等待）。
  文字识别与停止后的编码都已交出去：`Ocr::RecognitionRunner` 与 `RecordingService::startEncoding`
  都走 Qt 的全局线程池，认完/编完由 `QFutureWatcher` / `QFuture::then`（context 是界面线程那个对象）送回界面，
  两处都同一时间只跑一条，析构前等手上的任务跑完。
  **恢复条件**：这一处要与采集循环交接帧与状态，并发改动要有 TSan 才算判据（本机 MSVC 没有 TSan，
  而容器里 `setarch -R` 被 Docker 默认 seccomp 拒绝），先用 `--security-opt seccomp=unconfined` 起一档、
  证明旧写法报得出竞态，再动它。没有这一档之前把跨线程手抖写进去，只是把可复现的卡顿换成不可复现的竞态。
- **Linux 录屏取声**：Windows 那一路已经落地，Linux 的入口是 ALSA/Pulse，容器与本机的验证环境都没有
  可登记的音频设备，写进去就是一段没人验过的代码。现在那一侧列出设备与取声都直接给出中文降级原因。
- **录屏编码器只自动选，不给设置项**：需求的参数清单里有「编码器」，现在做法是编码前用真 ffmpeg 探一遍
  （三帧到 `null` 输出），按 libx264 → NVENC → QSV → AMF → mpeg4 取第一个开得起来的，界面上没有那一格。
  探测代替名单是因为实测：本机那份 ffmpeg 的 `-encoders` 里 h264_qsv、h264_amf 都在，真跑却分别报
  「MFX 不支持」与「amfrt64.dll 打不开」；h264_mf 在名单里也起得来，连跑五次有四次自己崩了。
  要给这一格的前提：把探出来的可用档位列进下拉（与音频设备那两格同一条形状），而不是把五个名字静态摆出来——
  选出这台机器开不起来的那一档，就是让整段录屏白录。
- **Linux 全局热键**：`Platform` 抽象下只有 Windows 后端有真登记，Linux 侧给出中文降级提示；
  要做得接 X11/Wayland 各自的快捷键协议。
- **界面截图与演示**：本文档暂时没有截图，等打包产物稳定后补真实界面图，不放占位图。
- **录屏的采集原语没换**：性能那一格 1080p30 没达成，根因是抓一帧中位 34ms 就把 33ms 的预算吃光（数字见上方实测）。
  放开帧率要换 Windows Graphics Capture 或 DXGI 桌面重复，接口位已经在 `Capture/IScreenCaptureSource.h` 写明；
  这一条排在「长录屏」之前——帧序列换成 `rawvideo` 管道也救不了抓帧本身慢。
- **录屏最长时长是个常量**：默认 600 秒、硬上限 7200 秒，到点自动收尾，界面上既没有这一格也不说这句——
  录满十分钟自己停下来，用户只能猜。要么给它一个设置项并把「到点就收」写进控制条的提示，要么放开上限交给磁盘。
- **GIF 的帧率天花板与设置页不同解**：`RecorderConfig` 判 GIF 只能到 24 帧，设置页那一格却允许填到 60，
  差别要到开录那一刻才拒。修法是按当前输出格式算那一格的取值范围，而不是让用户撞一次拒绝。
- **指定屏幕截图还没有入口**：`captureFullScreen(screenName)` 这条通道与它内部按名字找屏的逻辑都在
  （跨模块用例就在点名一块屏），但托盘与主窗口只放「整屏截图」一条，走的是光标所在那一块。
  多屏上想点名另一块屏，得先给这一条加上一份屏幕清单的入口。
- **`AppController` 那一层没有直测**：构造函数自己造出设置、抓帧、托盘、编码这些真模块，没有注入口，测试里一个实例都没有。
  延时截图的路由与成品进历史那几条都住在这一层，本轮「截取当前窗口绕过延时」就是这么漏下来的——只能靠人读代码发现。
  前置工作是给它一套与 `tests/App/TestModuleFlow.cpp` 同形状的替身。
- **图标接线**：`Resources/Icons` 里有 16 枚在界面上一次都不出现（`CaptureWindow`、`RecordFullScreen`、`RecordRegion`、
  `RecordWindow`、`Microphone`、`SystemAudio`、`CursorHighlight`、`Countdown`、`OcrLanguage`、`OcrRegion`、`PinTop`、
  `PinTransparent`、`PinSave`、`ThemeLight`、`ThemeDark`、`ThemeSystem`）。它们由 `tests/Ui/TestIconProvider.cpp`
  那份独立清单逐张核对画得出来，所以不是坏资产，只是没人摆：接到对应的行上（录屏三档范围、两路取声、主题档位下拉），或者删掉。
- **剪贴板里的文本与颜色还不能转贴图**：现在这一条只认图像（图像文件本身与其像素都算）。
  文本要渲染成图、颜色要画成色卡，两条都需要一套「拿什么字体、多大、留多少边距」的决定，
  没想清之前不做——做半条的代价是用户贴出一张排版随缘的图。
## 参与贡献

改动要么修掉错误行为，要么加上需求里的能力。上手顺序：

1. 按[从源码构建](#从源码构建)跑一次本机的构建脚本——它一条命令包含配置、编译与 `ctest`，绿了才谈改动。
2. 写完先跑 clang-format（仓库根的 `.clang-format`，`ColumnLimit: 180`），只递给源文件，别把 `CMakeLists.txt` 喂进去。
3. 再跑对应平台的门禁：本机 `scripts\WindowsBuild.bat <preset>`；改到并发、生命周期或跨线程代码时，
   容器两档也要跑（`sanitize.sh`、`coverage.sh`）。**本机全绿不构成并发正确性的证据**——MSVC 没有 TSan。
4. 加功能要带用例，`tests/` 的目录结构逐级镜像 `src/`；每条用例说明它钉住什么，并且能红：
   把实现改回缺陷行为确认这一条会失败，再提交。
5. 提交信息用中文写「为什么」而不是「做了什么」，一次提交只讲一件事，
   格式是约定式提交 `<类型>[(范围)]: <描述>`（类型取 `feat` / `fix` / `refactor` / `perf` / `test` / `docs` / `build` / `chore`）。

规范要点（细节见 [docs/ProjectRequirements.md](docs/ProjectRequirements.md) 与 `tests/Style/` 三条门禁）：

- 文件与类名 PascalCase、接口 `I` 前缀、函数 camelCase、成员 `m_` 前缀、常量 `k` 前缀，标识符不用缩写。
- 声明处中文 Doxygen（重写的方法要独立写全），实现体在分支、状态流转、加锁、魔法数字处写中文 `//`；
  注释只留「为什么」，不写实现历史与「旧实现怎样、现在怎样」。
- 可恢复错误返回 `std::expected<T, Error>`，错误文案中文且可操作（写清「原因 + 替代做法」）；
  不许静默失败、取整、回绕、把「不支持」悄悄当成「按默认处理」。
- 一切要显示给用户的文本走 `Core::uiText()`，并且同步进翻译目录（`Style.TranslationCatalog` 会拦漏项）。
- 图标必须是原创 SVG：`viewBox="0 0 24 24"`、`currentColor`、描边 1.5 或 2、圆角端点，
  只经 `IconProvider` 加载；不下载、不引用、不复制任何现成图标库或任何现有产品的素材。
- 零告警是硬判据：MSVC 侧每个目标都带 `/W4 /WX`；容器侧的 GCC 预设没开 `-Wall`，
  这类编译器不报的漏项由 `Style.SourceConventions` 与用例兜住。

## 许可与第三方声明

本项目的代码、界面与图标均为原创。第三方组件的许可证、用途与分发方式逐条列在
[docs/ThirdPartyLicenses.md](docs/ThirdPartyLicenses.md)：Qt 6 走 LGPL-3.0 的动态链接路线，
Tesseract / Leptonica / spdlog / GoogleTest / C++/WinRT 投影头各自按上游许可，
`ffmpeg` 只作为外部进程调用、**不链接**（避开 GPL 传染），随包那一份按上游原样分发并附许可证正文与源码获取说明。
识别语言包（`eng`、`chi_sim`）随包分发在安装目录的 `tessdata/` 里，查找顺序见 [docs/OcrBackends.md](docs/OcrBackends.md)。

本软件**免费供任何人使用**，条款见仓库根的 [`LICENSE.txt`](LICENSE.txt)（安装器的许可页就是它）。
协议里有一处刻意的放宽：为保证第三方许可证合规而做的反向工程是允许的——LGPL 要求必须留这一条。
各第三方组件的许可证正文随包发在 `licenses/` 下，仓库里也能直接看：Qt（LGPL-3.0）、
FFmpeg（GPL-3.0，未修改的官方构建，对应源码由上游公开发布）、Tesseract 与语言包（Apache-2.0）、
Leptonica（BSD-2-Clause）、spdlog（MIT）、GoogleTest（BSD-3-Clause，只进测试）、C++/WinRT 投影头（MIT）。
