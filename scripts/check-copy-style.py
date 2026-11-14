"""界面文案体例门禁：正式、书面、术语统一。

用法: python scripts/check-copy-style.py --catalog translations/GyanisScribe_en.ts
逐条检查翻译目录里的 <source> 原文；命中即失败，并打印规则、命中文本与原文。
"""

import argparse
import re
import sys
import xml.etree.ElementTree as ET
from pathlib import Path

# 口语与随意说法：界面文案一律用书面语，出现任何一条都要求改写
COLLOQUIAL = ["一下", "吧", "啦", "搞定", "咱们", "嗯", "哦", "呀", "哈", "嘛", "呗", "哎", "唉"]
DOUBLE_PUNCTUATION = ["。。", "，，", "；；", "、、"]
EMOJI_PATTERN = re.compile("[\U0001F000-\U0001FAFF\u2600-\u27BF]")


def check(text: str):
    violations = []
    stripped = text.strip()
    # 这几类留白是拼接协议而不是排版：单位后缀、命令行帮助、以空格结尾的句子前缀、
    # 以「 · 」开头的列表续行
    unit_fragment = text.startswith(" ") and len(stripped) <= 6 and not any(mark in stripped for mark in "。！？，；")
    help_text = chr(10) in text or stripped.startswith("--")
    sentence_prefix = text.endswith(" ") and stripped[-1] not in "。！？"
    list_continuation = stripped.startswith("·")
    if text != stripped and not (unit_fragment or help_text or sentence_prefix or list_continuation):
        violations.append("首尾留白")
    # Markdown 图片语法 ![](…) 里的感叹号是语法，不是语气
    if "！" in text or ("!" in text and "![" not in text):
        violations.append("感叹号")
    for token in COLLOQUIAL:
        if token in text:
            violations.append(f"口语词「{token}」")
    for token in DOUBLE_PUNCTUATION:
        if token in text:
            violations.append(f"重复标点「{token}」")
    if EMOJI_PATTERN.search(text):
        violations.append("表情符号")
    return violations


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--catalog", required=True)
    args = parser.parse_args()

    tree = ET.parse(Path(args.catalog))
    failures = 0
    for message in tree.iter("message"):
        source = message.findtext("source") or ""
        violations = check(source)
        if not violations:
            continue
        failures += 1
        print(f"[失败] {'；'.join(violations)}")
        print(f"       原文：{source}")
    print(f"界面文案体例检查：{failures} 条不合格")
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
