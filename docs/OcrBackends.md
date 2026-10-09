# OCR 后端选型与状态

## 现在有什么

- `IOcrEngine`（同步接口）+ `UnavailableOcrEngine`（降级桩）+ 后端选型 `createOcrEngine()` 已落地并有 13 条用例。
- 预处理链 `toGrayscale → denoise → enlarge → binarize` 已落地并逐环有判据。
- 结果导出：TXT 与 JSON（JSON 带 language、averageConfidence、imageSize、逐词方框）。
- 语言标签词表对外只有 `zh-Hans` / `en` 两个，磁盘上的模型名（`chi_sim` / `eng`）在引擎内部映射。

## 当前构建里没有后端

`createOcrEngine()` 现在恒回桩实现，原因文案会区分两种情况：语言包没找到（点出三处查找位置），
或语言包在但本构建没编入后端（指向本文）。不装成「可用但返回空结果」——空结果与「这张图里没字」长得一样。

## Tesseract（跨平台那一路）

- conancenter 有配方（实测列到 5.5.3）。本机 `--build=missing` 的探测在从源码构建 leptonica + tesseract
  （Windows/MSVC 195 没有可用的预编译包）。
- 结论出来之前不把 `TesseractOcrEngine.{h,cpp}` 放进仓库：**没有依赖就编不到的文件，不该以「看起来完成」的形式留在树里**。
- 接入清单（三处必须同批，否则 CMake 分支会指向不存在的文件）：
  1. `conandata.yml` 的 `requirements` 加 `tesseract/<版本>`；
  2. `src/Ocr/CMakeLists.txt` 恢复 `find_package(Tesseract QUIET CONFIG)` 的可选分支与 `GYANISSCRIBE_HAS_TESSERACT`；
  3. 放回 `TesseractOcrEngine.h/.cpp`（C API：`TessBaseAPIInit3/SetImage/Recognize/GetIterator`，
     行由词框纵坐标归并，置信度取均值）。
- 语言包（`*.traineddata`）**不依赖构建期联网下载**（编码规范第 25 条：要么随包发布，要么由用户放置）；
  查找顺序为设置项 `Ocr/TessDataDirectory` → 环境变量 `TESSDATA_PREFIX` → 应用数据目录下的 `tessdata`。

## Windows.Media.Ocr（需求里的优先项）—— 为什么本轮没走

系统识别要用 C++/WinRT：SDK 里确实有 `cppwinrt.exe`（实测 `G:\Windows Kits\10\bin\10.0.26100.0\x64\`），
但要在构建里加「从 `UnionMetadata` 生成 winrt 头 → 参与编译 → 协程返回值」这一步，双档（MSVC + 容器内 GCC）
的构建集成成本高于加一个 Conan 依赖，而收益只是省一个可选依赖。接口层已经给它留好位置：
新增一份 `WinRtOcrEngine` 并在 `createOcrEngine()` 里排在 Tesseract 之前即可，其余代码不动。
另有两条实际限制：系统识别需要已安装对应语言的 OCR 组件（不是所有 Windows 都有中文包），
且它只给行框不给词框与置信度，与本仓的 `OcrWord`/`averageConfidence` 契约不完全对齐。

## 许可证

| 组件 | 许可 | 备注 |
| --- | --- | --- |
| Tesseract | Apache-2.0 | 与 Qt/LGPL 并存无冲突 |
| Leptonica | BSD-2-clause | Tesseract 的传递依赖 |
| tessdata 训练数据 | Apache-2.0（Google 训练的 eng/chi_sim 系列） | 随包发布时要一并带 LICENSE 与来源说明 |
| Windows.Media.Ocr | 系统组件，不随本程序分发 | 只是调用系统 API，不引入第三方代码 |

第三方清单的总表在交付文档里统一列（打包与交付文档那一批）。
