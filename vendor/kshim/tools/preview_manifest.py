#!/usr/bin/env python3
"""将真实 LVGL PPM 帧转换为 PNG，生成预览索引与摘要，不重新绘制界面。"""
from __future__ import annotations
import argparse
import hashlib
import json
from pathlib import Path
import struct
import zlib


# 封装 PNG 块，同时计算规范要求的 CRC。
def chunk(kind: bytes, content: bytes) -> bytes:
    return struct.pack('>I', len(content)) + kind + content + struct.pack('>I', zlib.crc32(kind + content) & 0xffffffff)


# 本项目预览程序输出固定的 P6 头；拒绝截断或额外内容。
def convert(path: Path) -> dict[str, object]:
    data = path.read_bytes()
    magic, dimensions, maximum, pixels = data.split(b'\n', 3)
    width, height = map(int, dimensions.split())
    if magic != b'P6' or maximum != b'255' or width <= 0 or height <= 0 or len(pixels) != width * height * 3:
        raise ValueError(f'Invalid PPM: {path}')
    rows = b''.join(b'\0' + pixels[y * width * 3:(y + 1) * width * 3] for y in range(height))
    png = b'\x89PNG\r\n\x1a\n' + chunk(b'IHDR', struct.pack('>IIBBBBB', width, height, 8, 2, 0, 0, 0))
    png += chunk(b'IDAT', zlib.compress(rows, 6)) + chunk(b'IEND', b'')
    out = path.with_suffix('.png')
    out.write_bytes(png)
    return {'file': out.name, 'width': width, 'height': height, 'sha256': hashlib.sha256(png).hexdigest()}


# 输出便于下载后查看的索引，不包含外部字体或网络资源。
def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('directory', type=Path)
    args = parser.parse_args()
    files = sorted(args.directory.glob('*.ppm'))
    if not files:
        parser.error('No real LVGL PPM keyframes were produced')
    result = [convert(p) for p in files]
    (args.directory / 'manifest.json').write_text(json.dumps(result, indent=2) + '\n')
    body = '\n'.join(f'<figure><img src="{v["file"]}" alt="{v["file"]}" style="max-width:100%;max-height:90vh"><figcaption>{v["file"]}</figcaption></figure>' for v in result)
    (args.directory / 'index.html').write_text('<!doctype html><meta charset="utf-8"><title>Real LVGL keyframes</title><h1>Real LVGL keyframes</h1>' + body)
    print(f'Converted {len(result)} real LVGL frames; PNG SHA256 values:')
    print(json.dumps(result, indent=2))


if __name__ == '__main__':
    main()
