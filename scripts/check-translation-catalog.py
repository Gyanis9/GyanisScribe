#!/usr/bin/env python3
"""界面文案与翻译目录的对照门禁，两用模式（校验 / 生成骨架）。

为什么不用 lupdate：界面文案有一部分存在按位置对齐的 constexpr 表里（档位—标签只能有一个真源），
显示时走 Core::uiText(row.label.data()) 运行期查表；lupdate 只认字面量实参，认不出这批。
于是目录由本脚本从源码穷举生成，并把「源码用到的每条都在目录里、且译文非空」钉成 ctest 用例
Style.TranslationCatalog——漏掉的那几处在英文档下只会安静地显示中文。

用法：
    python3 scripts/check-translation-catalog.py                      # 校验（CI/ctest 走这条）
    python3 scripts/check-translation-catalog.py --generate           # 生成/刷新骨架，已有译文原样带过去
    python3 scripts/check-translation-catalog.py --scan-root src --catalog Translations/GyanisScribe_en.ts

长文案会被 clang-format 在空格处断成相邻字面量，编译期才拼回去；脚本按同样的规则先拼再取，
否则目录里会出现一条运行期永远查不中的半截文案（这条门禁自己就漏过一次，见 SELF_PROBE）。
"""

from __future__ import annotations

import argparse
import html
import pathlib
import re
import sys
import xml.etree.ElementTree as ElementTree

#判据全文是中文：控制台码页不对就把「违规」那条打成乱码，读的人无法据此改代码
sys.stdout.reconfigure(encoding="utf-8", errors="replace")
sys.stderr.reconfigure(encoding="utf-8", errors="replace")

REPO_ROOT = pathlib.Path(__file__).resolve().parent.parent
DEFAULT_SCAN_ROOT = REPO_ROOT / "src"
DEFAULT_CATALOG = REPO_ROOT / "Translations" / "GyanisScribe_en.ts"

# uiText("…") / uiTextUtf8("…")：函数名带数字，字符类不能只写 [A-Za-z]*
CALL = re.compile(r'uiText[A-Za-z0-9]*\(\s*"((?:\\.|[^"\\])*)"')
# 相邻字面量拼接：字符串收尾引号 + 空白 + 下一片起始引号
JOINED = re.compile(r'"\s+"')
ENTRY = re.compile(r"<source>([^<]*)</source>\s*<translation([^>]*)>([^<]*)</translation>", re.S)
# 表行的标签不走字面量实参：只认「大写开头的聚合名{…}」这一形，且必须在同一行内，
# 否则两个大括号之间的跨度会吃掉整段代码，把日志文案也算成表行
AGGREGATE_ROW = re.compile(r"[A-Z][A-Za-z0-9_]*\{[^{}\n]*\}")
# 另一形是按位置对齐的字符串数组：`kXxxLabels = {"…", "…"}`。标签与档位各占一份表、
# 显示时才按行下标互指，因此这一形同样进不了 CALL。此前门禁只认上一形，
# 「十进制」这类档位名在英文档下就一直是中文（实测目录里 0 条）
INITIALIZER_LIST = re.compile(r"=\s*\{([^{}\n]*)\}")
LITERAL = re.compile(r'"((?:\\.|[^"\\])*)"')
CJK = re.compile(r"[\u4e00-\u9fff]")

SELF_PROBE = (
    'auto a = Core::uiTextUtf8("甲 \\"{}\\" 乙"\n     "丙");'
    'auto b = Core::uiText("单条");'
    'auto c = Core::uiTextUtf8("整条 {}");'
)

SELF_PROBE_ROWS = (
    'constexpr auto kRows = Row{"丁", 0, "戊"};\n'
    'constexpr std::array<std::string_view, 3> kLabels = {"己", "HEX", "庚"};\n'
)


def unescape_cpp(text: str) -> str:
    """把 C++ 字面量里的转义还原成运行期真正的那串字节。"""
    return text.replace('\\"', '"').replace("\\\\", "\\")


def collect_required(scan_root: pathlib.Path) -> list[str]:
    """穷举源码里用到的界面文案：uiText 的字面量实参 + constexpr 表与标签数组里的中文。"""
    required: list[str] = []
    for source in sorted(list(scan_root.rglob("*.cpp")) + list(scan_root.rglob("*.h"))):
        text = source.read_text(encoding="utf-8")
        flat = JOINED.sub("", text.replace("\n", " "))
        required += [unescape_cpp(match) for match in CALL.findall(flat)]
        # 两种「按下标互指」的表形都不走字面量实参，只能在同一行内认，
        # 否则两个大括号之间的跨度会吃掉整段代码，把日志文案也算成表行
        table_rows = list(AGGREGATE_ROW.finditer(text)) + list(INITIALIZER_LIST.finditer(text))
        for row in sorted(table_rows, key=lambda match: match.start()):
            for cell in LITERAL.finditer(row.group(0)):
                value = unescape_cpp(cell.group(1))
                if CJK.search(value):
                    required.append(value)

    ordered = list(dict.fromkeys(required))
    return ordered


def read_catalog(catalog: pathlib.Path) -> dict[str, str]:
    """读出现有译文，供生成时原样带过去。"""
    if not catalog.exists():
        return {}
    translations: dict[str, str] = {}
    for source, attributes, body in ENTRY.findall(catalog.read_text(encoding="utf-8")):
        if "unfinished" in attributes:
            continue
        translations.setdefault(html.unescape(source), html.unescape(body))
    return translations


