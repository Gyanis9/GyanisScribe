#!/usr/bin/env python3
"""源码约定门禁：三条「编译期看不出来、运行时只安静地错」的检查。

一、路径不许走窄串。Windows 上 std::filesystem::path::string() 按本地 ANSI 码页给字节，
   码页装不下的字会当场抛 std::system_error——而抛出点多半在承诺返回 std::expected 的通道里。
   反过来把界面里的路径文本换成路径也要走 wstring。唯一的真源是
   Base::Paths::toDisplayText / fromDisplayText。确属例外（命令行参数要按本地编码交给子进程）
   的调用点必须在同一句里写明理由：不写就红，写了就放过。

二、按枚举分派的开关要覆盖每个值。MSVC 默认不对「switch 漏了某个枚举值」报警（C4061/C4062
   没开），于是漏一支就等于菜单上多一条按了没反应的项。

三、声明出来的信号要有人接。Qt 的 connect 是字符串之外的字符串，编译器不参与：一条没人接的
   信号会让界面上出现「按了没反应」，而菜单、托盘、快捷键这三处入口都靠它转手。
   认的是 &本类名::信号名 这种写法（连到槽、以及信号转信号都算），emit 一处不算消费方。
   类名必须一起参与匹配：只按信号名找，两个类各有一条同名通道时接了的那条会把没人接的那条
   一起冒认掉——贴图那条 ocrRequested 就是这么被放过去过一次的。

跑法：python scripts/check-source-conventions.py [--scan-root src]
退出码：0 通过；1 有违规；2 工具自己坏了（自检探针跑不起来或真源形状变了）——不许当成通过。
"""

from __future__ import annotations

import argparse
import pathlib
import re
import sys

#判据全文是中文：控制台码页不对就把「违规」那条打成乱码，读的人无法据此改代码
sys.stdout.reconfigure(encoding="utf-8", errors="replace")
sys.stderr.reconfigure(encoding="utf-8", errors="replace")

#例外要在同一句里留下的理由关键词
NARROW_REASON = "本地码页"

NEWLINE = chr(10)
CARRIAGE_RETURN = chr(13)

#path → 窄串：QString::fromLocal8Bit(x.string().c_str()) 一类的形状
PATH_TO_NARROW = re.compile(r"QString::fromLocal8Bit\([^;]*?(?:\.|->)string\(\)")
#窄串 → path：std::filesystem::path{某串.toStdString() } 一类的形状
NARROW_TO_PATH = re.compile(r"std::filesystem::path\s*[({][^;]*?\.toStdString\(\)")

#文案与日志里的路径实参：std::format("…", path.string()) 会在本地码页装不下时抛出，
#而抛出点常在「承诺返回 expected」的函数里 —— 该用 Base::Paths::toDisplayText(path).toStdString()
MESSAGE_ARG = re.compile(r"(?:std::format|uiFormat|Logger::)")
PATH_DOT_STRING = re.compile(r"(?:\.|->)string\(\)")

#按枚举分派的开关：(枚举名, 声明所在文件, 分派所在文件)
DISPATCH_TABLES = (
    ("TrayAction", "src/App/TrayAction.h", "src/Main/AppController.cpp"),
    ("MainWindowPage", "src/Main/MainWindow.h", "src/Main/MainWindow.cpp"),
    ("TrayClickRole", "src/App/TrayClick.h", "src/App/TrayClick.cpp"),
    ("TrayClickOutcome", "src/App/TrayClick.h", "src/App/TrayController.cpp"),
)


def is_path_narrow(text: str) -> bool:
    """这一段是不是「路径与窄串互转」；同一段里写了理由的算例外。"""
    if not (PATH_TO_NARROW.search(text) or NARROW_TO_PATH.search(text)):
        return False
    return NARROW_REASON not in text


def is_message_narrow(text: str) -> bool:
    """这一段是不是「文案或日志里把路径换成窄串」；写了理由或已走 toDisplayText 的放过。"""
    if not (MESSAGE_ARG.search(text) and PATH_DOT_STRING.search(text)):
        return False
    if "toDisplayText" in text:
        return False
    return NARROW_REASON not in text


