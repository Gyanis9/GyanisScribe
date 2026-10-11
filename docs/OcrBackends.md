# OCR 后端选型与状态

## 现在有什么

- `IOcrEngine`（同步接口）+ `WinRTImageOcrEngine`（Windows 系统识别）+ `TesseractOcrEngine`（跨平台真后端）
  + `UnavailableOcrEngine`（降级桩）+ 后端选型 `createOcrEngine()`，用例逐条摆在 `tests/Ocr/`（条数不写在这儿，加一条就漂移一次）。
- 预处理链只有「灰度 → 最近邻放大（上限 2×）」两环：二值化与中值滤波刻意不做——实测在系统后端上，
  同一张「小字灰底带噪点」的图原样 4 词、灰度+放大 4 词（并把「741g」认回「7419」），再加二值化与中值滤波后 0 词，
  判据与理由写在 `src/Ocr/OcrPreprocessor.h` 的 `@warning`。
- 结果导出：TXT 与 JSON（JSON 带 language、averageConfidence、imageSize、逐词方框；
  后端不报置信度时那一项是 null，界面上那一格写「未提供」，不打 0%）。
- 语言标签词表对外只有 `zh-Hans` / `en` 两个，磁盘上的模型名（`chi_sim` / `eng`）在引擎内部映射。

## 后端选型按「先问系统、再看语言包」排，两档形状不同

`createOcrEngine()` 的顺序是：Windows 上先问 `WinRTImageOcrEngine`（宏 `GYANISSCRIBE_HAS_WINRT_OCR`），
可用就直接交回去；不可用才找 Tesseract（宏 `GYANISSCRIBE_HAS_TESSERACT`，它只在 Release 档进依赖图）；
两条都没有才落到 `UnavailableOcrEngine`。三个分支都被门禁编到（Debug 与 Release 两档 × 系统识别有无语言包），
所以没有「声明了却没编过」的分支。

- Debug 档为什么不带：上游 leptonica 的 Debug 包把库名写成 `leptonica-1.87.0`，而它自己的 `cpp_info`
  里找不到这个文件，tesseract 在 Debug 下 `cmake configure` 必失败。实测两轮：第一轮卡在 WebP 的版本判定
  （`find_dependency(WebP 0.5.0)` 与 libwebp 1.6.0 主版本不同），关掉 webp 之后第二轮卡在库名。
- 撤掉这条不对称的判据：`conan install --requires=tesseract/5.5.2 -s build_type=Debug --build=missing`
  能 configure 过，就把 `conandata.yml` 的 `releaseOnlyRequirements` 并回 `requirements`，
  删掉 `conanfile.py` 里那段 `build_type` 判断与 `src/Ocr/CMakeLists.txt` 的 `if (Tesseract_FOUND)` 分支。
- 包是按静态库给的（编译行里带 `-DLIBJPEG_STATIC` 一类开关），`bin/` 只有四个 Qt 的 DLL，
  Release 的 `--selftest` 与 120 条用例都能跑起来，不需要额外部署 `tesseract.dll`。
- 模型可用性只认「目录里有认识的语言对应的 `*.traineddata`」。空文件冒充的模型不算识别成功：
  `recognize()` 返回 `InternalError` 并写明「初始化语言 X 失败」，这条由用例
  `PrefersTesseractWhenLanguagePacksArePresent` 钉住（它同时证明库真的在运行，而不是被 `#if` 掉了）。
  这台机器有系统识别时工厂不会把 Tesseract 交出来，那条用例就按能力改取「直接构造 TesseractOcrEngine」——
  验的仍是「语言包在位这条路是通的」，工厂顺序另有 `FactoryPrefersSystemOcrAndFallsBackWithActionableReason` 钉。
- 迭代用 Tesseract 的 C 接口：函数是 `extern "C"` 的全局名，而 `RIL_*` 这组枚举在 C++ 模式下住在
  `namespace tesseract` 里（capi.h 在 C++ 分支走 `typedef tesseract::PageIteratorLevel`），
  两类名字的前缀不能一起加或一起去。
- 语言包（`*.traineddata`）**不依赖构建期联网下载**（编码规范第 25 条：要么随包发布，要么由用户放置）；
  查找顺序为设置项 `Ocr/TessDataDirectory` → 环境变量 `TESSDATA_PREFIX` → **程序目录下的 `tessdata`（随包那份）** → 应用数据目录下的 `tessdata`。
  安装包自带 `eng` 与 `chi_sim`，所以干净安装后不需要往任何目录放文件；这条顺序由
  `OcrFixture.TessDataCandidatesPutTheBundledDirectoryBeforeUserData` 钉住（随包那份必须排在用户数据目录之前）。
- 行归并是「按词框的纵坐标自己分行」：C 接口只给到逐词迭代，字号差异大的一行可能被拆成两行，
  要做到逐行准确得换 C++ 接口按 `RIL_TEXTLINE` 走（源码里标了 `ponytail:`）。

