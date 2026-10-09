# OCR 后端选型与状态

## 现在有什么

- `IOcrEngine`（同步接口）+ `TesseractOcrEngine`（真后端）+ `UnavailableOcrEngine`（降级桩）+ 后端选型 `createOcrEngine()`，Ocr 目录共 14 条用例。
- 预处理链 `toGrayscale → denoise → enlarge → binarize` 已落地并逐环有判据。
- 结果导出：TXT 与 JSON（JSON 带 language、averageConfidence、imageSize、逐词方框）。
- 语言标签词表对外只有 `zh-Hans` / `en` 两个，磁盘上的模型名（`chi_sim` / `eng`）在引擎内部映射。

## 后端按构建档分形状：Release 带 Tesseract，Debug 带降级桩

`createOcrEngine()` 有两种形状，由宏 `GYANISSCRIBE_HAS_TESSERACT` 决定：Release 档拿到 `TesseractOcrEngine`，
Debug 档拿到 `UnavailableOcrEngine`。两个预设都在门禁里各编各的，所以没有「声明了却没编过」的分支。

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
- 迭代用 Tesseract 的 C 接口：函数是 `extern "C"` 的全局名，而 `RIL_*` 这组枚举在 C++ 模式下住在
  `namespace tesseract` 里（capi.h 在 C++ 分支走 `typedef tesseract::PageIteratorLevel`），
  两类名字的前缀不能一起加或一起去。
- 语言包（`*.traineddata`）**不依赖构建期联网下载**（编码规范第 25 条：要么随包发布，要么由用户放置）；
  查找顺序为设置项 `Ocr/TessDataDirectory` → 环境变量 `TESSDATA_PREFIX` → 应用数据目录下的 `tessdata`。
- 行归并是「按词框的纵坐标自己分行」：C 接口只给到逐词迭代，字号差异大的一行可能被拆成两行，
  要做到逐行准确得换 C++ 接口按 `RIL_TEXTLINE` 走（源码里标了 `ponytail:`）。

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
