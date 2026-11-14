# PP-OCRv6 small 检测/识别模型与字典

## 来源与归属

- 来源仓库：ModelScope `RapidAI/RapidOCR`，标签 `v3.10.0`。
- 模型出自百度 PaddleOCR 的 PP-OCRv6，由 RapidOCR（RapidAI）转换为 ONNX；许可证为 Apache-2.0。
- 三个文件的上游地址：
  - det：https://www.modelscope.cn/models/RapidAI/RapidOCR/resolve/v3.10.0/onnx/PP-OCRv6/det/PP-OCRv6_det_small.onnx
  - rec：https://www.modelscope.cn/models/RapidAI/RapidOCR/resolve/v3.10.0/onnx/PP-OCRv6/rec/PP-OCRv6_rec_small.onnx
  - dict：https://www.modelscope.cn/models/RapidAI/RapidOCR/resolve/v3.10.0/paddle/PP-OCRv6/rec/PP-OCRv6_rec_small/ppocrv6_dict.txt

## SHA-256

| 文件 | 大小 | SHA-256 | 校验 |
|---|---|---|---|
| `PP-OCRv6_det_small.onnx` | 9929594 字节 | `090f04abcd9d9a7498bc4ebf677e4cb9bdce1fe4197ddb7e529f1ef44e1ff94f` | 与注册表预期值一致 |
| `PP-OCRv6_rec_small.onnx` | 21234383 字节 | `6f327246b50388f3c176ae304bd95767ea6dc0c9ae92153ef8cbe210b3c14884` | 与注册表预期值一致 |
| `ppocrv6_dict.txt` | 74947 字节 | `b5f2bfe2bdd9448429e3e82b51c789775d9b42f2403d082b00662eb77e401c5d` | 无上游预期值，按实际记录 |

字典为 UTF-8 文本，18708 条、以换行结尾；首条为 `!`，末条为 🛅（U+1F6C5）。

## 模型签名（ONNX Runtime 1.32.0 实测）

用 `build/gate/ppocr_probe.cpp`（不入库）加载 `vendor/OnnxRuntime/bin/onnxruntime.dll`，对两个模型各建一次 session，输出如下：

```text
ORT version: 1.32.0 (header ORT_API_VERSION=32)

== PP-OCRv6_det_small.onnx ==
inputs: 1
  input[0] name="x" elem=float(1) rank=4 shape=[-1, 3, -1, -1]
outputs: 1
  output[0] name="fetch_name_0" elem=float(1) rank=4 shape=[-1, 1, -1, -1]

== PP-OCRv6_rec_small.onnx ==
inputs: 1
  input[0] name="x" elem=float(1) rank=4 shape=[-1, 3, 48, -1]
outputs: 1
  output[0] name="fetch_name_0" elem=float(1) rank=3 shape=[-1, -1, 18710]
```

要点：

- 两模型各 1 个输入、1 个输出，输入名均为 `x`，元素类型 float32。
- det 输入 `[-1, 3, -1, -1]`：NCHW，高宽全部动态，没有固定尺寸约束。
- rec 输入 `[-1, 3, 48, -1]`：高固定为 48，宽动态。接入方必须按 48 高送输入，宽度可随文本行长度变化。
- rec 输出类别数 18710，比字典条目多 2，对应 PP-OCR 后处理的 blank 与空格位。
- 两模型的输出名都叫 `fetch_name_0`，分属不同文件，不冲突。
