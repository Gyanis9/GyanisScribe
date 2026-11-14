# 智录 · GyanisScribe

<p align="center">
  <img src="src/Gui/Resources/Brand/AppIcon128.png" width="96" alt="智录图标">
</p>

<p align="center">
  <b>截图 · 标注 · 贴图 · 取色 · 录屏 · 文字识别</b><br>
  Windows 优先的桌面效率工具，常驻托盘、热键直达；界面、图标与代码全部原创。
</p>

<p align="center">
  <img src="https://img.shields.io/badge/C%2B%2B-C%2B%2B23-00599C?logo=c%2B%2B&logoColor=white" alt="C++23">
  <img src="https://img.shields.io/badge/Qt-6.11-41CD52?logo=qt&logoColor=black" alt="Qt 6.11">
  <img src="https://img.shields.io/badge/CMake-3.28%20%2B%20Conan%202-064F5C" alt="CMake and Conan">
  <img src="https://img.shields.io/badge/Platform-Windows-informational" alt="Windows">
  <img src="https://img.shields.io/badge/version-0.0.1-lightgrey" alt="version 0.0.1">
</p>

<p align="center">
  <img src="docs/images/action-annotation.png" width="820" alt="标注窗：在截下来的画面上画框、箭头、序号与高亮">
</p>

## 功能特性

| 能力 | 一句话 |
| --- | --- |
| **截图** | 全屏 / 框选 / 窗口 / 多屏 / 高 DPI，悬停认窗口与界面元素，放大镜与像素读数随行，滚动长截图，快速保存一键存盘 |
| **清场截图** | 收起桌面图标、最小化其他窗口再铺遮罩，截完自动还原；平台不支持时入口不出现 |
| **长截图** | 滚动接图按容差认缝：零星动效与闪烁光标不再断；固定标题栏只留一次、底部输入框不进成品；没有可信接缝时明确报错并给出重来方法 |
| **美化分享** | 标注成品套模板：背景、圆角、投影、水印、代码窗口外观；模板本地保存，导出不改原图 |
| **步骤记录** | 记录点击与窗口切换，自动编号配说明与截图；审核后可导出 HTML/PDF 教程，钩子只在录制时存在 |
| **元素拾取** | 悬停选中界面元素，按元素矩形截取并复制元素上下文；失败可回退矩形框选 |
| **Bug 报告** | 标注成品一键生成结构化报告：自动带系统/屏幕/窗口/时间，截图本地检查并默认打码，可编辑复现步骤，复制文本或存 `.md`+`.png`；不上传 |
| **截图上下文** | 标注窗旁可浮截图信息面板（应用 / 窗口 / 屏幕 / 时间），文件名模板支持 `%app%`、`%title%`、`%monitor%` 与实时预览；默认关闭、只在本机 |
| **回溯截图** | 默认关闭、只存内存的最近一段屏幕画面回看：`Ctrl+Alt+Z` 选帧后照常框选、标注与交付，也能把一段区间导出成 MP4 片段；不写盘、不上传、不录音 |
| **标注** | 十四件工具：矩形、椭圆、箭头、直线、画笔、高亮、马赛克、高斯模糊、文字、序号、橡皮擦、折线、放大镜等 |
| **隐私打码** | 标注窗里本地检查手机号、证件号、银行卡、邮箱、IP 与二维码，逐项确认后批量马赛克/模糊，整批一次撤销；不联网、不上传、不落盘 |
| **截图记忆** | 只索引主动截图的本地 OCR 索引：关键词、应用与时间筛选，重复图合并，旧图导入；只存本机、不上传、不录屏，可随时关闭清空 |
| **贴图** | 把图钉在屏幕最上层对着用：缩放旋转、分组、吸附对齐、多选、动图续播、拖文件与链接上屏 |
| **取色** | 冻帧加放大镜，六种写法轮换，最近取色一排随时取回 |
| **录屏** | 整屏 / 选区 / 窗口，MP4 与 GIF，编码器真探测，麦克风与系统声可混音 |
| **识别** | PaddleOCR（PP-OCRv6）本地识别，Windows 系统识别兜底；二维码 / 条形码识别，结果可导出 |
| **识别结果动作** | 认出文字可复制 Markdown；检测到简单表格可导出 CSV（UTF-8 BOM，Excel 直接打开），复杂表只提示不导出 |

每个功能的完整行为与每一格设置的含义见 [使用指南](docs/UserGuide.md)。

## 截图：框到哪，读到哪

