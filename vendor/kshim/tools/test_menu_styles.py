#!/usr/bin/env python3
"""编译菜单预设，检查兼容默认值、颜色对比度与非法多选配置。"""
from __future__ import annotations

import argparse
import itertools
import os
from pathlib import Path
import shlex
import subprocess
import tempfile

STYLES = ('FLAT', 'CLASSIC', 'CARDS', 'TERMINAL', 'MINIMAL', 'HIGH_CONTRAST', 'FLUENT', 'MATERIAL3', 'SURFACE', 'CUPERTINO', 'GLASS', 'AURORA', 'SOFT_UI', 'BENTO', 'NEON', 'NORD', 'MATERIAL2', 'MATERIAL2_DARK', 'FLUENT2', 'FLUENT2_DARK', 'FLUENT_DARK', 'SURFACE_DARK', 'HARMONYOS', 'CLOVER', 'IOS_HIG', 'METRO', 'ONE_UI', 'ADWAITA', 'HOLO', 'LVGL_DEFAULT')
ROOT = Path(__file__).resolve().parents[1]
SOURCE = ROOT / "tests/menu_style_tokens_test.c"
FIELDS = ("screen", "panel", "text", "muted", "item", "focus", "focus_text",
          "boot", "boot_pressed", "boot_text", "border")


# 执行命令并保留完整编译诊断，防止负向测试掩盖无关编译错误。
def run(command: list[str]) -> subprocess.CompletedProcess[str]:
    return subprocess.run(command, text=True, capture_output=True, timeout=45, check=False)


# 根据 sRGB 分量计算相对亮度。
def luminance(color: int) -> float:
    components = [((color >> shift) & 255) / 255.0 for shift in (16, 8, 0)]
    linear = [v / 12.92 if v <= 0.04045 else ((v + 0.055) / 1.055) ** 2.4
              for v in components]
    return sum(a * b for a, b in zip(linear, (0.2126, 0.7152, 0.0722)))


# 计算两个不透明颜色的对比度。
def contrast(first: int, second: int) -> float:
    low, high = sorted((luminance(first), luminance(second)))
    return (high + 0.05) / (low + 0.05)


# 编译并运行一次真实的预处理配置，返回编译结果中的预设数据。
def check_preset(
    compiler: list[str],
    temporary: Path,
    expected: str,
    flags: list[str],
) -> dict[str, int]:
    executable = temporary / "menu-style-test"
    command = compiler + ["-std=c11", "-Wall", "-Wextra", "-Werror", "-pedantic",
                          "-UNDEBUG", *flags, str(SOURCE), "-o", str(executable)]
    result = run(command)
    if result.returncode:
        raise RuntimeError(f"{expected} compile failed:\n{result.stdout}{result.stderr}")
    result = run([str(executable)])
    if result.returncode:
        raise RuntimeError(f"{expected} contract failed:\n{result.stdout}{result.stderr}")
    values = result.stdout.split()
    if len(values) != len(FIELDS) + 1 or values[0] != expected:
        raise RuntimeError(f"Expected {expected}, got {result.stdout!r}")
    return dict(zip(FIELDS, (int(v, 16) for v in values[1:])))