## Windows.Media.Ocr：排在最前的系统后端

`WinRTImageOcrEngine` 调系统组件 `Windows.Media.Ocr`，在 `createOcrEngine()` 里排在 Tesseract 之前——
它不要求用户放语言包，语言包由 Windows 自己管（实测这台机器：`AvailableRecognizerLanguages` 给回
`zh-Hans-CN`，`TryCreateFromUserProfileLanguages()` 能建出识别器）。语言匹配按对外标签的分段前缀判
（`zh-Hans` 对 `zh-Hans-CN` 成立），词表仍是 `supportedOcrLanguageTags()` 那一份，界面不接触映射。

- 编译条件：`if (WIN32 AND MSVC)`，宏 `GYANISSCRIBE_HAS_WINRT_OCR` 发 PUBLIC（用例要知道这一档有没有它，
  与 `GYANISSCRIBE_HAS_TESSERACT` 同一条理由）。容器那一档这个文件不进编译列表。
- C++/WinRT 投影头 vendor 在 `Vendor/WinRt/winrt/`（62 个头、3.7 MB，MIT），生成与裁剪办法：
  `G:\Windows Kits\10\bin\10.0.26100.0\x64\cppwinrt.exe -input 10.0.26100.0+ -base -output <目录>`，
  然后从 `base.h`、`Windows.Foundation.h`、`Windows.Foundation.Collections.h`、`Windows.Globalization.h`、
  `Windows.Graphics.Imaging.h`、`Windows.Media.Ocr.h`、`Windows.Storage.Streams.h` 出发按 `#include <winrt/...>`
  取传递闭包——`-include` 参数不减文件（实测给了仍出全部 342 个，84 MB）。闭包脚本落盘后要跑一次真实编译验证。
- 这些生成头必须按外部头处理：`target_include_directories(... SYSTEM PRIVATE)` + `/external:W0`，
  否则本项目的 `/W4 /WX` 会被 vendor 代码的警告淹没（只挂 SYSTEM 不改告警档位仍然会红）。
- 四条实测踩过的形状，改这份后端时都要保住：
  1. `winrt/Windows.Foundation.Collections.h` 要排在其它投影之前，缺了它任何 `.Size()`/`.GetAt()`
     都是 C3779「auto 返回函数必须先定义」。
  2. `Buffer(容量)` 建出来长度是 0，写完字节必须 `buffer.Length(n)`；不写就是往流里写 0 字节，
     解码器拿到空流（现场症状是进程 `0xC0000409` 直接消失，stdout 缓冲区里的字都来不及落）。
  3. `IBufferByteAccess` 在这份 SDK 的 `robuffer.h` 里声明在 `Windows::Storage::Streams` 下而不是
     `ABI::Windows::…`，按老样例写 `ABI::` 会报「未声明的标识符」。
  4. 所有 WinRT 调用跑在一次性线程上并在那里 `init_apartment(multi_threaded)`：Qt 的界面线程是 STA，
     而 `.get()` 会堵在调用线程上等异步完成——STA 上等自己的消息泵就是死锁。
- 图超过系统上限（`MaxImageDimension`，实测 10000）时报错并给出尺寸，不偷偷缩放：缩放会让词框坐标与
  用户看到的那张图不同系，而结果窗口「点文字定位」靠的就是这套坐标。
- 与契约的对齐：系统给逐词方框（`OcrWord::boundingBox` 有值），不给逐词置信度，所以
  `averageConfidence` 恒为 0，界面按「后端给不出」那一格显示。行框由词框并集算出。
- 用例 `tests/Ocr/TestWinRTImageOcrEngine.cpp` 里「读出画上去的数字」那条是真的跑通系统识别才绿的
  （不是跳过）；判据可被证伪：摘掉上面第 2 条那句 `Length()`，红正落在它身上。


## 许可证

| 组件 | 许可 | 备注 |
| --- | --- | --- |
| Tesseract | Apache-2.0 | 与 Qt/LGPL 并存无冲突 |
| Leptonica | BSD-2-clause | Tesseract 的传递依赖 |
| tessdata 训练数据 | Apache-2.0（Google 训练的 eng/chi_sim 系列） | **已随包发布**：`Vendor/Tessdata/` 下两份 fast 变体进安装包的 `tessdata/`，许可证正文在 `licenses/TessData-Apache-2.0.txt` |
| Windows.Media.Ocr | 系统组件，不随本程序分发 | 只是调用系统 API，不引入第三方代码 |
| C++/WinRT 投影头（`Vendor/WinRt`） | MIT（微软 cppwinrt 生成） | 只在 Windows 档参与编译，编译进本程序的是我们的代码，头本身不分发 |

第三方清单的总表在交付文档里统一列（打包与交付文档那一批）。
