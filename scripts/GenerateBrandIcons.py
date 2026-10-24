#!/usr/bin/env python3
"""生成智录品牌图标的八档 SVG/PNG 母版与 Windows 的 .ico。

跑法（仓库根目录）：
    python scripts/GenerateBrandIcons.py

每档 SVG 由本脚本按尺寸表直接写出，不做缩放；PNG 交给 Chrome/Edge 以透明底按原生尺寸
光栅化；.ico 由脚本按 ICO 目录格式手工拼装，条目内容直接放 PNG 数据（Vista 起 Windows
与 Qt 的 ICO 读取都认这种条目）。512 不进 .ico：目录的宽高字段上限是 256。
浏览器路径可用环境变量 CHROME 覆盖。
"""

from __future__ import annotations

import os
import struct
import subprocess
import sys
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
OUT = ROOT / "src" / "Gui" / "Resources" / "Brand"
BLACK = "#151515"
WHITE = "#F5F5F3"
GREEN = "#5BE37A"

# 尺寸: (砖圆角, 框路径内缩, 框线宽, 框角半径, 录点直径)
# 16/24/32 的框只占 75% 外框，48 以上回到 62.5%，与 96 母版一致。
SIZES = {
    16: (4, 3.0, 2, 2, 4),
    24: (6, 4.0, 2, 4, 5),
    32: (8, 5.5, 3, 5, 6),
    48: (12, 11.0, 4, 7, 9),
    64: (16, 15.0, 6, 9, 12),
    128: (32, 30.0, 12, 19, 24),
    256: (64, 60.0, 24, 37, 48),
    512: (128, 120.0, 48, 75, 96),
}
MASTER = (96, 24, 22.5, 9, 14, 18)


def frame_path(size: float, inset: float, radius: float) -> str:
    """白框路径：从顶边中点起笔，逆时针绕左上、左下、右下三个圆角，止于右边中点。

    右上 90 度缺口由此留出。整条路径逆时针走，三个转角都是凸弧，故 sweep 标志全为 0。
    """
    c = size / 2.0
    r = radius
    return (
        f"M{c:g} {inset:g}H{inset + r:g}"
        f"A{r:g} {r:g} 0 0 0 {inset:g} {inset + r:g}"
        f"V{size - inset - r:g}"
        f"A{r:g} {r:g} 0 0 0 {inset + r:g} {size - inset:g}"
        f"H{size - inset - r:g}"
        f"A{r:g} {r:g} 0 0 0 {size - inset:g} {size - inset - r:g}"
        f"V{c:g}"
    )


def svg(size: int, tile_radius: float, inset: float, stroke: float, radius: float, dot: float) -> str:
    c = size / 2.0
    return (
        f'<svg xmlns="http://www.w3.org/2000/svg" width="{size}" height="{size}" viewBox="0 0 {size} {size}">\n'
        f'  <rect width="{size}" height="{size}" rx="{tile_radius:g}" fill="{BLACK}"/>\n'
        f'  <path d="{frame_path(size, inset, radius)}" fill="none" stroke="{WHITE}"'
        f' stroke-width="{stroke:g}" stroke-linecap="round"/>\n'
        f'  <circle cx="{c:g}" cy="{c:g}" r="{dot / 2:g}" fill="{GREEN}"/>\n'
        f"</svg>\n"
    )


def browser() -> str:
    candidates = [
        os.environ.get("CHROME"),
        r"C:\Program Files\Google\Chrome\Application\chrome.exe",
        r"C:\Program Files (x86)\Microsoft\Edge\Application\msedge.exe",
    ]
    for candidate in candidates:
        if candidate and Path(candidate).exists():
            return candidate
    raise SystemExit("Chrome/Edge not found; set CHROME")


def render(svg_text: str, size: int, output: Path) -> None:
    html = (
        '<!doctype html><html><head><meta charset="utf-8"><style>'
        f"html,body{{margin:0;padding:0;width:{size}px;height:{size}px;overflow:hidden;background:transparent}}"
        "svg{display:block}"
        f"</style></head><body>{svg_text}</body></html>"
    )
    with tempfile.TemporaryDirectory() as temp:
        temp_dir = Path(temp)
        page = temp_dir / "icon.html"
        page.write_text(html, encoding="utf-8")
        subprocess.run(
            [
                browser(), "--headless=new", "--disable-gpu", "--hide-scrollbars",
                "--no-first-run", "--no-default-browser-check", "--force-device-scale-factor=1",
                "--default-background-color=00000000", f"--window-size={size},{size}",
                f"--user-data-dir={temp_dir / 'profile'}", f"--screenshot={output}",
                page.as_uri(),
            ],
            check=True,
            stdout=subprocess.DEVNULL,
            stderr=subprocess.DEVNULL,
        )


def png_header(path: Path) -> tuple[int, int, int, int]:
    """读 PNG 的 IHDR：返回 (宽, 高, 位深, 颜色类型)；颜色类型 6 才是带 alpha 的 RGBA。"""
    data = path.read_bytes()[:33]
    if data[:8] != b"\x89PNG\r\n\x1a\n" or data[12:16] != b"IHDR":
        raise SystemExit(f"{path.name} 不是 PNG")
    width, height, depth, color_type = struct.unpack(">IIBB", data[16:26])
    return width, height, depth, color_type


def pack_ico(output: Path) -> None:
    sizes = [16, 24, 32, 48, 64, 128, 256]
    payloads = [(size, (OUT / f"AppIcon{size}.png").read_bytes()) for size in sizes]
    header = struct.pack("<HHH", 0, 1, len(payloads))
    offset = 6 + 16 * len(payloads)
    directory = b""
    data = b""
    for size, png in payloads:
        side = 0 if size == 256 else size
        directory += struct.pack("<BBBBHHII", side, side, 0, 0, 1, 32, len(png), offset)
        data += png
        offset += len(png)
    output.write_bytes(header + directory + data)


def verify(sizes: list[int]) -> None:
    """逐张核对文件确实落在原生尺寸上、且带 alpha 通道；数目不对就当场停。"""
    for size in sizes:
        width, height, _depth, color_type = png_header(OUT / f"AppIcon{size}.png")
        if (width, height) != (size, size):
            raise SystemExit(f"AppIcon{size}.png 实际是 {width}x{height}")
        if color_type != 6:
            raise SystemExit(f"AppIcon{size}.png 没有 alpha 通道（颜色类型 {color_type}）")
    ico = (OUT / "AppIcon.ico").read_bytes()
    count = struct.unpack("<H", ico[4:6])[0]
    if count != 7:
        raise SystemExit(f"AppIcon.ico 里有 {count} 个条目，应为 7")
    print(f"verified {len(sizes)} PNGs + AppIcon.ico ({count} entries)")


def main() -> None:
    OUT.mkdir(parents=True, exist_ok=True)
    (OUT / "AppIcon.svg").write_text(svg(*MASTER), encoding="utf-8", newline="\n")
    for size, (tile, inset, stroke, radius, dot) in SIZES.items():
        text = svg(size, tile, inset, stroke, radius, dot)
        (OUT / f"AppIcon{size}.svg").write_text(text, encoding="utf-8", newline="\n")
        render(text, size, OUT / f"AppIcon{size}.png")
    pack_ico(OUT / "AppIcon.ico")
    verify(sorted(SIZES))
    print(f"wrote brand assets to {OUT}")


if __name__ == "__main__":
    sys.exit(main())
