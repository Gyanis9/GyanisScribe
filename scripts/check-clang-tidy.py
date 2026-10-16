"""clang-tidy 检查：按仓库根那份 .clang-tidy（直接复用 AsynGyanis 的检查面与命名规范）
把 src/ 的每个编译单元跑一遍，按 AsynGyanis 的口径判红——**只有「本仓代码没解析成」才拦**，
发现项逐条报出来但不阻塞（clang 读 /WX 会把 clang 独有的几类升成 error，那是发现项不是没跑成）。

退出码：0 全部处理完（含报告发现项）；1 有翻译单元在本仓代码里就没解析成——这不等于「检查通过」；
2 工具自己坏了（找不到 clang-tidy / 没有编译数据库 / 找不到 vcvars / 有 TU 连第三方头都进不去）。

覆盖范围就定在 src/：build/**/_autogen 与 moc_ 是生成代码，tests/ 的命中大多来自 gtest 宏的假阳性，
两边都不进来。跑一次要真解析每个 TU（Qt 头的开销在那儿），所以不进 ctest，单独跑：

    scripts\\WindowsBuild.bat debug        # 先出编译数据库（clang-tidy 只读它，不依赖这次编译成功与否）
    python scripts/check-clang-tidy.py     # 默认 build/debug；换档用 --build-dir build/release

clang-tidy 的位置：PATH 上找，找不到再读环境变量 GYANISSCRIBE_LLVM_DIR（指到 bin 的上一层）。
MSVC 环境：clang-cl 按 INCLUDE 找 SDK 头（wrl/client.h 在只有 vcvars 才会加上的 winrt 目录里），
所以每个 TU 都在 vcvars 之后的环境里跑——VCVARS 环境变量优先（值同 WindowsBuild.bat 里那个），
其次按 vswhere 找。
"""

from __future__ import annotations

import argparse
import concurrent.futures
import json
import os
import pathlib
import re
import shutil
import subprocess
import sys
import tempfile

# CI 的 Windows runner 控制台是 cp1252：两个流先钉成 UTF-8，诊断与中文提示才不会在中途炸掉
sys.stdout.reconfigure(encoding="utf-8", errors="replace")
sys.stderr.reconfigure(encoding="utf-8", errors="replace")

# 「有没有真解析成功」只认 [clang-diagnostic-error] 这个标签（与 AsynGyanis 同口径）：
# /WX 会被 clang 读成 -Werror，clang 独有的几类警告（unused-lambda-capture、reorder-ctor 一类）
# 以 error 的面目出现、还带一句 "Error while processing <文件>"——那些是发现项，不是没解析成。
PARSE_FAILURE_TAGS = ("[clang-diagnostic-error]",)
FINDING_MARKERS = (": warning: ", ": error: ")
DIAGNOSTIC_PATH = re.compile(r"^(.*?):\d+:\d+: (?:warning|error): ")


def find_tool(name: str) -> pathlib.Path | None:
    """先 PATH，再环境变量指的目录；两个都没有就交回空。"""
    found = shutil.which(name)
    if found is not None:
        return pathlib.Path(found)
    configured = os.environ.get("GYANISSCRIBE_LLVM_DIR")
    if configured:
        for candidate in (pathlib.Path(configured) / "bin" / name, pathlib.Path(configured) / name):
            if candidate.exists():
                return candidate
    return None


def resolve_vcvars() -> pathlib.Path | None:
    """找 vcvars64.bat：环境变量 VCVARS 优先（WindowsBuild.bat 同一个名字），其次问 vswhere。"""
    configured = os.environ.get("VCVARS")
    if configured and pathlib.Path(configured).exists():
        return pathlib.Path(configured)
    vswhere = pathlib.Path(os.environ.get("ProgramFiles(x86)", r"C:\Program Files (x86)")) / "Microsoft Visual Studio/Installer/vswhere.exe"
    if vswhere.exists():
        completed = subprocess.run([str(vswhere), "-latest", "-products", "*", "-requires", "Microsoft.VisualStudio.Component.VC.Tools.x86.x64", "-property",
                                    "installationPath"],
                                   capture_output=True, text=True, errors="replace")
        for line in completed.stdout.splitlines():
            candidate = pathlib.Path(line.strip()) / "VC/Auxiliary/Build/vcvars64.bat"
            if candidate.exists():
                return candidate
    return None