def enumerate_names(text: str, enum_name: str) -> list[str]:
    """从声明文本里取某个 enum class 的成员名：注释掉的一行不算，带行尾注释的一行该算。"""
    # 按名字找那一个声明体，而不是取文件里的第一个再比名字：一个头里放两个枚举是常态
    # （鼠标动作那一族就是档位与结果两个枚举同住一个头），取第一个会把第二个判成「真源形状变了」
    match = re.search(r"enum class " + enum_name + r"\s*\{([^}]*)\}", text.replace(CARRIAGE_RETURN + NEWLINE, NEWLINE), re.S)
    if not match:
        raise RuntimeError(f"找不到 enum class {enum_name} 的声明体：真源形状变了，这条门禁要跟着改，不能静默放行")
    names: list[str] = []
    for line in match.group(1).splitlines():
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
            if is_path_narrow(statement) or is_message_narrow(statement):
                one_line = " ".join(part.strip() for part in statement.splitlines()).strip()
                findings.append(f"{source.relative_to(root.parent)}:{number} 路径与窄串互转，要改走 Base::Paths::toDisplayText / fromDisplayText：{one_line}")
    return findings


def dispatched_names(body: str, enum_name: str) -> set[str]:
    """开关里已分派的枚举值。允许命名空间限定：开关落在无命名空间的外壳里时，
    标签只能是 case GyanisScribe::App::TrayAction::X: 这种全限定形状——
    限定前缀不吃掉本体名，而名字只以它结尾的别的枚举（FooTrayAction）不会冒认。"""
    return set(re.findall(r"case\s+(?:\w+::)*" + enum_name + r"::(\w+)\s*:", body))


def check_dispatch_coverage(root: pathlib.Path) -> list[str]:
    """每个按枚举分派的开关，都要覆盖该枚举的全部取值。"""
    findings: list[str] = []
    for enum_name, header, dispatcher in DISPATCH_TABLES:
        declared = enumerate_names((root.parent / header).read_text(encoding="utf-8", errors="replace"), enum_name)
        body = (root.parent / dispatcher).read_text(encoding="utf-8", errors="replace")
        handled = dispatched_names(body, enum_name)
        for name in declared:
            if name not in handled:
                findings.append(f"{dispatcher} 里没有 {enum_name}::{name} 这一支：按枚举分派漏一格，界面上就是一条按了没反应的项")
    return findings


IDENT_CHARS = set("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_")


def signal_has_handler(texts: list[str], class_name: str, name: str) -> bool:
    """这条信号有没有消费者：要见到「本类名::信号名」并且同一段前面挂着 &。

    类名必须参与匹配：只按信号名找，两个类各有一条同名的通道时，接了的那一条会把没人接的
    那条一起冒认掉（实测就是这样放过贴图那条 ocrRequested 的）。
    """
    needle = class_name + "::" + name
    for text in texts:
        for line in text.splitlines():
            position = line.find(needle)
            while position != -1:
                after = position + len(needle)
                ends_cleanly = after == len(line) or line[after] not in IDENT_CHARS
                if ends_cleanly and "&" in line[:position]:
                    return True
                position = line.find(needle, position + 1)
    return False


CLASS_DECL = re.compile("(class|struct)[ ]+([A-Za-z_][A-Za-z0-9_]*)")


def enclosing_class(line: str) -> str | None:
    """这一行在开一个类定义时给出类名；前向声明和「class QMenu *buildContextMenu();」这种不算。"""
    stripped = line.strip()
    if stripped.endswith(";"):
        return None
    match = CLASS_DECL.search(stripped)
    return match.group(2) if match else None


def collect_signals(root: pathlib.Path) -> list[tuple[pathlib.Path, str, str]]:
    """声明在各 signals: 段里的信号，连着所属类名一起返回（纯文本扫：本条门一律不依赖编译）。"""
    found: list[tuple[pathlib.Path, str, str]] = []
    for source in sorted(list(root.rglob("*.h")) + list(root.rglob("*.cpp"))):
        inside = False
        current_class: str | None = None
        for line in source.read_text(encoding="utf-8", errors="replace").splitlines():
            stripped = line.strip()
            declared = enclosing_class(stripped)
            if declared:
                current_class = declared
            if stripped == "signals:":
                inside = True
                continue
            if inside and stripped in ("private:", "public:", "protected:", "};"):
                inside = False
                continue
            if inside and stripped.startswith("void "):
                name = stripped[5:].split("(")[0].strip()
                if name and current_class:
                    found.append((source, current_class, name))
    return found


