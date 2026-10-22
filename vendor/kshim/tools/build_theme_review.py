#!/usr/bin/env python3
"""Verify CI menu artifacts, then build an offline viewer without repainting PNGs.

Input is the all-menu-previews artifact extracted as menu-THEME/previews/.
Source stamps identify the rendered checkout, including PR merge checkouts.
No GitHub access, dependency installation or firmware execution is performed.
"""
from __future__ import annotations
import argparse
import base64
import hashlib
import json
import re
import struct
import tempfile
import zlib
from pathlib import Path

THEMES = tuple("FLAT CLASSIC CARDS TERMINAL MINIMAL HIGH_CONTRAST FLUENT MATERIAL3 SURFACE CUPERTINO GLASS AURORA SOFT_UI BENTO NEON NORD MATERIAL2 MATERIAL2_DARK FLUENT2 FLUENT2_DARK FLUENT_DARK SURFACE_DARK HARMONYOS CLOVER IOS_HIG METRO ONE_UI ADWAITA HOLO LVGL_DEFAULT".split())
SIZES = ((1920, 1080), (1080, 2340))
STAGES = ("entry-120ms", "ready-400ms", "focus-130ms", "confirm-90ms")
SHA = re.compile(r"[0-9a-f]{40}")
FILENAME = re.compile(r"menu-([0-9]+)x([0-9]+)-(entry-120ms|ready-400ms|focus-130ms|confirm-90ms)\.png")


def safe_path(root: Path, name: str) -> Path:
    path = (root / name).resolve()
    if not path.is_relative_to(root.resolve()):
        raise ValueError(f"Path escapes dataset: {name}")
    return path


def paeth(a,b,c):
    p=a+b-c; da=abs(p-a);db=abs(p-b);dc=abs(p-c)
    return a if da<=db and da<=dc else b if db<=dc else c

def png_rgb(data:bytes):
    if not data.startswith(b'\x89PNG\r\n\x1a\n'): raise ValueError('Not a PNG')
    p=8; compressed=[]; header=None; ended=False
    while p<len(data):
        if p+12>len(data): raise ValueError('Truncated PNG')
        n=struct.unpack('>I',data[p:p+4])[0];kind=data[p+4:p+8];value=data[p+8:p+8+n]
        if p+12+n>len(data): raise ValueError('Truncated chunk')
        want=struct.unpack('>I',data[p+8+n:p+12+n])[0]
        if zlib.crc32(kind+value)&0xffffffff!=want: raise ValueError('PNG CRC mismatch')
        if kind==b'IHDR': header=struct.unpack('>IIBBBBB',value)
        if kind==b'IDAT': compressed.append(value)
        p+=12+n
        if kind==b'IEND': ended=True;break
    if not header or not ended or p!=len(data): raise ValueError('Invalid PNG structure')
    w,h,bits,color,compression,method,interlace=header
    if bits!=8 or color not in (2,6) or compression or method or interlace: raise ValueError('Only non-interlaced RGB/RGBA8 is supported')
    if not 1<=w<=4096 or not 1<=h<=4096: raise ValueError('Unexpected frame dimensions')
    bpp=3 if color==2 else 4; stride=w*bpp; expected=(stride+1)*h
    decoder=zlib.decompressobj();raw=decoder.decompress(b''.join(compressed),expected+1)
    if len(raw)!=expected or not decoder.eof or decoder.unused_data: raise ValueError('PNG decompressed size mismatch')
    previous=bytearray(stride); pixels=bytearray()
    for y in range(h):
        f=raw[y*(stride+1)];row=bytearray(raw[y*(stride+1)+1:(y+1)*(stride+1)])
        if f>4: raise ValueError('Unknown PNG filter')
        if f:
            for i in range(stride):
                a=row[i-bpp] if i>=bpp else 0;b=previous[i];c=previous[i-bpp] if i>=bpp else 0
                v=a if f==1 else b if f==2 else (a+b)//2 if f==3 else paeth(a,b,c)
                row[i]=(row[i]+v)&255
        pixels.extend(row);previous=row
    if color==6:
        if any(a!=255 for a in pixels[3::4]): raise ValueError('Non-opaque frame requires explicit compositing')
        rgb=bytearray(w*h*3);rgb[0::3]=pixels[0::4];rgb[1::3]=pixels[1::4];rgb[2::3]=pixels[2::4];pixels=rgb
    return w,h,bytes(pixels)


