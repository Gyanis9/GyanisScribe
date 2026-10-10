#!/usr/bin/env python3
"""源码约定门禁：两条「编译期看不出来、运行时只安静地错」的检查。

一、路径不许走窄串。Windows 上 std::filesystem::path::string() 按本地 ANSI 码页给字节，
   码页装不下的字会当场抛 std::system_error——而抛出点多半在承诺返回 std::expected 的通道里。
   反过来把界面里的路径文本换成路径也要走 wstring。唯一的真源是
   Core::Paths::toDisplayText / fromDisplayText。确属例外（命令行参数要按本地编码交给子进程）
   的调用点必须在同一句里写明理由：不写就红，写了就放过。

二、按枚举分派的开关要覆盖每个值。MSVC 默认不对「switch 漏了某个枚举值」报警（C4061/C4062
   没开），容器里 GCC 的预设也没带 -Wall，于是漏一支就等于菜单上多一条按了没反应的项。

跑法：python scripts/check-source-conventions.py [--scan-root src]
退出码：0 通过；1 有违规；2 工具自己坏了（自检探针跑不起来或真源形状变了）——不许当成通过。
"""

from __future__ import annotations

import argparse
import pathlib
import re
import sys

# 例外要在同一句里留下的理由关键词
NARROW_REASON = "本地码页"

NEWLINE = chr(10)
CARRIAGE_RETURN = chr(13)

# path → 窄串：QString::fromLocal8Bit(x.string().c_str()) 一类的形状
PATH_TO_NARROW = re.compile(r"QString::fromLocal8Bit\([^;]*?(?:\.|->)string\(\)")
# 窄串 → path：std::filesystem::path{某串.toStdString()} 一类的形状
NARROW_TO_PATH = re.compile(r"std::filesystem::path\s*[({][^;]*?\.toStdString\(\)")

# 按枚举分派的开关：(枚举名, 声明所在文件, 分派所在文件)
DISPATCH_TABLES = (
    ("TrayAction", "src/App/TrayController.h", "src/App/AppController.cpp"),
    ("MainWindowPage", "src/App/MainWindow.h", "src/App/MainWindow.cpp"),
)

ENUM_BODY = re.compile(r"enum class (\w+)\s*\{([^}]*)\}", re.S)


def is_path_narrow(text: str) -> bool:
    """这一段是不是「路径与窄串互转」；同一段里写了理由的算例外。"""
    if not (PATH_TO_NARROW.search(text) or NARROW_TO_PATH.search(text)):
        return False
    return NARROW_REASON not in text


def enumerate_names(text: str, enum_name: str) -> list[str]:
    """从声明文本里取某个 enum class 的成员名：注释掉的一行不算，带行尾注释的一行该算。"""
    match = ENUM_BODY.search(text.replace(CARRIAGE_RETURN + NEWLINE, NEWLINE))
    if not match or match.group(1) != enum_name:
        raise RuntimeError(f"找不到 enum class {enum_name} 的声明体：真源形状变了，这条门禁要跟着改，不能静默放行")
    names: list[str] = []
    for line in match.group(2).splitlines():
        stripped = line.split("///")[0].split("//")[0].strip()
        identifier = re.fullmatch(r"([A-Z][A-Za-z0-9_]*)\s*,?", stripped)
        if identifier:
            names.append(identifier.group(1))
    if not names:
        raise RuntimeError(f"enum class {enum_name} 穷举出零个成员：判据退化成了永真")
    return names


def statements(text: str) -> list[tuple[int, str]]:
    """把源码切成「一条语句」的片段，返回（起始行号，整段文本）。

    按行扫会被 clang-format 骗过去：长语句被断到下一行（翻译目录那条门就是这么漏过八句文案），
    而 std::filesystem::path{QString…toStdString()} 正好是容易被断开的长形状。
    """
    chunks: list[tuple[int, str]] = []
    start_line = 1
    buffer: list[str] = []
    for number, line in enumerate(text.replace(CARRIAGE_RETURN + NEWLINE, NEWLINE).splitlines(), start=1):
        if not buffer:
            start_line = number
        buffer.append(line)
        if ";" in line:
            chunks.append((start_line, NEWLINE.join(buffer)))
            buffer = []
    if buffer:
        chunks.append((start_line, NEWLINE.join(buffer)))
    return chunks