# 将背景与前景按整数 alpha 合成，检查玻璃透明度及渐变状态的可读性。
def mix(background: int, foreground: int, opacity: int) -> int:
    return sum(((((background >> shift) & 255) * (255 - opacity) +
                  ((foreground >> shift) & 255) * opacity + 127) // 255) << shift
               for shift in (0, 8, 16))


# 从真正编译的 C 预设读取效果字段，覆盖渐变中间值和按压态而非只看色板名。
def check_effects(compiler: list[str], temporary: Path, name: str) -> None:
    fields = ("Modern", "Screen", "ScreenEnd", "Panel", "PanelEnd", "PanelOpacity",
              "Item", "ItemOpacity", "Muted", "Text", "Focus", "FocusEnd", "FocusOpacity",
              "FocusText", "Press", "PressText", "Boot", "BootPressed", "BootText", "Layout", "Navigation")
    source = temporary / "effects.c"
    source.write_text('#include <stdio.h>\n#include "menu_style.h"\nint main(void) { printf("' +
                      ' '.join('%u' for _ in fields) + '\\n", ' +
                      ', '.join('(unsigned)mMenuStyle.' + f for f in fields) + '); return 0; }\n')
    executable = temporary / "effects"
    result = run(compiler + ["-std=c11", "-Wall", "-Wextra", "-Werror",
                            "-I", str(ROOT / "src/ui"),
                            f"-DCONFIG_KSHIM_MENU_STYLE_{name}=1", str(source), "-o", str(executable)])
    if result.returncode:
        raise RuntimeError(f"Effect probe failed: {result.stderr}")
    result = run([str(executable)])
    if result.returncode:
        raise RuntimeError(f"Effect probe execution failed: {result.stderr}")
    values = dict(zip(fields, map(int, result.stdout.split())))
    if not values["Modern"]:
        return
    ratios = []
    for opacity in range(0, 256, 17):
        panel = mix(values["Panel"], values["PanelEnd"], opacity)
        focus = mix(values["Focus"], values["FocusEnd"], opacity)
        for screen in (values["Screen"], values["ScreenEnd"]):
            surface = mix(screen, panel, values["PanelOpacity"])
            item = mix(surface, values["Item"], values["ItemOpacity"])
            selected = mix(surface, focus, values["FocusOpacity"])
            ratios.extend((contrast(values["Text"], surface), contrast(values["Muted"], surface),
                           contrast(values["Muted"], item), contrast(values["FocusText"], selected)))
    ratios.extend((contrast(values["PressText"], values["Press"]),
                   contrast(values["BootText"], values["Boot"]),
                   contrast(values["BootText"], values["BootPressed"])))
    if values["Layout"] in (3, 4):
        ratios.append(contrast(values["FocusText"], values["Navigation"]))
    if min(ratios) < 4.5:
        raise RuntimeError(f"{name}: composed/pressed contrast {min(ratios):.2f}:1")
    print(f"PASS: {name} composed gradients/alpha/pressed text >= {min(ratios):.2f}:1")


# 验证真实 Kconfig choice、旧配置默认值、生成头文件与禁用 LVGL 的行为。
def check_kconfig(compiler: list[str], temporary: Path) -> None:
    try:
        import kconfiglib
    except ImportError as error:
        raise RuntimeError("--kconfig requires: python -m pip install kconfiglib") from error
    os.environ.update(srctree=str(ROOT / "src"), HOST_ARCH="x86_64", ARCH="aarch64",
                      HOST_INFO="menu-style-test", BUILD_TIME="test", VERSION="test")
    config = kconfiglib.Kconfig(str(ROOT / "Kconfig"))
    choice = config.named_choices["KSHIM_MENU_STYLE"]
    if choice.selection.name != "KSHIM_MENU_STYLE_FLAT":
        raise RuntimeError("Old/default configuration no longer selects FLAT")
    header = temporary / "config.h"
    for name in STYLES:
        config.syms[f"KSHIM_MENU_STYLE_{name}"].set_value(2)
        selected = [n for n in STYLES if config.syms[f"KSHIM_MENU_STYLE_{n}"].tri_value == 2]
        if selected != [name]:
            raise RuntimeError(f"Kconfig selection is not one-hot: {selected}")
        config.write_autoconf(str(header))
        check_preset(compiler, temporary, name, ["-include", str(header)])
    config.syms["KSHIM_LVGL"].set_value(0)
    if choice.selection is not None or any(config.syms[f"KSHIM_MENU_STYLE_{n}"].tri_value
                                          for n in STYLES):
        raise RuntimeError("Style choice remains enabled without LVGL")
    print(f"PASS: Kconfig default, {len(STYLES)} generated headers, one-hot selection and LVGL-off")


# 运行完整的轻量预设矩阵；实际 LVGL 渲染测试由 tests/CMakeLists.txt 提供。
def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--cc", default=os.environ.get("CC", "cc"))
    parser.add_argument("--kconfig", action="store_true")
    args = parser.parse_args()
    compiler = shlex.split(args.cc)
    if not compiler:
        parser.error("--cc must identify a C compiler")
    try:
        with tempfile.TemporaryDirectory(prefix="kshim-menu-styles-") as directory:
            temporary = Path(directory)
            default = check_preset(compiler, temporary, "FLAT", [])
            palettes = []
            for name in STYLES:
                colors = check_preset(compiler, temporary, name,
                                      [f"-DCONFIG_KSHIM_MENU_STYLE_{name}=1"])
                palettes.append(tuple(colors.values()))
                if name == "FLAT":
                    if colors != default:
                        raise RuntimeError("Implicit and explicit FLAT differ")
                    expected = (0x13235a, 0x161c3e, 0xffffff, 0xc9d1ee, 0xffffff,
                                0x2f6bf0, 0xffffff, 0xf4f7ff, 0xdfe5fb, 0x161c3e, 0xc9d1ee)
                    if tuple(colors.values()) != expected:
                        raise RuntimeError("FLAT palette changed")
                    print("PASS: FLAT legacy colors, opacity, radii and implicit default")
                    continue
                if name == "LVGL_DEFAULT":
                    if any(colors.values()):
                        raise RuntimeError("Native theme must not use a simulated palette")
                    print("PASS: LVGL_DEFAULT native dispatch (live styles tested with LVGL)")
                    continue
                pairs = (("text", "panel"), ("muted", "panel"), ("muted", "item"),
                         ("focus_text", "focus"), ("boot_text", "boot"),
                         ("boot_text", "boot_pressed"))
                minimum = min(contrast(colors[a], colors[b]) for a, b in pairs)
                if minimum < 4.5:
                    raise RuntimeError(f"{name}: opaque text contrast is only {minimum:.2f}:1")
                print(f"PASS: {name}, minimum opaque text contrast {minimum:.2f}:1")
            if len(set(palettes)) != len(STYLES):
                raise RuntimeError("Two presets have identical palettes")
            check_preset(compiler, temporary, "FLAT",
                         [f"-DCONFIG_KSHIM_MENU_STYLE_{n}=0" for n in STYLES])
            for name in STYLES:
                check_preset(compiler, temporary, name,
                             [f"-DCONFIG_KSHIM_MENU_STYLE_{n}={int(n == name)}" for n in STYLES])
            for first, second in itertools.combinations(STYLES, 2):
                result = run(compiler + ["-std=c11", "-fsyntax-only", str(SOURCE),
                                        f"-DCONFIG_KSHIM_MENU_STYLE_{first}=1",
                                        f"-DCONFIG_KSHIM_MENU_STYLE_{second}=1"])
                if result.returncode == 0 or "Select exactly one KSHIM_MENU_STYLE preset" not in result.stderr:
                    raise RuntimeError(f"Invalid multi-selection not diagnosed: {first}, {second}")
            print(f"PASS: zero-valued symbols, explicit 0/1 symbols and all {len(STYLES)*(len(STYLES)-1)//2} invalid pairs")
            for name in STYLES:
                check_effects(compiler, temporary, name)
            if args.kconfig:
                check_kconfig(compiler, temporary)
        return 0
    except (OSError, RuntimeError, subprocess.TimeoutExpired) as error:
        parser.exit(1, f"FAIL: {error}\n")


if __name__ == "__main__":
    raise SystemExit(main())