def read_set(root: Path, *, themes=THEMES, sizes=SIZES,
             supplied_source: str | None = None) -> dict:
    """Read a complete artifact matrix, checking hashes, pixels, stamps and logs.

    Old artifacts without stamps may be labelled with an explicit source from
    their CI run. This is recorded as supplied, never as an embedded stamp.
    """
    root = Path(root)
    if supplied_source is not None and not SHA.fullmatch(supplied_source):
        raise ValueError("Invalid supplied source commit")
    result = dict(frames=[], bytes={}, evidence={}, sources=set(), tests_passed=0)
    stamped = 0
    for theme in themes:
        prefix = f"menu-{theme}/previews/"
        manifest_name = prefix + "manifest.json"
        manifest_bytes = safe_path(root, manifest_name).read_bytes()
        manifest = json.loads(manifest_bytes)
        if not isinstance(manifest, list):
            raise ValueError(f"Invalid manifest: {theme}")
        result['evidence'][manifest_name] = manifest_bytes
        stamp_name = prefix + "source-commit.txt"
        stamp = safe_path(root, stamp_name)
        if stamp.exists():
            source = stamp.read_text().strip()
            result['evidence'][stamp_name] = stamp.read_bytes()
            stamped += 1
            if supplied_source is not None and source != supplied_source:
                raise ValueError("Supplied source disagrees with original source stamp")
        else:
            source = supplied_source
        if not isinstance(source, str) or not SHA.fullmatch(source):
            raise ValueError(f"Missing or invalid source identity: {theme}")
        result['sources'].add(source)
        for item in manifest:
            name = item['file']
            match = FILENAME.fullmatch(name) if isinstance(name, str) else None
            if match is None:
                raise ValueError(f"Invalid frame filename: {name}")
            w, h = int(match[1]), int(match[2])
            if (item['width'], item['height']) != (w, h):
                raise ValueError(f"Frame dimensions disagree with name: {name}")
            if (w, h) not in sizes:
                raise ValueError(f"Unexpected frame dimensions: {name}")
            key = f"{theme}|{w}x{h}|{match[3]}"
            if key in result['bytes']:
                raise ValueError(f"Duplicate frame: {key}")
            path = prefix + name
            file = safe_path(root, path)
            if file.stat().st_size > 20 * 1024 * 1024:
                raise ValueError(f"Frame exceeds compressed-size budget: {name}")
            raw = file.read_bytes()
            digest = hashlib.sha256(raw).hexdigest()
            if digest != item['sha256']:
                raise ValueError(f"Frame hash mismatch: {path}")
            pw, ph, rgb = png_rgb(raw)
            if (pw, ph) != (w, h):
                raise ValueError(f"Decoded frame dimensions mismatch: {path}")
            result['frames'].append(dict(key=key, theme=theme, width=w, height=h,
                stage=match[3], path=path, sha256=digest,
                pixel_sha256=hashlib.sha256(rgb).hexdigest()))
            result['bytes'][key] = raw
        for optional in ('render.log',):
            name = prefix + optional
            p = safe_path(root, name)
            if p.exists():
                result['evidence'][name] = p.read_bytes()
        log_name = f"menu-{theme}/build-menu/Testing/Temporary/LastTest.log"
        log_data = safe_path(root, log_name).read_bytes()
        log = log_data.decode('utf-8')
        passed = len(re.findall(r'^Test Passed\.$', log, re.MULTILINE))
        if not passed or 'Test Failed.' in log or 'End testing:' not in log:
            raise ValueError(f"Failed or incomplete test log: {theme}")
        result['tests_passed'] += passed
        result['evidence'][log_name] = log_data
    if len(result['sources']) != 1:
        raise ValueError("Mixed source commits in artifact matrix")
    required = {f"{n}|{w}x{h}|{s}" for n in themes for w, h in sizes for s in STAGES}
    missing = required - set(result['bytes'])
    if missing:
        raise ValueError(f"Missing {len(missing)} frames: {min(missing)}")
    result['source'] = result.pop('sources').pop()
    result['source_kind'] = 'stamped' if stamped == len(themes) else 'supplied' if not stamped else 'mixed-stamps-and-supplied'
    result['themes'] = list(themes)
    result['sizes'] = [f"{w}x{h}" for w, h in sizes]
    return result


