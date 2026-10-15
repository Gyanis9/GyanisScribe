#!/usr/bin/env python3
"""界面量尺：对离屏出的界面图逐行量「标签左/右缘、控件列左缘、缝宽」，或定位色块列并算节距。

用法：
  python scripts/measure-ui.py <图...>                    # 默认口径逐行量
  python scripts/measure-ui.py <图> --label 215-345 --ctrl 346-470 --y0 130
  python scripts/measure-ui.py <图> --colors "#E83E3E,#F28D32" --band 26

口径：
  暗像素（文字笔画）= 三通道和 < 600；非白（控件边框）= 与纯白差 > 24。
  输出全 ASCII：控制台码页不保证 UTF-8，中文说明放在本文件里，读数不带。
"""

import argparse
import sys

from PIL import Image


def dark(pixel):
    return sum(pixel) < 3 * 200


def off_white(pixel):
    return abs(pixel[0] - 255) + abs(pixel[1] - 255) + abs(pixel[2] - 255) > 24


def parse_range(text):
    low, high = text.split("-")
    return int(low), int(high)


def scan_rows(image, label_x, ctrl_x, y0):
    """Returns [(row_top, row_bottom, label_left, label_right, ctrl_left, gap)]."""
    px = image.load()
    _, height = image.size
    rows, current = [], None
    for y in range(y0, height - 16):
        if any(dark(px[x, y]) for x in range(*label_x)) and current is None:
            current = y
        elif not any(dark(px[x, y]) for x in range(*label_x)) and current is not None:
            if y - 1 - current >= 6:
                rows.append((current, y - 1))
            current = None
    out = []
    for top, bottom in rows:
        left = next((x for x in range(*label_x) if any(dark(px[x, y]) for y in range(top, bottom + 1))), None)
        right = next((x for x in range(label_x[1], label_x[0], -1) if any(dark(px[x, y]) for y in range(top, bottom + 1))), None)
        ctrl = next((x for x in range(ctrl_x[0], min(ctrl_x[1], image.width)) if any(off_white(px[x, y]) for y in range(top, bottom + 1))), None)
        if left and right and ctrl:
            out.append((top, bottom, left, right, ctrl, ctrl - right))
    return out


def scan_color_columns(image, hex_colors, band):
    """Locate each color's column runs on one row band; returns [(color, runs)]."""
    px = image.load()
    results = []
    for token in hex_colors:
        target = (int(token[1:3], 16), int(token[3:5], 16), int(token[5:7], 16))
        columns = [x for x in range(image.width)]
        matched = [x for x in columns if all(abs(px[x, band][channel] - target[channel]) <= 10 for channel in range(3))]
        runs = []
        for column in matched:
            if runs and column == runs[-1][1] + 1:
                runs[-1][1] = column
            else:
                runs.append([column, column])
        results.append((token, [run for run in runs if run[1] - run[0] >= 8]))
    return results


def main(argv):
    parser = argparse.ArgumentParser(description="measure UI screenshots for the audit ledger")
    parser.add_argument("images", nargs="+")
    parser.add_argument("--label", default="215-345", help="label column x range")
    parser.add_argument("--ctrl", default="346-470", help="control column x range")
    parser.add_argument("--y0", type=int, default=130, help="first row to scan")
    parser.add_argument("--colors", help="comma separated hex colors to locate")
    parser.add_argument("--band", type=int, default=0, help="row band for --colors")
    args = parser.parse_args(argv)

    label_x = parse_range(args.label)
    ctrl_x = parse_range(args.ctrl)
    for path in args.images:
        image = Image.open(path).convert("RGB")
        print(f"== {path} ({image.width}x{image.height}) ==")
        if args.colors:
            tokens = [token.strip() for token in args.colors.split(",") if token.strip()]
            results = scan_color_columns(image, tokens, args.band)
            previous = None
            for token, runs in results:
                print(f"  {token}: {runs}")
                if runs and previous is not None:
                    print(f"    pitch from {previous[0]} to {token[1][0]}: {runs[0][0] - previous[1][0][0]}")
                if runs:
                    previous = (token, runs)
            continue
        rows = scan_rows(image, label_x, ctrl_x, args.y0)
        for top, bottom, left, right, ctrl, gap in rows:
            print(f"  y={top}-{bottom} label=[{left}..{right}] ctrl_left={ctrl} gap={gap}")
        gaps = [row[5] for row in rows]
        if gaps:
            print(f"  rows={len(rows)} gap range: {min(gaps)}..{max(gaps)}")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
