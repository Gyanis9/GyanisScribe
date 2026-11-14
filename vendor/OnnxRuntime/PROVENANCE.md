# ONNX Runtime 1.32.0（Windows x64 CPU）

## 来源

- 运行库与许可证：PyPI 官方 wheel `onnxruntime-1.32.0-cp314-cp314-win_amd64.whl`，经清华 TUNA 镜像下载（https://pypi.tuna.tsinghua.edu.cn/simple/onnxruntime/）。`bin/onnxruntime.dll` 取自 wheel 内 `onnxruntime/capi/onnxruntime.dll`，`LICENSE` 取自 wheel 内 `onnxruntime/LICENSE`。
- 头文件：https://raw.githubusercontent.com/microsoft/onnxruntime/v1.32.0/include/onnxruntime/core/session/onnxruntime_c_api.h ，同一标签下另取被它包含的 `onnxruntime_error_code.h` 与 `onnxruntime_ep_c_api.h`。
- 版本：1.32.0。wheel 的 SHA-256 为 `2b08ee990d00ab2ee129c5e039f7339963335a4d76f5bd22bfb4f896ae1cec0a`，与 PyPI simple index 公布值一致；DLL 版本资源为 `1.32.0.20261112.6.fab30a4`，公司为 Microsoft Corporation。
- 许可证：MIT（见本目录 `LICENSE`，版权归 Microsoft Corporation）。

## SHA-256

| 文件 | 说明 | SHA-256 |
|---|---|---|
| wheel（未入库） | 17123630 字节 | `2b08ee990d00ab2ee129c5e039f7339963335a4d76f5bd22bfb4f896ae1cec0a` |
| `bin/onnxruntime.dll` | 22668600 字节 | `f529520505e46ba357e8042bb690f30135b2df01f5ff674b8d069f8201e091e9` |
| `include/onnxruntime_c_api.h` | 433257 字节 | `4a8df643e1667147db6a9de16570aac452cc17fb2423984530c964363d6ff5f8` |
| `include/onnxruntime_error_code.h` | 2775 字节 | `5ce3b054e798eced8d14f5b86e98692fd33470463f96194ce0700a2d53dd8721` |
| `include/onnxruntime_ep_c_api.h` | 169708 字节 | `7ca769301114baeb8cb7e0aed901b44a0b3726079fd908daada68fb47bd33bb2` |
| `LICENSE` | 1094 字节 | `c250d6278f0b47a6439fb7592b08b58a55eb9f535aa49a1db63211c3f982b674` |
| `VERSION.txt` | 38 字节 | `6df14c8a9cbacc6193b33eb10abf32eb31037e6865298a8daa085e5cdc6ef377` |

## 采纳范围

只保留 C API 运行所需的最小集合。1.32.0 把部分声明拆到单独头文件里，`onnxruntime_c_api.h` 无法单独编译，因此按传递包含关系一并入库：

- `include/onnxruntime_c_api.h`：C API 主头，`ORT_API_VERSION` 为 32。
- `include/onnxruntime_error_code.h`：被主头第 37 行包含。
- `include/onnxruntime_ep_c_api.h`：被主头第 9381 行包含。
- `include` 之外另需 Windows SDK 的 `specstrings.h`（vcvars 环境下自带，不入库）。

`dumpbin /dependents` 对 DLL 的导入表只有系统 DLL（KERNEL32、ADVAPI32、dbghelp、SETUPAPI、dxgi、api-ms-win-crt-*）与 VC 运行时（MSVCP140、MSVCP140_1、VCRUNTIME140、VCRUNTIME140_1），没有 `onnxruntime_providers_shared.dll`，故只需入库这一个 DLL。

## 交叉验证

同一 1.32.0 的官方 Windows x64 CPU 发布包（`onnxruntime-win-x64-1.32.0.zip`，96945131 字节）也经镜像完整取回并校验：整包 CRC 全过，包内 `VERSION_NUMBER` 为 `1.32.0`，其 DLL 版本资源为 `1.32.0.20261112.3.fab30a4`（与本 DLL 同一提交 fab30a4，构建号不同），包内 `LICENSE` 与本目录 `LICENSE` 逐字节相同，包内三个头文件与 raw 标签版本内容相同（仅换行符不同）。两条独立官方渠道互证。

探针用本目录头文件编译后 `LoadLibraryW` 加载本目录 DLL、`GetApi(32)` 成功、`GetVersionString()` 返回 `1.32.0`，并为两模型建 session 打印签名（见 `vendor/PpocrModels/PROVENANCE.md`）。

1.32 的 C API 与旧版有几处签名差异，接入代码需注意：

- 计数与索引参数由 `uint32_t` 改为 `size_t`（`SessionGetInputCount`、`SessionGetInputTypeInfo` 等）。
- 取名称用 `SessionGetInputName` / `SessionGetOutputName`（带 allocator 参数），没有 `…NameAllocated` 变体；字符串用 `allocator->Free` 释放。
- 版本串在 `OrtApiBase::GetVersionString`，不在 `OrtApi` 上。
- 头文件含 UTF-8 注释，在 GBK 代码页下用 cl 编译需加 `/utf-8`，否则 C4819 在 `/WX` 下成为错误。