按下热键整屏冻帧，拖框时选区里浮出三分网格，放大镜跟着光标报坐标与颜色；
悬停能认出光标压着的窗口甚至按钮一级的界面元素，原地按一下即选中。
尺寸要精确有键盘路：`S` 敲「宽x高」、`A` 锁长宽比。

<p align="center">
  <img src="docs/images/action-capture.png" width="820" alt="截图遮罩：拖框中的选区、三分网格与放大镜读数">
</p>

一键清场（`Ctrl+Alt+X`）先把桌面图标收起来、让其他窗口让开，截完自动放回；
滚动长截图（`Ctrl+Alt+L`）按容差认缝接图；全局快速保存（`Ctrl+Alt+S`）跳过编辑，直接存文件并送进剪贴板。

## 标注：画完即成品

色板、线宽、字号、填充、线型、箭头款式逐档可调，色板还能换成自己的一套并导出分享；
选中即拖、多选整组改，撤销重做一路到底；`F5` 就地重抓这一块屏幕，画好的标注原地留着。
标注成品还能直接送去打码、套模板分享或生成 Bug 报告。

## 贴图：钉在屏幕上对着用

截下来的、剪贴板里的、盘上的文件、网页图片地址，都能贴成一张置顶的图；
滚轮缩放、`Ctrl`+滚轮旋转、`Shift`+滚轮调透明度，拖到屏边自动收起，按住 `Shift` 拖动与别的窗口对齐。
贴图上还能直接标注、裁剪、取色与识别。

<p align="center">
  <img src="docs/images/action-pin.png" width="680" alt="两张贴图钉在桌面窗口之上">
</p>

## 识别：文字、表格、条码

本地识别默认用随包的 PaddleOCR（PP-OCRv6 small 模型，ONNX Runtime 推理），模型或运行库不可用时回落到
`Windows.Media.Ocr`（用系统自己装的语言包），装完即用，不需要另放文件。
结果窗左侧原图、右侧文本，点一行就在图上圈出那一行；可导出 TXT / JSON，可换语言重认。

<p align="center">
  <img src="docs/images/action-ocr.png" width="760" alt="识别结果窗：左侧原图与词框，右侧文本、复制 Markdown 与导出">
</p>

认出文字后可复制 Markdown；检测到简单表格可导出 CSV（UTF-8 BOM，Excel 双击不乱码），
复杂表只提示不导出。二维码与条形码由 zxing-cpp 认，认出即进剪贴板。

## 录屏与回溯：从整段到一帧

整屏 / 选区 / 窗口，MP4 与 GIF，编码器真探测，麦克风与系统声可混音；控制条可暂停、可取消，
停止后后台编码不冻界面。

<p align="center">
  <img src="docs/images/action-record.png" width="680" alt="录屏控制条：录制状态、已录时长与暂停/停止按钮">
</p>

回溯截图默认关闭、只存内存的最近一段画面：`Ctrl+Alt+Z` 唤出胶片条，候选帧标出；
选一帧照常框选、标注与交付，`[` / `]` 圈一段区间就能导出 MP4 片段。

<p align="center">
  <img src="docs/images/action-rewind.png" width="820" alt="回溯胶片条：候选帧、区间标记与导出片段按钮">
</p>

## 步骤记录：点击即教程

托盘或 `Ctrl+Alt+T` 开始一次步骤记录，只记左右键点击与前台窗口切换；每步自动配整屏截图与说明，
停止后进审核窗改字、补步、调序，再导出单文件 HTML 或 PDF（A4）。
截图与说明只在本机内存与临时目录里，不写历史、不上传；不自动打码，导出前请自行检查。

<p align="center">
  <img src="docs/images/action-steps.png" width="760" alt="步骤审核窗：三步记录、缩略图、说明编辑与导出 HTML/PDF">
</p>

## 打码、分享与报告

敏感信息检测只在本机跑：手机号、证件号、银行卡、邮箱、IP 与二维码逐项确认后批量马赛克或高斯模糊，
整批一次撤销。分享给标注成品套上背景、圆角、投影与水印，导出不改原图。
Bug 报告把系统与窗口信息、编辑好的复现步骤和一张默认打码的截图装进一份 `.md` 加 `.png`，不上传。

<table>
  <tr>
    <td align="center"><img src="docs/images/action-privacy.png" width="400" alt="隐私打码审核：逐条确认手机号、证件号、邮箱与银行卡"><br><sub>隐私打码：逐项确认后整批打码</sub></td>
    <td align="center"><img src="docs/images/action-share.png" width="400" alt="美化分享：模板、背景、圆角、投影与水印实时预览"><br><sub>美化分享：模板与水印实时预览</sub></td>
  </tr>
