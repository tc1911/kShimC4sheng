#!/usr/bin/env python3
"""Render the Atlas UI preview from the same layout/palette as menu_atlas.h.

The real screen is 3048x2032; pages are drawn at half resolution so they stay
readable in the README and in the overview sheet.
"""
from __future__ import annotations

import importlib.util
from pathlib import Path

from PIL import Image, ImageDraw, ImageFilter, ImageFont

ROOT = Path(__file__).resolve().parents[2]
FONT = "/home/tc191/项目/kindel/pkgs/fonts/LXGWWenKai-Regular.ttf"
OUT = Path(__file__).resolve().parent

W, H = 1524, 1016
BG = (7, 17, 30)
BG_BOTTOM = (4, 10, 18)
PANEL = (12, 24, 40)
CARD = (16, 31, 51)
BLUE = (47, 111, 237)
BLUE_DIM = (22, 40, 74)
BORDER = (29, 43, 64)
TEXT = (238, 244, 255)
MUTED = (142, 163, 191)

MARGIN = int(W * 21 / 1000)
HEADER = int(H * 74 / 1000)
FOOTER = int(H * 46 / 1000)
SIDE_W = int(W * 250 / 1000)
CONTENT_X = MARGIN + SIDE_W + int(W * 30 / 1000)
CONTENT_W = W - CONTENT_X - MARGIN
CONTENT_H = H - HEADER - FOOTER - 20
PAD = 28
ROW_H = 52
ROW_GAP = 6

F_TITLE = ImageFont.truetype(FONT, 32)
F_SUB = ImageFont.truetype(FONT, 15)
F_NAV = ImageFont.truetype(FONT, 22)
F_SECTION = ImageFont.truetype(FONT, 22)
F_BODY = ImageFont.truetype(FONT, 14)
F_VALUE = ImageFont.truetype(FONT, 22)
F_SMALL = ImageFont.truetype(FONT, 13)
F_CARD_TITLE = ImageFont.truetype(FONT, 21)
F_BADGE = ImageFont.truetype(FONT, 12)


def text(d, xy, value, f, fill=TEXT, anchor=None):
    d.text(xy, value, font=f, fill=fill, anchor=anchor)


def wrap(d, value, f, max_w):
    lines, line = [], ""
    for ch in value:
        trial = line + ch
        if d.textlength(trial, font=f) <= max_w:
            line = trial
        else:
            lines.append(line)
            line = ch
    if line:
        lines.append(line)
    return lines


def load_icons():
    spec = importlib.util.spec_from_file_location("icons", ROOT / "tools/gen_atlas_icons.py")
    mod = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(mod)
    return {name.lower(): mask for name, mask in mod.icons}


RAW_ICONS = load_icons()


def icon(img, name, xy, size, color=BLUE):
    mask = RAW_ICONS[name].resize((size, size), Image.LANCZOS)
    solid = Image.new("RGB", (size, size), color)
    img.paste(solid, xy, mask)


def gradient():
    img = Image.new("RGB", (W, H), BG)
    d = ImageDraw.Draw(img)
    for y in range(H):
        t = y / (H - 1)
        d.line([(0, y), (W, y)],
               fill=tuple(int(BG[i] * (1 - t) + BG_BOTTOM[i] * t) for i in range(3)))
    glow = Image.new("RGBA", (W, H), (0, 0, 0, 0))
    g = ImageDraw.Draw(glow)
    g.ellipse([W - 410, -180, W + 90, 320], fill=(27, 77, 143, 26))
    g.ellipse([-130, H - 260, 250, H + 120], fill=(18, 58, 107, 26))
    glow = glow.filter(ImageFilter.GaussianBlur(70))
    return Image.alpha_composite(img.convert("RGBA"), glow).convert("RGB")


NAV = [
    ("android", "Android 原厂"),
    ("linux", "Linux"),
    ("gear", "高级启动模式"),
    ("wrench", "设置"),
    ("info", "设备信息"),
    ("restart", "重启"),
    ("power", "关机"),
]


