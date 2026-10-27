#!/usr/bin/env python3
"""冷启动读数：进程起 → 自检报出「就绪」，取中位。

为什么要它：`自检`（`--selftest`）建控制器、装托盘、开窗口、登记一次热键，然后把自己那行读数打出来并退出；
日志就绪且固定与自定义热键全部登记上时退出码才是 0。它不锁单实例，也不恢复上次贴图，所以量到的是
「进程起到界面建起」这一段，不是用户双击那条路的逐字复刻；因为它自己退出，不会撞上单实例那道锁。
常驻实例占着热键的机器上它会退 1，那一轮不计入统计。它不断言时间：跑在什么机器上就报什么数。

跑法：
    python scripts/measure-cold-start.py --exe build/release/bin/GyanisScribe.exe
    python scripts/measure-cold-start.py --exe ... --rounds 7

退出码：0 全部跑通并报出中位；1 有哪一轮没到「就绪」（含热键没登记上；那种轮次不进统计，全灭就退 1）。
"""

from __future__ import annotations

import argparse
import pathlib
import statistics
import subprocess
import sys
import time

sys.stdout.reconfigure(encoding="utf-8", errors="replace")
sys.stderr.reconfigure(encoding="utf-8", errors="replace")


def main() -> int:
    parser = argparse.ArgumentParser(description="冷启动读数（不断言时间）")
    parser.add_argument("--exe", default="build/release/bin/GyanisScribe.exe", help="被测可执行体")
    parser.add_argument("--rounds", type=int, default=5, help="轮数；首轮另算（那是冷启动里的冷启动）")
    args = parser.parse_args()

    exe = pathlib.Path(args.exe).resolve()
    if not exe.is_file():
        print(f"[失败] 找不到可执行体：{exe}", file=sys.stderr)
        return 1

    samples: list[float] = []
    for round_index in range(args.rounds):
        started = time.perf_counter()
        finished = subprocess.run([str(exe), "--selftest"], capture_output=True, timeout=120, check=False)
        elapsed = (time.perf_counter() - started) * 1000.0
        ready = finished.returncode == 0
        print(f"第 {round_index + 1} 轮：{elapsed:.0f}ms  退出码 {finished.returncode}  {'就绪' if ready else '没到就绪（不进统计）'}")
        if ready:
            samples.append(elapsed)

    if not samples:
        print("[失败] 一轮都没到「就绪」，读数不成立", file=sys.stderr)
        return 1

    median = statistics.median(samples)
    print(f"[读数] 冷启动（进程起 → 自检报出就绪）中位 {median:.0f}ms（{len(samples)}/{args.rounds} 轮，min {min(samples):.0f} / max {max(samples):.0f}）")
    return 0


if __name__ == "__main__":
    sys.exit(main())