def check_signal_consumers(root: pathlib.Path) -> list[str]:
    """每条信号都要有人接：发了没人接的通道，就是界面上的一块「按了没反应」。"""
    texts = [source.read_text(encoding="utf-8", errors="replace") for source in sorted(list(root.rglob("*.h")) + list(root.rglob("*.cpp")))]
    findings: list[str] = []
    for source, class_name, name in collect_signals(root):
        if not signal_has_handler(texts, class_name, name):
            findings.append(f"{source.relative_to(root.parent)}: {class_name}::{name} 发了没人接：要么接出去，要么删掉")
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

    message_shape = '        Base::Logger::info(std::format("已保存：{}", filePath.string()));'
    if not is_message_narrow(message_shape):
        raise RuntimeError("文案里的 path.string() 没被认出来：抛出会被当成可恢复错误漏掉")
    if is_message_narrow('        Base::Logger::info(std::format("已保存：{}", Base::Paths::toDisplayText(filePath).toStdString()));'):
        raise RuntimeError("已经走 toDisplayText 的写法被误报：门禁会红得让人放弃它")

    header = "enum class WidgetAction" + NEWLINE + "{" + NEWLINE + "    Open," + NEWLINE + "    Close, ///< 关掉" + NEWLINE + "    // Deleted," + NEWLINE + "};" + NEWLINE
    if enumerate_names(header, "WidgetAction") != ["Open", "Close"]:
        raise RuntimeError("枚举名穷举错了：注释掉的一行不该算，带 ///< 的一行该算")

#信号消费的判据要两个方向都能给答案：认不出 &类名::信号名 就是永红，把 emit 也算上就是永真
    if signal_has_handler(["        QObject::connect(w, &Widget::valueChanged, this, &Owner::onValue);"], "Widget", "valueChanged") is not True:
        raise RuntimeError("探针：有人接的信号被判成没人接，模式认不出 &类名::信号名")
    if signal_has_handler(["        emit statusChanged(1);"], "Widget", "statusChanged"):
        raise RuntimeError("探针：没人接的信号被放过了，这条判据是永真的")

#同名信号分属两个类时，接了的那条不许替没人接的那条顶名——这正是本条门原先放过的那一种漏
    collision = [
        "        QObject::connect(annotation, &Annotation::AnnotationWindow::ocrRequested, this, &Owner::handle);",
        "        emit ocrRequested(image);",
    ]
    if signal_has_handler(collision, "PinWindow", "ocrRequested"):
        raise RuntimeError("探针：跨类冒认没被拦住，别人接了同名信号就算这条有人接")
    if not signal_has_handler(["        QObject::connect(raw, &PinWindow::ocrRequested, this, &PinManager::pinOcrRequested);"], "PinWindow", "ocrRequested"):
        raise RuntimeError("探针：类限定的转发写法没被认出来，判据成了永红")

#分派检查也要能红：拿一份缺了一支的合成开关喂进去
    missing_case_body = "switch (action)" + NEWLINE + "{" + NEWLINE + "    case WidgetAction::Open:" + NEWLINE + "        return;" + NEWLINE + "}" + NEWLINE
    if "Close" in dispatched_names(missing_case_body, "WidgetAction"):
        raise RuntimeError("分派检查的探针失效：漏掉的那一支应当被认出来")

#外壳里的开关只能写全限定标签：认不出来就是整张表永红（App/Main 拆分后就是这么红的）
    qualified_case_body = "switch (action)" + NEWLINE + "{" + NEWLINE + "    case ns::App::WidgetAction::Open:" + NEWLINE + "        return;" + NEWLINE + "}" + NEWLINE
    if "Open" not in dispatched_names(qualified_case_body, "WidgetAction"):
        raise RuntimeError("分派检查认不出带命名空间限定的 case 标签：全限定形状会被判成整表漏格")

#名字只以它结尾的别的枚举不许冒认：OtherWidgetAction 不是 WidgetAction
    if "Open" in dispatched_names("switch (action) {" + NEWLINE + "    case OtherWidgetAction::Open:" + NEWLINE + "}" + NEWLINE, "WidgetAction"):
        raise RuntimeError("分派检查把名字以它结尾的另一个枚举认成了本体")


def main() -> int:
    parser = argparse.ArgumentParser(description="源码约定门禁：路径窄串、枚举分派、信号消费方")
    parser.add_argument("--scan-root", default=str(pathlib.Path(__file__).resolve().parent.parent / "src"), help="要扫的源码根目录")
    args = parser.parse_args()

    root = pathlib.Path(args.scan_root).resolve()
    try:
        self_probe()
        findings = check_path_encoding(root) + check_dispatch_coverage(root) + check_signal_consumers(root)
    except RuntimeError as failure:
        print(f"[门禁坏了] {failure}", file=sys.stderr)
        return 2

    for finding in findings:
        print(f"[违规] {finding}", file=sys.stderr)
    if findings:
        print(f"[失败] 源码约定检查报了 {len(findings)} 处", file=sys.stderr)
        return 1
    print(f"[ok] 源码约定检查没有发现违规（扫 {root}）")
    return 0


if __name__ == "__main__":
    sys.exit(main())