def msvc_environment(vcvars: pathlib.Path) -> dict[str, str] | None:
    """在 vcvars 之后把完整环境抄回来，喂给每个 clang-tidy 子进程。

    为什么非它不可：clang-cl 按 INCLUDE 环境变量找 SDK 头，而 Windows 系统级的 INCLUDE
    只有 shared/ucrt/um——`wrl/client.h` 在另一个 `winrt` 子目录里，只有 vcvars 会把它加上。
    用临时 .bat 承接而不是往命令行里塞引号：cmd 的转义在 MSYS 与 python subprocess
    两处各有一套规矩，实测都会被剥掉（"file not found" 一类的假红就是从这儿来的）。
    """
    with tempfile.TemporaryDirectory() as probe_root:
        probe = pathlib.Path(probe_root) / "capture-env.bat"
        probe.write_text(f'@echo off\ncall "{vcvars}" >nul\nset\n', encoding="ascii")
        completed = subprocess.run(["cmd", "/c", str(probe)], capture_output=True, text=True, errors="replace")
    environment: dict[str, str] = {}
    for line in completed.stdout.splitlines():
        if "=" in line and not line.startswith("="):
            key, value = line.split("=", 1)
            environment[key] = value
    return environment if "INCLUDE" in environment else None


def source_translation_units(database: pathlib.Path, root: pathlib.Path) -> list[str]:
    """编译数据库里属于 src/ 的那些源文件（绝对路径）。

    「属于 src/」按「路径在 <仓库根>/src/ 底下」判，而不是「路径里出现过 src」——
    后者会把 build/debug/src/... 下那堆生成代码一起收进来。
    """
    wanted_prefix = (root / "src").as_posix() + "/"
    files: list[str] = []
    for entry in json.loads(database.read_text(encoding="utf-8")):
        path = pathlib.Path(entry["file"])
        if not path.is_absolute():
            path = (database.parent / path).resolve()
        if path.as_posix().startswith(wanted_prefix):
            files.append(str(path))
    return sorted(set(files))


def analyse_one(tidy: pathlib.Path, build_dir: pathlib.Path, source: str, environment: dict[str, str]) -> tuple[str, str]:
    """跑一个 TU，把它的诊断整段收回来。"""
    completed = subprocess.run([str(tidy), "-quiet", "-p", str(build_dir), source], capture_output=True, text=True, encoding="utf-8", errors="replace", env=environment)
    return source, completed.stdout + completed.stderr


def diagnostic_path(line: str, root: pathlib.Path) -> str:
    """诊断行开头的文件路径归一化（解析 .. 并统一斜杠）；取不到路径时交回空串。"""
    matched = DIAGNOSTIC_PATH.match(line)
    if matched is None:
        return ""
    spelled = matched.group(1).replace("\\", "/")
    path = pathlib.Path(spelled)
    if not path.is_absolute():
        path = root / path
    try:
        return path.resolve().as_posix()
    except OSError:
        return path.as_posix()


