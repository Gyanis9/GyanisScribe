# OCR 后端选型与状态

## 现在有什么

- `IOcrEngine`（同步接口）+ `PpOcrOnnxEngine`（PaddleOCR 的 PP-OCRv6 small 模型，推理走 ONNX Runtime）+ `WinRTImageOcrEngine`（Windows 系统识别兜底）
  + `UnavailableOcrEngine`（降级桩）+ 后端选型 `createOcrEngine()`；用例按三层摆在 `tests/Core/Ocr/`、
  `tests/Gui/Ocr/`、`tests/Plugins/OcrEngines/`（条数不写在这儿，加一条就漂移一次）。
- 结果导出：TXT 与 JSON（JSON 带 language、averageConfidence、imageSize、逐词方框；
  后端不报置信度时那一项是 null，界面上那一格写「未提供」，不打 0%）。
- 语言标签词表对外只有 `zh-Hans` / `en` 两个。两个模型共用同一份中英混合字表，标签只按请求值记进结果，
  两种标签走同一条识别路径。

## 模型与运行库放在哪

- 模型三件，文件名固定（引擎常量）：`PP-OCRv6_det_small.onnx`（检测）、`PP-OCRv6_rec_small.onnx`（识别）、
  `ppocrv6_dict.txt`（字表，18708 条）。上游是 ModelScope 的 `RapidAI/RapidOCR` 标签 `v3.10.0`：
  模型出自百度 PaddleOCR 的 PP-OCRv6，由 RapidOCR（RapidAI）转换为 ONNX。三件的上游地址、SHA-256
  与实测模型签名记在 `vendor/PpocrModels/PROVENANCE.md`。
- 模型目录查找顺序由 `OcrEngineFactory` 定：设置项 `Ocr/ModelDirectory`（非空才计）→ **程序目录下的 `ppocr`（随包那份）**
  → 应用数据目录下的 `ppocr`。三件文件都齐全且非空才算一处可用目录；缺件、空文件、目录不存在都跳过，
  一处都不完整时把每个候选的「路径（缺少 …）」列全再交给用户。这条顺序由
  `OcrFixture.PpOcrModelCandidatesPutBundledDirectoryBeforeUserData` 钉住（随包那份必须排在用户数据目录之前）。
- 运行库 `onnxruntime.dll`（1.32.0，Windows x64 CPU）的加载顺序：环境变量 `GYANISSCRIBE_ONNXRUNTIME_DLL`
  （设了只走它，文件不在场直接失败、不回落）→ 程序目录 → 系统搜索路径。构建期不链接导入库，
  接口表用 `LoadLibrary` + `OrtGetApiBase` 现取；文件损坏、缺 `OrtGetApiBase`、接口版本与 `ORT_API_VERSION`
  不符，都按「运行库不可用」报中文原因。
- 设置界面：设置 → 「文字识别」页的「识别语言」与「模型目录」两行；说明行写明「可识别的文字取决于识别模型。
  目录留空时按程序目录与应用数据目录查找。」模型目录的默认展示值是应用数据目录下的 `ppocr`。
- 模型与运行库都 vendored 进仓库（`vendor/PpocrModels/`、`vendor/OnnxRuntime/`），不从构建期联网下载；
  打包脚本把它们复制进安装包的 `ppocr/` 与程序目录。开发树的 `DeployQtRuntime` 目标也把三件模型
  摆到可执行体旁（`build/debug/bin/ppocr`），与安装包同一套布局，开发树不会静默落到系统识别。

## 后端选型顺序

`createOcrEngine()` 的顺序是：先 `PpOcrOnnxEngine`（模型目录齐、运行库也起得来才交回去）；不可用才问
`WinRTImageOcrEngine`（宏 `GYANISSCRIBE_HAS_WINRT_OCR`，Windows 档编进）；两条都没有才落到
`UnavailableOcrEngine`。

- 「为什么不可用」分两种：一处完整模型目录都没有时，工厂逐候选列出缺了哪些文件；目录齐了、运行库没起来时
  用引擎自己的原因。系统识别也不可用时，两段原因用换行拼进降级桩。
- 两档构建（Debug / Release）编进同一组后端，依赖图不按 `build_type` 分岔；条件编译只剩「这一档有没有系统识别」，
  本工程的 Windows 两档都有。

## 识别管线

1. 检测：`buildDetInput` 把原图长边等比缩到 1280（模型输入全动态，1280 是通用上限），补黑边到 32 的倍数
   （主干 32 倍下采样），按 B、G、R 通道平面归一化（ImageNet 均值方差；训练时喂的是 BGR，所以 0.485 对应 B 通道）。
   检测模型输出概率图，`extractTextBoxes` 按阈值 0.3、最小面积与外扩系数 0.2 取连通域，把框折回原图坐标
   并按阅读顺序排列。