def self_probe() -> None:
    """模式必须先在合成样本上抓对三种形状，否则这条门会永远绿着漏掉真问题。"""
    flat = JOINED.sub("", SELF_PROBE.replace("\n", " "))
    hits = [unescape_cpp(match) for match in CALL.findall(flat)]
    if len(hits) != 3:
        raise RuntimeError(f"自检失败：合成文本里三条文案抓到 {len(hits)} 条（应为 3），模式不可信")
    if "甲 \"{}\" 乙丙" not in hits:
        raise RuntimeError("自检失败：两片字面量没被拼成一条，目录会生成永远查不中的半截文案")
    if "单条" not in hits or "整条 {}" not in hits:
        raise RuntimeError("自检失败：带 {} 或紧随其他代码的文案没被抓到")

    # 表形探针：两种按下标互指的写法都要抓到，纯 ASCII 的档位标记不该被当成文案收进目录
    row_hits = []
    for pattern in (AGGREGATE_ROW, INITIALIZER_LIST):
        for row in pattern.finditer(SELF_PROBE_ROWS):
            for cell in LITERAL.finditer(row.group(0)):
                value = unescape_cpp(cell.group(1))
                if CJK.search(value):
                    row_hits.append(value)
    if len(row_hits) != 4 or set(row_hits) != {"丁", "戊", "己", "庚"}:
        raise RuntimeError(f"自检失败：合成表行抓到 {row_hits}（应为 丁、戊、己、庚 四条，多抓说明两种表形重复计，少抓说明有一形没认）")


def generate(scan_root: pathlib.Path, catalog: pathlib.Path, required: list[str]) -> int:
    existing = read_catalog(catalog)
    lines = [
        "<?xml version=\"1.0\" encoding=\"utf-8\"?>",
        "<!DOCTYPE TS>",
        '<TS version="2.1" language="en_US">',
        "<context>",
        "    <name>GyanisScribe</name>",
    ]
    blanks = 0
    for value in required:
        escaped = html.escape(value, quote=False)
        translation = existing.get(value)
        if translation:
            lines += [
                "        <message>",
                f"            <source>{escaped}</source>",
                f"            <translation>{html.escape(translation, quote=False)}</translation>",
                "        </message>",
            ]
        else:
            blanks += 1
            lines += [
                "        <message>",
                f"            <source>{escaped}</source>",
                '            <translation type="unfinished"></translation>',
                "        </message>",
            ]
    lines += ["</context>", "</TS>", ""]
    catalog.parent.mkdir(parents=True, exist_ok=True)
    catalog.write_text("\n".join(lines), encoding="utf-8", newline="\n")
    print(f"[生成] {catalog}：必需文案 {len(required)} 条，其中空白译文 {blanks} 条")
    return 0


def verify(scan_root: pathlib.Path, catalog: pathlib.Path, required: list[str]) -> int:
    if not catalog.exists():
        print(f"[失败] 翻译目录 {catalog} 不存在：先跑 {pathlib.Path(__file__).name} --generate", file=sys.stderr)
        return 1
    # 先把整份文件当 XML 解析一遍：手填译文最容易漏掉 & 的转义，而 lrelease 要到构建期才报，
    # 报的还是一句「Expected '#' or '[a-zA-Z]'」——这里当场点名行列，红在改完的那一刻
    try:
        ElementTree.fromstring(catalog.read_text(encoding="utf-8"))
    except ElementTree.ParseError as failure:
        print(f"[失败] 翻译目录不是合法 XML：{failure}。常见原因是译文里写了没转义的 &（要写 &amp;）", file=sys.stderr)
        return 1
    existing = read_catalog(catalog)
    missing = [value for value in required if value not in existing]
    if missing:
        print(f"[失败] 界面文案共 {len(required)} 条，缺译文 {len(missing)} 条。英文档下这些位置会露出中文：", file=sys.stderr)
        for value in missing:
            print(f"  - {value}", file=sys.stderr)
        print("  补译文后重新构建（lrelease 会自动生成 .qm）", file=sys.stderr)
        return 1
    print(f"[通过] {scan_root} 下 {len(list(scan_root.rglob('*.cpp')))} 个 .cpp 的 {len(required)} 条文案全部有译文")
    return 0


def main() -> int:
    parser = argparse.ArgumentParser(description="界面文案与翻译目录的对照门禁")
    parser.add_argument("--scan-root", type=pathlib.Path, default=DEFAULT_SCAN_ROOT)
    parser.add_argument("--catalog", type=pathlib.Path, default=DEFAULT_CATALOG)
    parser.add_argument("--generate", action="store_true", help="生成/刷新目录骨架，已有译文原样带过去")
    arguments = parser.parse_args()

    if not arguments.scan_root.is_dir():
        print(f"[失败] 扫描根 {arguments.scan_root} 不是目录", file=sys.stderr)
        return 2
    try:
        self_probe()
    except RuntimeError as failure:
        print(f"[失败] {failure}", file=sys.stderr)
        return 2

    required = collect_required(arguments.scan_root)
    if not required:
        # 扫到空名单也算失败：一条永远看不到内容的检查比没有检查更糟（它会一直绿）
        print(f"[失败] 在 {arguments.scan_root} 下没穷举到任何界面文案，检查本身没在扫任何东西", file=sys.stderr)
        return 1
    if len(required) < 10:
        print(f"[失败] 只穷举到 {len(required)} 条文案，怀疑模式失效", file=sys.stderr)
        return 1

    if arguments.generate:
        return generate(arguments.scan_root, arguments.catalog, required)
    return verify(arguments.scan_root, arguments.catalog, required)


if __name__ == "__main__":
    sys.exit(main())
