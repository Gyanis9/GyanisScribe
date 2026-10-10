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
| C++/WinRT 投影头 | cppwinrt v2.0.250303.5 从 Windows SDK 10.0.26100.0 元数据生成 | MIT | Windows 系统识别的编译期投影 | vendor 在 `Vendor/WinRt/`，只进编译，不作为独立件分发 |

Qt 走 LGPL-3.0 的「动态链接 + 可替换」路线：用户拿到的是 Qt 的 DLL，可自行换成同版本库；
构建脚本不静态链接 Qt，也不做只读合并。产品若要在禁用逆向工程的场合分发，需要商业授权，
这一条属于发布方的决定，代码侧不做假设。

## 识别语言包

`*.traineddata`（`eng`、`chi_sim`）**不随安装包分发**：官方 Tesseract 语言包是 Apache-2.0，
可以随包，但体积与更新节奏更适合让用户自己放。查找顺序写在 `docs/OcrBackends.md`：
设置项 `Ocr/TessDataDirectory` → 环境变量 `TESSDATA_PREFIX` → 应用数据目录下的 `tessdata`。
缺语言包时识别功能会明确报「不可用」并给出放置位置，不装成能用而返回空结果。
Windows 上这一节只在选用 Tesseract 时才需要用户动手：系统识别走 Windows 自己的语言包
（设置 → 时间和语言 → 语音 里的「光学字符识别」），不需要往任何目录放文件。

## 录屏用的 FFmpeg（外部进程，不链接）

录屏的编码一步调用用户机器上的 `ffmpeg` 可执行文件（`QProcess` 起进程），本项目不链接
FFmpeg / x264 的任何库，也不把它们打进包里。这样做的结果是：

- 产品本体不被 GPL 传染：FFmpeg 若含 x264 等 GPL 组件，那是用户自己安装的独立程序；
- 用户换 FFmpeg 版本不影响本程序构建；
- 代价是必须自己检查：没装 FFmpeg 时录屏会失败并给出可操作的中文提示（装它，或改用其它编码器）。

需要用户装的东西：`ffmpeg`（LGPL-2.1+/GPL-2.0+，按构建而定）。本项目不重新分发它，
也不从其输出中嵌入任何素材。

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
- 本页与 `README.md`、`docs/OcrBackends.md` 一起随包发布（`scripts/WindowsPackage.bat` 会把它们拷进包目录）。