</table>

<p align="center">
  <img src="docs/images/action-bugreport.png" width="820" alt="Bug 报告窗：自动打码的截图预览、复现步骤表单与报告预览">
</p>

## 设置页一览

主窗口十页设置的真实渲染（离屏抓取控件自绘画面，不是设计稿）。出图工具与重拍办法见 [开发指南](docs/Development.md)。

<details>
<summary>展开全部十页设置</summary>

<table>
  <tr>
    <td align="center"><img src="docs/images/main-general.png" width="380" alt="主窗口 · 通用页"><br><sub>通用</sub></td>
    <td align="center"><img src="docs/images/main-capture.png" width="380" alt="主窗口 · 截图页"><br><sub>截图</sub></td>
  </tr>
  <tr>
    <td align="center"><img src="docs/images/main-annotation.png" width="380" alt="主窗口 · 标注页"><br><sub>标注</sub></td>
    <td align="center"><img src="docs/images/main-pin.png" width="380" alt="主窗口 · 贴图页"><br><sub>贴图</sub></td>
  </tr>
  <tr>
    <td align="center"><img src="docs/images/main-record.png" width="380" alt="主窗口 · 录屏页"><br><sub>录屏</sub></td>
    <td align="center"><img src="docs/images/main-ocr.png" width="380" alt="主窗口 · 文字识别页"><br><sub>文字识别</sub></td>
  </tr>
  <tr>
    <td align="center"><img src="docs/images/main-hotkey.png" width="380" alt="主窗口 · 快捷键页"><br><sub>快捷键</sub></td>
    <td align="center"><img src="docs/images/main-theme.png" width="380" alt="主窗口 · 主题页"><br><sub>主题</sub></td>
  </tr>
  <tr>
    <td align="center"><img src="docs/images/main-history.png" width="380" alt="主窗口 · 历史页"><br><sub>历史</sub></td>
    <td align="center"><img src="docs/images/main-about.png" width="380" alt="主窗口 · 关于页"><br><sub>关于</sub></td>
  </tr>
</table>

</details>

浅色、深色、高对比度三套主题加跟随系统；高 DPI 与多屏；中英双语界面。

## 下载安装