def header(img, d):
    text(d, (MARGIN, 16), "KShimC 启动器", F_TITLE, TEXT)
    text(d, (MARGIN, 58), "Xiaomi Pad 6S Pro · sheng · v0.0.4", F_SUB, MUTED)
    text(d, (W - MARGIN, 36), "简体中文", F_SUB, MUTED, anchor="ra")


def sidebar(img, d, selected):
    x, y = MARGIN, HEADER
    for i, (name, label) in enumerate(NAV):
        ry = y + i * (ROW_H + ROW_GAP)
        if i == selected:
            d.rounded_rectangle([x, ry, x + SIDE_W, ry + ROW_H], radius=8, fill=BLUE)
            d.rounded_rectangle([x, ry, x + 2, ry + ROW_H], radius=1, fill=TEXT)
            icol = TEXT
        else:
            icol = MUTED
        icon(img, name, (x + 12, ry + (ROW_H - 28) // 2 + 1), 28, icol)
        text(d, (x + 48, ry + (ROW_H - 22) // 2 - 1), label, F_NAV, TEXT)


def content_panel(d):
    d.rounded_rectangle([CONTENT_X, HEADER, CONTENT_X + CONTENT_W, HEADER + CONTENT_H],
                        radius=11, fill=PANEL, outline=BORDER, width=1)


def section(d, title, badge=None):
    y = HEADER + PAD
    d.rounded_rectangle([CONTENT_X + PAD, y, CONTENT_X + PAD + 4, y + 26], radius=2, fill=BLUE)
    text(d, (CONTENT_X + PAD + 14, y - 3), title, F_SECTION, TEXT)
    if badge:
        bx = CONTENT_X + CONTENT_W - PAD - 54
        d.rounded_rectangle([bx, y, bx + 54, y + 24], radius=4, outline=BORDER, width=1)
        text(d, (bx + 27, y + 12), badge, F_BADGE, MUTED, anchor="mm")


def card(img, d, y, h, title, desc=None, value=None, name=None):
    x = CONTENT_X + PAD
    w = CONTENT_W - 2 * PAD
    d.rounded_rectangle([x, y, x + w, y + h], radius=7, fill=CARD, outline=BORDER, width=1)
    tx = x + 16
    if name:
        icon(img, name, (x + 15, y + (h - 28) // 2), 28, BLUE)
        tx = x + 56
    text(d, (tx, y + 8), title, F_CARD_TITLE, TEXT)
    if desc:
        for i, line in enumerate(wrap(d, desc, F_BODY, x + w - tx - 46)[:2]):
            text(d, (tx, y + 39 + i * 17), line, F_BODY, MUTED)
    if value:
        text(d, (x + w - 58, y + 9), value, F_VALUE, BLUE, anchor="ra")
    text(d, (x + w - 26, y + h // 2 - 12), "›", F_CARD_TITLE, MUTED)


def back(d, y):
    text(d, (CONTENT_X + PAD, y), "‹  返回主菜单", F_NAV, TEXT)


def footer(d, status):
    y = H - FOOTER
    d.line([(MARGIN, y), (W - MARGIN, y)], fill=BORDER)
    text(d, (MARGIN, y + 12), "音量 + / - 选择 · 电源键确认", F_SMALL, MUTED)
    text(d, (W * 0.57, y + 12), status, F_SMALL, TEXT, anchor="ma")
    text(d, (W - MARGIN, y + 12), "sheng · SM8550", F_SMALL, MUTED, anchor="ra")


def page_environment():
    img = gradient(); d = ImageDraw.Draw(img)
    header(img, d); sidebar(img, d, 0); content_panel(d)
    section(d, "启动环境")
    x = CONTENT_X + PAD
    y = HEADER + PAD + 62
    colw = (CONTENT_W - 2 * PAD) // 2
    items = [("当前槽位", "A"), ("默认启动", "Android"),
             ("自动启动", "8 秒"), ("上次启动", "Linux")]
    for i, (label, value) in enumerate(items):
        cx = x + (i % 2) * colw
        cy = y + (i // 2) * 120
        text(d, (cx, cy), label, F_BODY, MUTED)
        text(d, (cx, cy + 27), value, ImageFont.truetype(FONT, 28), TEXT)
    footer(d, "8 秒后自动启动 Android")
    return img


def page_advanced():
    img = gradient(); d = ImageDraw.Draw(img)
    header(img, d); sidebar(img, d, 2); content_panel(d)
    section(d, "高级启动模式")
    y = HEADER + PAD + 62
    for title, desc, name in [
        ("Fastboot 模式", "进入 Fastboot，通过 USB 连接电脑进行维护或刷写。", "bolt"),
        ("Recovery 恢复模式", "进入 Android 恢复模式，进行系统维护。", "rotate"),
        ("9008 下载模式", "进入高通 9008 下载模式，供电脑端工具恢复或刷写。", "download"),
    ]:
        card(img, d, y, 75, title, desc, name=name)
        y += 85
    text(d, (CONTENT_X + PAD, y + 3), "高级启动模式只对本次开机有效；部分模式需要连接电脑或按键组合。",
         F_BODY, MUTED)
    back(d, y + 48)
    footer(d, "已取消自动启动")
    return img


def page_settings():
    img = gradient(); d = ImageDraw.Draw(img)
    header(img, d); sidebar(img, d, 3); content_panel(d)
    section(d, "设置")
    y = HEADER + PAD + 62
    card(img, d, y, 75, "语言 / Language", "选择菜单显示语言", "简体中文", name="globe")
    y += 85
    card(img, d, y, 75, "开机延迟", "自动启动前等待的时间", "8 秒", name="clock")
    text(d, (CONTENT_X + PAD, y + 92), "语言和开机延迟会永久保留，下次开机仍然生效。", F_BODY, MUTED)
    back(d, y + 132)
    footer(d, "已取消自动启动")
    return img


def page_language():
    img = gradient(); d = ImageDraw.Draw(img)
    header(img, d); sidebar(img, d, 3); content_panel(d)
    section(d, "语言 / Language")
    y = HEADER + PAD + 62
    gap = 20
    cw = (CONTENT_W - 2 * PAD - gap) // 2
    for i, (title, sub, checked) in enumerate([
        ("简体中文", "Chinese (Simplified)", True),
        ("English", "English", False),
    ]):
        x = CONTENT_X + PAD + i * (cw + gap)
        d.rounded_rectangle([x, y, x + cw, y + 95], radius=7, fill=CARD,
                            outline=BLUE if checked else BORDER, width=2 if checked else 1)
        text(d, (x + 20, y + 16), title, F_CARD_TITLE, TEXT)
        text(d, (x + 20, y + 52), sub, F_BODY, MUTED)
        if checked:
            bx, by = x + cw - 42, y + 14
            d.ellipse([bx, by, bx + 24, by + 24], fill=BLUE)
            d.line([(bx + 6, by + 12), (bx + 11, by + 17), (bx + 19, by + 7)],
                   fill=TEXT, width=3)
    text(d, (CONTENT_X + PAD, y + 120), "切换后菜单会立即使用新语言，并保存到本次启动记录。",
         F_BODY, MUTED)
    back(d, y + 160)
    footer(d, "已取消自动启动")
    return img


def page_delay():
    img = gradient(); d = ImageDraw.Draw(img)
    header(img, d); sidebar(img, d, 3); content_panel(d)
    section(d, "开机延迟")
    y = HEADER + PAD + 62
    gap = 12
    bw = (CONTENT_W - 2 * PAD - 2 * gap) // 3
    vals = ["不自动启动", "3 秒", "5 秒", "8 秒", "10 秒", "15 秒", "30 秒"]
    for i, v in enumerate(vals):
        x = CONTENT_X + PAD + (i % 3) * (bw + gap)
        yy = y + (i // 3) * 67
        sel = v == "8 秒"
        d.rounded_rectangle([x, yy, x + bw, yy + 55], radius=6,
                            fill=BLUE_DIM if sel else CARD,
                            outline=BLUE if sel else BORDER, width=2 if sel else 1)
        text(d, (x + bw // 2, yy + 27), v, F_NAV, TEXT if sel else MUTED, anchor="mm")
    text(d, (CONTENT_X + PAD, y + 218), "设为「不自动启动」后，菜单会一直等待按键选择；其他时长从菜单出现时开始倒计时。",
         F_BODY, MUTED)
    back(d, y + 266)
    footer(d, "已取消自动启动")
    return img


def page_info():
    img = gradient(); d = ImageDraw.Draw(img)
    header(img, d); sidebar(img, d, 4); content_panel(d)
    section(d, "设备信息", badge="只读")
    x = CONTENT_X + PAD
    y = HEADER + PAD + 62
    colw = (CONTENT_W - 2 * PAD) // 2
    fields = [
        ("机型", "Xiaomi Pad 6S Pro"),
        ("SoC", "Qualcomm SM8550"),
        ("DT 型号", "Xiaomi Pad 6S Pro (sheng)"),
        ("DT 报告内存", "12 GB"),
        ("DT 兼容列表", "xiaomi,sheng · qcom,sm8550"),
        ("帧缓冲", "3048 × 2032 · 32 bpp"),
        ("kShimC 版本", "v0.0.4"),
    ]
    for i, (label, value) in enumerate(fields):
        cx = x + (i % 2) * colw
        cy = y + (i // 2) * 86
        text(d, (cx, cy), label, F_BODY, MUTED)
        vf = ImageFont.truetype(FONT, 20) if len(value) > 22 else F_VALUE
        for j, line in enumerate(wrap(d, value, vf, colw - 24)[:2]):
            text(d, (cx, cy + 20 + j * 24), line, vf, TEXT)
    text(d, (x, y + 330), "信息来自设备树与运行时探测，仅供查看。", F_BODY, MUTED)
    back(d, y + 372)
    footer(d, "已取消自动启动")
    return img


def charger():
    img = Image.new("RGB", (W, H), (0, 0, 0))
    d = ImageDraw.Draw(img)
    text(d, (W // 2, H // 2 - 52), "已连接电源线", ImageFont.truetype(FONT, 34), TEXT, anchor="mm")
    text(d, (W // 2, H // 2 + 8), "长按电源开机", ImageFont.truetype(FONT, 34), TEXT, anchor="mm")
    text(d, (W // 2, H // 2 + 94), "kShimC v0.0.4", F_SMALL, MUTED, anchor="mm")
    return img


def overview(pages):
    gap = 22
    pw, ph = 1000, 667
    cols, rows = 3, 2
    sheet = Image.new("RGB", (cols * pw + (cols + 1) * gap,
                              rows * ph + (rows + 1) * gap), (3, 6, 12))
    for i, p in enumerate(pages):
        thumb = p.resize((pw, ph), Image.LANCZOS)
        x = gap + (i % cols) * (pw + gap)
        y = gap + (i // cols) * (ph + gap)
        sheet.paste(thumb, (x, y))
    return sheet


def main():
    pages = [page_environment(), page_advanced(), page_settings(),
             page_language(), page_delay(), page_info()]
    names = ["main", "advanced", "settings", "language", "delay", "info"]
    (OUT / "pages").mkdir(exist_ok=True)
    (ROOT / "docs" / "images").mkdir(exist_ok=True)
    for name, p in zip(names, pages):
        p.save(OUT / "pages" / f"{name}.png", optimize=True)
        p.save(ROOT / "docs" / "images" / f"menu-{name}.png", optimize=True)
    overview(pages).save(OUT / "pages-overview.png", optimize=True)
    charger().save(ROOT / "docs" / "images" / "menu-charger.png", optimize=True)
    print("wrote", OUT / "pages-overview.png")


if __name__ == "__main__":
    main()