def compare_sets(old: dict, new: dict) -> dict:
    """Check FLAT's decoded pixels and refuse renamed-but-unchanged themes."""
    if old['source'] == new['source']:
        raise ValueError("Before/after source identities are identical")
    old_frames = {f['key']: f for f in old['frames']}
    new_frames = {f['key']: f for f in new['frames']}
    if old_frames.keys() != new_frames.keys():
        raise ValueError("Before/after frame matrices differ")
    count = 0
    changed = set()
    for key, after in new_frames.items():
        same = after['pixel_sha256'] == old_frames[key]['pixel_sha256']
        if after['theme'] == 'FLAT':
            if not same or png_rgb(old['bytes'][key]) != png_rgb(new['bytes'][key]):
                raise ValueError(f"FLAT pixel regression: {key}")
            count += 1
        elif not same:
            changed.add(after['theme'])
    unchanged = set(new['themes']) - {'FLAT'} - changed
    if unchanged:
        raise ValueError(f"Redesign is unchanged for: {sorted(unchanged)}")
    return dict(flat_matches=count, changed_themes=sorted(changed))


def write_review(new: dict, output: Path, old: dict | None = None) -> dict:
    """Write relative and embedded viewers atomically to a new directory."""
    output = Path(output)
    if output.exists():
        raise ValueError(f"Output already exists: {output}")
    checks = compare_sets(old, new) if old is not None else dict(flat_matches=None, changed_themes=[])
    summary = dict(new_source=new['source'], old_source=old['source'] if old else None,
        new_source_kind=new['source_kind'], old_source_kind=old['source_kind'] if old else None,
        new_frames=len(new['frames']), old_frames=len(old['frames']) if old else 0,
        new_tests_passed=new['tests_passed'], **checks)
    template = Path(__file__).with_name('theme_review.html').read_text(encoding='utf-8')
    if template.count('__DATA__') != 1:
        raise ValueError('Viewer template must contain one dataset placeholder')
    versions = {'new': new}
    if old is not None:
        versions['old'] = old
    dataset = dict(names=new['themes'], sizes=new['sizes'], stages=list(STAGES),
        summary=summary, images={}, meta={})
    output.parent.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix='.theme-review-', dir=output.parent) as temp:
        target = Path(temp)
        for version, data in versions.items():
            dataset['images'][version] = {}
            dataset['meta'][version] = {}
            for frame in data['frames']:
                relative = version + '/' + frame['path']
                file = target / relative
                file.parent.mkdir(parents=True, exist_ok=True)
                file.write_bytes(data['bytes'][frame['key']])
                dataset['images'][version][frame['key']] = relative
                dataset['meta'][version][frame['key']] = frame['sha256']
            for relative, raw in data['evidence'].items():
                file = target / version / relative
                file.parent.mkdir(parents=True, exist_ok=True)
                file.write_bytes(raw)
        def html_file(name):
            text = json.dumps(dataset, ensure_ascii=False, separators=(',', ':')).replace('<', '\\u003c')
            (target / name).write_text(template.replace('__DATA__', text), encoding='utf-8')
        html_file('index.html')
        for version, data in versions.items():
            dataset['images'][version] = {key: 'data:image/png;base64,' + base64.b64encode(raw).decode('ascii')
                                         for key, raw in data['bytes'].items()}
        html_file('standalone.html')
        manifest = dict(summary=summary, frames={v: d['frames'] for v, d in versions.items()})
        (target / 'verification.json').write_text(json.dumps(manifest, indent=2) + '\n', encoding='utf-8')
        if output.exists():
            raise ValueError(f"Output already exists: {output}")
        target.rename(output)
    return summary


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--input', required=True, type=Path, help='Extracted all-menu-previews artifact')
    parser.add_argument('--output', required=True, type=Path, help='New directory; never overwrites')
    parser.add_argument('--expected-source', help='Optional exact expected stamp; never relabels frames')
    parser.add_argument('--baseline', type=Path, help='Optional older artifact matrix')
    parser.add_argument('--baseline-source', help='Explicit CI commit for legacy artifacts without stamps')
    args = parser.parse_args()
    if args.baseline_source and not args.baseline:
        parser.error('--baseline-source requires --baseline')
    current = read_set(args.input)
    if args.expected_source is not None and current['source'] != args.expected_source:
        raise ValueError('Unexpected source commit in current artifacts')
    previous = read_set(args.baseline, supplied_source=args.baseline_source) if args.baseline else None
    print(json.dumps(write_review(current, args.output, previous), indent=2))


if __name__ == '__main__':
    try:
        main()
    except (ValueError, OSError, KeyError, TypeError, struct.error, zlib.error) as error:
        raise SystemExit(f"ERROR: {error}")