def check_path_encoding(root: pathlib.Path) -> list[str]:
    """扫源码根目录找路径与窄串互转的两种形状（按语句而不是按行）。"""
    findings: list[str] = []
    for source in sorted(list(root.rglob("*.cpp")) + list(root.rglob("*.h"))):
        for number, statement in statements(source.read_text(encoding="utf-8", errors="replace")):
            if is_path_narrow(statement):
                one_line = " ".join(part.strip() for part in statement.splitlines()).strip()
                findings.append(f"{source.relative_to(root.parent)}:{number} 路径与窄串互转，要改走 Core::Paths::toDisplayText / fromDisplayText：{one_line}")
    return findings


def check_dispatch_coverage(root: pathlib.Path) -> list[str]:
    """每个按枚举分派的开关，都要覆盖该枚举的全部取值。"""
    findings: list[str] = []
    for enum_name, header, dispatcher in DISPATCH_TABLES:
        declared = enumerate_names((root.parent / header).read_text(encoding="utf-8", errors="replace"), enum_name)
        body = (root.parent / dispatcher).read_text(encoding="utf-8", errors="replace")
        handled = set(re.findall(r"case\s+" + enum_name + r"::(\w+)\s*:", body))
        for name in declared:
            if name not in handled:
                findings.append(f"{dispatcher} 里没有 {enum_name}::{name} 这一支：按枚举分派漏一格，界面上就是一条按了没反应的项")
    return findings


def self_probe() -> None:
    """探针：判据必须真的能红。任何一步不成立就抛，由 main 退 2。"""
    must_flag = [
        '        QSaveFile output(QString::fromLocal8Bit(filePath.string().c_str()));',
        '        return std::filesystem::path(location.toStdString());',
        '        emit finished(QString::fromLocal8Bit(encoded->string().c_str()));',
        '        const auto path = std::filesystem::path(' + NEWLINE + '            configured.toStdString());',
    ]
    for statement in must_flag:
        if not is_path_narrow(statement):
            raise RuntimeError(f"探针没认出应当报错的形状：{statement!r}")

    exempt = f"        return QString::fromLocal8Bit(path.string().c_str()); // 例外：命令行参数要按{NARROW_REASON}交给子进程"
    if is_path_narrow(exempt):
        raise RuntimeError("写了理由的例外仍被拦下：例外口等于没有")

    header = "enum class WidgetAction" + NEWLINE + "{" + NEWLINE + "    Open," + NEWLINE + "    Close, ///< 关掉" + NEWLINE + "    // Deleted," + NEWLINE + "};" + NEWLINE
    if enumerate_names(header, "WidgetAction") != ["Open", "Close"]:
        raise RuntimeError("枚举名穷举错了：注释掉的一行不该算，带 ///< 的一行该算")

    # 分派检查也要能红：拿一份缺了一支的合成开关喂进去
    missing_case_body = "switch (action)" + NEWLINE + "{" + NEWLINE + "    case WidgetAction::Open:" + NEWLINE + "        return;" + NEWLINE + "}" + NEWLINE
    if "Close" in set(re.findall(r"case\s+WidgetAction::(\w+)\s*:", missing_case_body)):
        raise RuntimeError("分派检查的探针失效：漏掉的那一支应当被认出来")


def main() -> int:
    parser = argparse.ArgumentParser(description="源码约定门禁：路径窄串与枚举分派")
    parser.add_argument("--scan-root", default=str(pathlib.Path(__file__).resolve().parent.parent / "src"), help="要扫的源码根目录")
    args = parser.parse_args()

    root = pathlib.Path(args.scan_root).resolve()
    try:
        self_probe()
        findings = check_path_encoding(root) + check_dispatch_coverage(root)
    except RuntimeError as failure:
        print(f"[门禁坏了] {failure}", file=sys.stderr)
        return 2

    for finding in findings:
        print(f"[违规] {finding}", file=sys.stderr)
    if findings:
        print(f"[失败] 源码约定检查报了 {len(findings)} 处", file=sys.stderr)
        return 1
    print(f"[ok] 源码约定检查通过：路径窄串与枚举分派都没有漏项（扫 {root}）")
    return 0


if __name__ == "__main__":
    sys.exit(main())