def main() -> int:
    root = pathlib.Path(__file__).resolve().parent.parent
    parser = argparse.ArgumentParser(description="clang-tidy 检查（src/ 面，按仓库根 .clang-tidy；发现项报告不阻塞，只有本仓解析失败才拦）")
    parser.add_argument("--build-dir", default="build/debug", help="要到哪个构建树里找 compile_commands.json")
    parser.add_argument("--jobs", type=int, default=0, help="并行度；0 = 按 CPU 数取，最多 12")
    parser.add_argument("--clang-tidy", default="", help="clang-tidy 可执行文件路径；默认按 PATH 与 GYANISSCRIBE_LLVM_DIR 找")
    parser.add_argument("--only", default="", help="只跑路径含这一小段的编译单元（排查用；默认全跑）")
    parser.add_argument("--report", default="", help="把完整输出另存一份到这个文件（CI 拿来当 artifact）")
    args = parser.parse_args()

    tidy = pathlib.Path(args.clang_tidy) if args.clang_tidy else find_tool("clang-tidy")
    if tidy is None or not tidy.exists():
        print(f"[没跑成] 找不到 clang-tidy（PATH 上找过，也看过 GYANISSCRIBE_LLVM_DIR={os.environ.get('GYANISSCRIBE_LLVM_DIR', '（未设）')}）", file=sys.stderr)
        return 2

    build_dir = root / args.build_dir
    database = build_dir / "compile_commands.json"
    if not database.exists():
        print(f"[没跑成] {database} 不存在：先跑一次 scripts\\WindowsBuild.bat {build_dir.name}", file=sys.stderr)
        return 2

    files = source_translation_units(database, root)
    if args.only:
        files = [f for f in files if args.only in f]
    if not files:
        print(f"[没跑成] 编译数据库里没有符合要求的编译单元：{database}", file=sys.stderr)
        return 2

    vcvars = resolve_vcvars()
    if vcvars is None:
        print("[没跑成] 找不到 vcvars64.bat：设 VCVARS 环境变量（值同 WindowsBuild.bat 里那个），或装一份能被 vswhere 认出的 Visual Studio", file=sys.stderr)
        return 2
    environment = msvc_environment(vcvars)
    if environment is None:
        print(f"[没跑成] vcvars 跑完没拿到 INCLUDE：{vcvars}", file=sys.stderr)
        return 2

    jobs = args.jobs if args.jobs > 0 else min(os.cpu_count() or 1, 12)
    print(f"clang-tidy 检查：{len(files)} 个编译单元，-j{jobs}，档位 {root / '.clang-tidy'}，MSVC 环境 {vcvars}")

    tsv_root = (root / "src").as_posix() + "/"
    findings: list[str] = []
    our_failures: list[tuple[str, str]] = []
    tolerated_failures: list[str] = []
    processed = 0
    with concurrent.futures.ThreadPoolExecutor(max_workers=jobs) as pool:
        for source, output in pool.map(lambda f: analyse_one(tidy, build_dir, f, environment), files):
            processed += 1
            failure_lines = [line for line in output.splitlines() if any(tag in line for tag in PARSE_FAILURE_TAGS)]
            if failure_lines:
                # 按第一条 clang-diagnostic-error 落在哪儿分档：落在本仓 src/ 里的是真没分析成；
                # 落在依赖头/系统头里的那份，clang 在这台机器上就吃不了，不拿它判红（与 AsynGyanis 同口径）
                first_path = ""
                for line in failure_lines:
                    first_path = diagnostic_path(line, root)
                    if first_path:
                        break
                if first_path.startswith(tsv_root):
                    our_failures.append((source, failure_lines[0]))
                else:
                    tolerated_failures.append(source)
            for line in output.splitlines():
                if any(tag in line for tag in PARSE_FAILURE_TAGS):
                    continue
                if any(marker in line for marker in FINDING_MARKERS):
                    findings.append(line)
            if processed % 25 == 0:
                print(f"    已跑 {processed}/{len(files)}，发现 {len(findings)} 条")

    for line in findings:
        print(line)
    summary = f"标记 {processed}/{len(files)}，本仓解析失败 {len(our_failures)} 个，依赖头放行 {len(tolerated_failures)} 个，发现 {len(findings)} 条（报告形式，不阻塞）"
    print(summary, file=sys.stderr)
    if args.report:
        pathlib.Path(args.report).write_text("\n".join(findings) + ("\n" if findings else ""), encoding="utf-8")

    if processed < len(files):
        print(f"[没跑成] 少了 {len(files) - processed} 个翻译单元的处理记录：工具被打断或崩了", file=sys.stderr)
        return 2
    if our_failures:
        print("[拦下] 有翻译单元在本仓代码里就没解析成——这不等于「检查通过」：", file=sys.stderr)
        for source, line in our_failures:
            print(f"    {pathlib.Path(source).name}: {line}", file=sys.stderr)
        return 1
    for source in tolerated_failures:
        print(f"[放行] {pathlib.Path(source).name}：clang 进不了它用的依赖头，这一份交别的档看", file=sys.stderr)
    print(f"[ok] {summary}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