- **安装器 / 绿色包**：到 [Releases](https://github.com/Gyanis9/GyanisScribe/releases) 取 NSIS 安装器或 zip 绿色目录，装完即用——`ffmpeg` 与 PP-OCRv6 识别模型随包带，不需要另装任何东西。
- **便携模式**：程序目录旁放一个空的 `portable.txt`，配置改读写同目录下的 `GyanisScribe.ini`。
- **自构建**：见下面[从源码构建](#从源码构建)。

关于页的「检查更新」按一下才联网去本仓的 GitHub Releases 问最新正式版，有新版就把版本号与下载页摆出来。
这是程序唯一的联网点，只做「查」不做「换」。

## 快速开始

```powershell
GyanisScribe.exe            # 起托盘，按 Ctrl+Alt+A 截第一张图
GyanisScribe.exe --help     # 启动参数
GyanisScribe.exe --selftest # 把启动路径跑完就退：日志就绪且全局热键全部登记上才退 0
```

带参数就是「跑一件事」：`--capture=full|region|window` 截一张、`--pin <文件>` 把盘上的图贴到屏幕上、
`--pick` 进屏幕取色；`--to=file,clipboard,pin` 点名交付去处，`--exec="命令 --path %file%"` 截完接着跑一条外部命令。
完整参数见 [使用指南 · 命令行](docs/UserGuide.md#命令行)。

严格自检要求那几颗全局热键在本机空闲；热键被别的程序（或常驻的另一份智录）占着时它会退 1。
只想量「包装完起不起得来」时，先设环境变量 `GYANISSCRIBE_SELFTEST_SKIP_HOTKEYS=1`：自检照常建
控制器、托盘与主窗口，只是跳过全局热键登记，退出码只看日志与装配是否就绪。打包脚本用的正是这一档。

程序常驻托盘，主窗口随时从托盘菜单打开；全局热键撞了可在设置里改，改完立刻生效。

### 全局热键

| 动作 | 默认组合 |
| --- | --- |
| 框选截图 | `Ctrl+Alt+A` |
| 清场截图 | `Ctrl+Alt+X` |
| 拾取界面元素 | `Ctrl+Alt+E` |
| 回溯截图 | `Ctrl+Alt+Z` |
| 录屏 / 停止并保存 | `Ctrl+Alt+R` |
| 步骤记录 | `Ctrl+Alt+T` |
| 文字识别 | `Ctrl+Alt+O` |
| 屏幕取色 | `Ctrl+Alt+P` |
| 滚动长截图 | `Ctrl+Alt+L` |
| 快速保存 | `Ctrl+Alt+S` |
| 收起 / 摆回全部贴图 | `Ctrl+Alt+H` |
| 切换贴图分组 | `Ctrl+Alt+G` |

十二个固定热键之外还能给任意功能再绑十个；截图遮罩、标注窗与贴图窗的窗内键见 [使用指南 · 键位表](docs/UserGuide.md#键位表)。

## 性能亮点

Release 档本机实测（2560×1440 单屏），读数用例只报数不断言，方法与全表见 [开发指南 · 性能实测](docs/Development.md#性能实测)：

| 指标 | 实测 |
| --- | --- |
| 抓一帧（DXGI 桌面重复） | 中位 **6.0ms** |
| 整条框选链（铺遮罩 → 拖框 → 交付） | 中位 **8.5–10.9ms** |
| 冷启动到自检就绪 | 中位 **391ms** |
| 4K 图识别一次 | 中位 **428ms** |
| 标注窗整幅重绘（30 个图形） | 中位 **3.7ms** |

识别那一行是换 PP-OCRv6 载荷之前的读数，新后端的实测待补。

## 从源码构建

```bat
set QTDIR=G:\Tools\Qt\6.11.1\msvc2022_64
scripts\WindowsBuild.bat release
```

一条命令包含配置、编译与 `ctest`。前置组件表、打包脚本与门禁口径见 [开发指南](docs/Development.md)。

## 项目结构

```
src/Base        无业务公共底座：值类型、日志、目录 provider、文案与本地化设施
src/Core        领域实现：图像、设置、热键、取色、历史、贴图、截图、识别、录屏、标注
src/App         应用服务集：命令行、抓取意图与交付、托盘、剪贴板贴图、自定义热键、单实例、崩溃兜底、更新检查
src/Main        程序骨架：Application、AppController、MainWindow、Main 入口
src/Gui         界面层：设计令牌与主题、通用控件、图标与样式表，及各功能的窗口
src/Plugins     可换用后端：条码识别、抓帧来源、识别引擎、录屏编码
src/Platform    平台能力抽象（热键、窗口几何、自启、系统集成）
tests/          逐级镜像 src 的目录结构，统一 GoogleTest
```

## 文档索引

| 文档 | 内容 |
| --- | --- |
| [docs/UserGuide.md](docs/UserGuide.md) | 各功能完整行为、设置项含义与窗内键位 |
| [docs/Development.md](docs/Development.md) | 构建、打包、质量门禁、性能实测与贡献规范 |
| [docs/Roadmap.md](docs/Roadmap.md) | 差距审计台账、后续计划与已知问题 |
| [docs/OcrBackends.md](docs/OcrBackends.md) | 两个识别后端的选型顺序与模型目录查找路径 |
| [docs/ThirdPartyLicenses.md](docs/ThirdPartyLicenses.md) | 随包分发的组件、许可证与合规路线 |
| [docs/UiStyleGuide.md](docs/UiStyleGuide.md) | 设计令牌、窗口样式与深浅主题对照 |

## 参与贡献

1. 按 [从源码构建](#从源码构建) 跑一次门禁脚本，绿了才谈改动。
2. 改到 C++ 面先跑仓库根的 clang-format，再跑 `scripts\WindowsBuild.bat <preset>`。
3. 加功能要带用例：`tests/` 逐级镜像 `src/`，每条用例说明它钉住什么，并且改回缺陷行为会红。
4. 提交信息用约定式提交写中文「为什么」：`<类型>[(范围)]: <描述>`。

规范要点与静态门禁清单见 [开发指南 · 参与贡献](docs/Development.md#参与贡献)。

## 许可与第三方声明

本软件**免费供任何人使用**，条款见仓库根的 [`LICENSE.txt`](LICENSE.txt)。
代码、界面与图标均为原创；第三方组件（Qt、FFmpeg、ONNX Runtime、PaddleOCR、spdlog、zxing-cpp 等）的许可证与分发方式逐条列在
[docs/ThirdPartyLicenses.md](docs/ThirdPartyLicenses.md)，许可证正文随包发在 `licenses/` 下。
