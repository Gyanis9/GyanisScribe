# ONNX Runtime 1.32.0（Windows x64 CPU）

## 来源

- 上游发布：https://github.com/microsoft/onnxruntime/releases/download/v1.32.0/onnxruntime-win-x64-1.32.0.zip
- 版本：1.32.0。压缩包内 `VERSION_NUMBER` 为 `1.32.0`；`bin/onnxruntime.dll` 的版本资源为 `1.32.0.20261112.3.fab30a4`，公司为 Microsoft Corporation。
- 许可证：MIT（见本目录 `LICENSE`，版权归 Microsoft Corporation）。

## 取件方式与完整性验证

本机直连 GitHub Release 只有约 16 KB/s（92 MB 需一个半小时以上），实际通过 gh-proxy.com 镜像以 8 段 HTTP Range 并行取回同一 URL 的压缩包（96945131 字节，CRC 全部通过）。上游未发布该 zip 的官方校验和，故通过以下四条证据确认内容为官方构件：

1. `unzip -t` 对全部条目做 CRC 校验，无错误。
2. 压缩包内 `VERSION_NUMBER` 为 `1.32.0`；DLL 版本资源与 Microsoft Corporation 一致。
3. 压缩包内 `LICENSE` 与 PyPI 官方 wheel（`onnxruntime-1.32.0-cp314-cp314-win_amd64.whl`，其 SHA-256 与 pypi.org 公布值 `2b08ee99…c1ec0a` 逐位相符）中的 `onnxruntime/LICENSE` 逐字节相同。
4. 探针用本目录头文件编译后 `LoadLibraryW` 加载 DLL、`GetApi(32)` 成功、`GetVersionString()` 返回 `1.32.0`，并成功创建两个模型的 session。

## SHA-256

| 文件 | 说明 | SHA-256 |
|---|---|---|
| 上游 zip（未入库） | 96945131 字节 | `aaa1d04fcf86bd31e84b90bc0cbde80e632bd0720dcc5d8ba05c4fe45c09d127` |
| `bin/onnxruntime.dll` | 20602168 字节 | `348c3d29d6995eee37e3451825eadb7ea7400be3afda6ca2286b12bc775f042` |
| `include/onnxruntime_c_api.h` | 442638 字节 | `46b146f79b611682cff95e000a3f9b58983e7a915687159e12d0e9a29f0d948c` |
| `include/onnxruntime_error_code.h` | 2873 字节 | `6690bf0df3f7a7a9c4f38597dc3232042e432b48ea166cb5672f72a49d32b742` |
| `include/onnxruntime_ep_c_api.h` | 173074 字节 | `2e6c2de471dedfda120e8579c4a5e23052a4883d45ce8848c40dd0f5b7f896a1` |
| `LICENSE` | 1094 字节 | `c250d6278f0b47a6439fb7592b08b58a55eb9f535aa49a1db63211c3f982b674` |
| `VERSION.txt` | 38 字节 | `6df14c8a9cbacc6193b33eb10abf32eb31037e6865298a8daa085e5cdc6ef377` |

## 裁剪范围

只保留 C API 运行所需的最小集合。1.32.0 把部分声明拆到单独头文件里，`onnxruntime_c_api.h` 无法单独编译，因此按传递包含关系一并入库：

- `include/onnxruntime_c_api.h`：C API 主头，`ORT_API_VERSION` 为 32。
- `include/onnxruntime_error_code.h`：被主头第 37 行包含。
- `include/onnxruntime_ep_c_api.h`：被主头第 9381 行包含。
- `include` 之外另需 Windows SDK 的 `specstrings.h`（vcvars 环境下自带，不入库）。

1.32 的 C API 与旧版有几处签名差异，接入代码需注意：

- 计数与索引参数由 `uint32_t` 改为 `size_t`（`SessionGetInputCount`、`SessionGetInputTypeInfo` 等）。
- 取名称用 `SessionGetInputName` / `SessionGetOutputName`（带 allocator 参数），没有 `…NameAllocated` 变体；字符串用 `allocator->Free` 释放。
- 版本串在 `OrtApiBase::GetVersionString`，不在 `OrtApi` 上。
- 头文件含 UTF-8 注释，在 GBK 代码页下用 cl 编译需加 `/utf-8`，否则 C4819 在 `/WX` 下成为错误。