2. 识别：逐框裁图，`buildRecInput` 把高固定到 48、宽按长宽比换算（下限 16、上限 2400），像素线性映射到 [-1, 1]。
   识别模型输出「时间步 × 18710 类」（字表 18708 条 + blank + 空格），`decodeCtc` 走贪心 CTC：
   逐步取最大类、合并重复、去 blank。
3. 拼结果：一个检测框出一行，`line.words` 只有一个词（文本 + 裁框）；`averageConfidence` 是按字符数加权的
   平均识别置信度，一行都没认出时留空。
4. 线程：`RecognitionRunner` 把同步接口搬到 Qt 全局线程池；同一次只跑一条——会话在首次识别时创建并复用，
   同一个后端不做并发保护。
5. 预处理：只有系统识别那一档走「灰度 → 最近邻放大（上限 2×）」；PaddleOCR 直接吃原图，缩放与归一化都在
   `PpOcrInput` 里按模型要求做，不做二值化与去噪。

## 已知限制

- 没有方向分类模型（cls）：倒置、90° 旋转的文字不自动转正，会认不出或认错。
- 识别输入宽上限 2400（高 48，约 50:1 的长宽比）：更长的行会被横向压到上限再送识别，字迹变窄，准确率下降。
- 一个检测框只出一行：框里的多行文本被压成一行，换行丢失；同一行被连通域切碎时会拆成多行。
- 不做自动反色：深底浅字的图按原样识别，不先反色；这类图要由上游自己处理。
- 最小边长 8：小于 8×8 的图直接报「图片太小或为空」。
- 模型或运行库缺一不可：缺件报中文「原因 + 替代做法」，不返回空结果冒充「没认出字」。

## 性能读数

读数用例 `PpOcrOnnxEngineFixture.ReportsMedianCostOfA4kImage`：3840×2160 白底图上一行 96 磅数字，识别 3 轮取中位，
只报数、不断言时间。本机 Debug 档 2026-11-14 连跑三次，中位 **415.0 / 536.6 / 480.2ms**
（各次 min 219.6 / 375.9 / 406.1，max 800.2 / 644.0 / 621.5；每次的第一轮含模型与运行库冷启动，所以同一台机器
三次之间也会差出百来毫秒）。机器：Intel Core i5-14600KF（14 核 20 线程）、32 GB 内存、
Windows 11 专业工作站 10.0.26300、Qt 6.12.0 msvc2022_64、单屏 2560×1440。Debug 档的数只与本档比较，
换机器或换档位读数就跟着换。

## Windows.Media.Ocr：系统识别兜底

`WinRTImageOcrEngine` 调系统组件 `Windows.Media.Ocr`，在 `createOcrEngine()` 里排在 PaddleOCR 之后当兜底——
它不要求用户放模型，语言包由 Windows 自己管（实测这台机器：`AvailableRecognizerLanguages` 给回
`zh-Hans-CN`，`TryCreateFromUserProfileLanguages()` 能建出识别器）。语言匹配按对外标签的分段前缀判
（`zh-Hans` 对 `zh-Hans-CN` 成立），词表仍是 `supportedOcrLanguageTags()` 那一份，界面不接触映射。

- 编译条件：`if (WIN32 AND MSVC)`，宏 `GYANISSCRIBE_HAS_WINRT_OCR` 发 PUBLIC（用例要知道这一档有没有它）。
- C++/WinRT 投影头 vendor 在 `vendor/WinRt/winrt/`（62 个头、3.7 MB，MIT），生成与裁剪办法：
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
- 用例 `tests/Plugins/OcrEngines/TestWinRTImageOcrEngine.cpp` 里「读出画上去的数字」那条是真的跑通系统识别才绿的
  （不是跳过）；判据可被证伪：摘掉上面第 2 条那句 `Length()`，红正落在它身上。


## 许可证

| 组件 | 许可 | 备注 |
| --- | --- | --- |
| PaddleOCR（PP-OCRv6 small 模型） | Apache-2.0 | 模型由百度 PaddleOCR 训练、RapidOCR 转换为 ONNX；**已随包发布**：`vendor/PpocrModels/` 下三件进安装包的 `ppocr/`，正文在 `licenses/PaddleOCR-Apache-2.0.txt` |
| RapidOCR | Apache-2.0 | 模型转换与分发的上游（ModelScope `RapidAI/RapidOCR` 标签 `v3.10.0`）；正文在 `licenses/RapidOCR-Apache-2.0.txt` |
| ONNX Runtime | MIT（Microsoft） | 推理运行库 1.32.0；`onnxruntime.dll` 随包摆在程序目录，正文在 `licenses/OnnxRuntime-MIT.txt` |
| Windows.Media.Ocr | 系统组件，不随本程序分发 | 只是调用系统 API，不引入第三方代码 |
| C++/WinRT 投影头（`vendor/WinRt`） | MIT（微软 cppwinrt 生成） | 只在 Windows 档参与编译，编译进本程序的是我们的代码，头本身不分发 |

第三方清单的总表在交付文档里统一列（打包与交付文档那一批）。
