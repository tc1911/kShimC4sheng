#!/usr/bin/env python3
"""Render the Atlas UI previews from the same layout/palette as menu_atlas.h.

The real screen is 3048x2032; pages are drawn at half resolution so they stay
readable in the README. Both the Simplified Chinese and English pages are
rendered; English files use the -en suffix.
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

TEXTS = {
    "zh": {
        "suffix": "",
        "title": "KShimC 启动器",
        "device": "Xiaomi Pad 6S Pro · sheng · v0.0.4",
        "lang_label": "简体中文",
        "nav": ["Android 原厂", "Linux", "高级启动模式", "设置", "设备信息", "重启", "关机"],
        "hint": "音量 + / - 选择 · 电源键确认",
        "status": "8 秒后自动启动 Android",
        "cancelled": "已取消自动启动",
        "back": "‹  返回主菜单",
        "env": "启动环境",
        "env_items": [("当前槽位", "A"), ("默认启动", "Android"),
                      ("自动启动", "8 秒"), ("上次启动", "Linux")],
        "advanced": "高级启动模式",
        "advanced_cards": [
            ("Fastboot 模式", "进入 Fastboot，通过 USB 连接电脑进行维护或刷写。", "bolt"),
            ("Recovery 恢复模式", "进入 Android 恢复模式，进行系统维护。", "rotate"),
            ("9008 下载模式", "进入高通 9008 下载模式，供电脑端工具恢复或刷写。", "download"),
        ],
        "advanced_note": "高级启动模式只对本次开机有效；部分模式需要连接电脑或按键组合。",
        "settings": "设置",
        "language_row": "语言 / Language",
        "language_desc": "选择菜单显示语言",
        "language_value": "简体中文",
        "delay_row": "开机延迟",
        "delay_desc": "自动启动前等待的时间",
        "delay_value": "8 秒",
        "settings_note": "语言和开机延迟会永久保留，下次开机仍然生效。",
        "language": "语言 / Language",
        "language_cards": [("简体中文", "Chinese (Simplified)", True),
                           ("English", "English", False)],
        "language_note": "切换后菜单会立即使用新语言，并保存到本次启动记录。",
        "delay": "开机延迟",
        "delay_values": ["不自动启动", "3 秒", "5 秒", "8 秒", "10 秒", "15 秒", "30 秒"],
        "delay_selected": "8 秒",
        "delay_note": "设为「不自动启动」后，菜单会一直等待按键选择；其他时长从菜单出现时开始倒计时。",
        "info": "设备信息",
        "readonly": "只读",
        "fields": [("机型", "Xiaomi Pad 6S Pro"), ("SoC", "Qualcomm SM8550"),
                   ("DT 型号", "Xiaomi Pad 6S Pro (sheng)"), ("DT 报告内存", "12 GB"),
                   ("DT 兼容列表", "xiaomi,sheng · qcom,sm8550"),
                   ("帧缓冲", "3048 × 2032 · 32 bpp"), ("kShimC 版本", "v0.0.4")],
        "info_note": "信息来自设备树与运行时探测，仅供查看。",
        "charger1": "已连接电源线",
        "charger2": "长按电源开机",
    },
    "en": {
        "suffix": "-en",
        "title": "KShimC Launcher",
        "device": "Xiaomi Pad 6S Pro · sheng · v0.0.4",
        "lang_label": "English",
        "nav": ["Android (Stock)", "Linux", "Advanced Boot", "Settings",
                "Device Info", "Restart", "Power Off"],
        "hint": "Volume keys to select, Power to confirm",
        "status": "Booting Android in 8 s",
        "cancelled": "Auto boot cancelled",
        "back": "‹  Back to Main Menu",
        "env": "Boot Environment",
        "env_items": [("Current slot", "A"), ("Default boot", "Android"),
                      ("Auto boot", "8 s"), ("Last boot", "Linux")],
        "advanced": "Advanced Boot",
        "advanced_cards": [
            ("Fastboot Mode", "Connect to a PC over USB for maintenance or flashing.", "bolt"),
            ("Recovery Mode", "Boot Android recovery for system maintenance.", "rotate"),
            ("9008 Download Mode", "Enter Qualcomm 9008 mode for PC-side recovery or flashing.", "download"),
        ],
        "advanced_note": "These modes apply to the current boot only; some need a PC or a key combination.",
        "settings": "Settings",
        "language_row": "Language",
        "language_desc": "Choose the menu language",
        "language_value": "English",
        "delay_row": "Boot Delay",
        "delay_desc": "Time to wait before auto boot",
        "delay_value": "8 s",
        "settings_note": "Language and boot delay are kept across reboots.",
        "language": "Language",
        "language_cards": [("English", "English", True),
                           ("简体中文", "Chinese (Simplified)", False)],
        "language_note": "The menu switches immediately and the choice is saved for this boot record.",
        "delay": "Boot Delay",
        "delay_values": ["No auto boot", "3 s", "5 s", "8 s", "10 s", "15 s", "30 s"],
        "delay_selected": "8 s",
        "delay_note": "With “No auto boot”, the menu waits for a key. Other values count down from menu appearance.",
        "info": "Device Info",
        "readonly": "READ-ONLY",
        "fields": [("Model", "Xiaomi Pad 6S Pro"), ("SoC", "Qualcomm SM8550"),
                   ("DT model", "Xiaomi Pad 6S Pro (sheng)"), ("DT memory", "12 GB"),
                   ("DT compatible", "xiaomi,sheng · qcom,sm8550"),
                   ("Framebuffer", "3048 × 2032 · 32 bpp"), ("kShimC version", "v0.0.4")],
        "info_note": "Read from the device tree and runtime probes; view only.",
        "charger1": "CHARGER CONNECTED",
        "charger2": "HOLD POWER TO BOOT",
    },
}


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


NAV_ICONS = ["android", "linux", "gear", "wrench", "info", "restart", "power"]


def header(d, T):
    text(d, (MARGIN, 16), T["title"], F_TITLE, TEXT)
    text(d, (MARGIN, 58), T["device"], F_SUB, MUTED)
    text(d, (W - MARGIN, 36), T["lang_label"], F_SUB, MUTED, anchor="ra")


def sidebar(img, d, T, selected):
    x, y = MARGIN, HEADER
    for i, (name, label) in enumerate(zip(NAV_ICONS, T["nav"])):
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
        bx = CONTENT_X + CONTENT_W - PAD - 58
        d.rounded_rectangle([bx, y, bx + 58, y + 24], radius=4, outline=BORDER, width=1)
        text(d, (bx + 29, y + 12), badge, F_BADGE, MUTED, anchor="mm")


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


def footer(d, T, status):
    y = H - FOOTER
    d.line([(MARGIN, y), (W - MARGIN, y)], fill=BORDER)
    text(d, (MARGIN, y + 12), T["hint"], F_SMALL, MUTED)
    text(d, (W * 0.57, y + 12), status, F_SMALL, TEXT, anchor="ma")
    text(d, (W - MARGIN, y + 12), "sheng · SM8550", F_SMALL, MUTED, anchor="ra")


def page_environment(T):
    img = gradient(); d = ImageDraw.Draw(img)
    header(d, T); sidebar(img, d, T, 0); content_panel(d)
    section(d, T["env"])
    x = CONTENT_X + PAD
    y = HEADER + PAD + 62
    colw = (CONTENT_W - 2 * PAD) // 2
    for i, (label, value) in enumerate(T["env_items"]):
        cx = x + (i % 2) * colw
        cy = y + (i // 2) * 120
        text(d, (cx, cy), label, F_BODY, MUTED)
        text(d, (cx, cy + 27), value, ImageFont.truetype(FONT, 28), TEXT)
    footer(d, T, T["status"])
    return img


def page_advanced(T):
    img = gradient(); d = ImageDraw.Draw(img)
    header(d, T); sidebar(img, d, T, 2); content_panel(d)
    section(d, T["advanced"])
    y = HEADER + PAD + 62
    for title, desc, name in T["advanced_cards"]:
        card(img, d, y, 75, title, desc, name=name)
        y += 85
    text(d, (CONTENT_X + PAD, y + 3), T["advanced_note"], F_BODY, MUTED)
    text(d, (CONTENT_X + PAD, y + 48), T["back"], F_NAV, TEXT)
    footer(d, T, T["cancelled"])
    return img


def page_settings(T):
    img = gradient(); d = ImageDraw.Draw(img)
    header(d, T); sidebar(img, d, T, 3); content_panel(d)
    section(d, T["settings"])
    y = HEADER + PAD + 62
    card(img, d, y, 75, T["language_row"], T["language_desc"], T["language_value"], name="globe")
    y += 85
    card(img, d, y, 75, T["delay_row"], T["delay_desc"], T["delay_value"], name="clock")
    text(d, (CONTENT_X + PAD, y + 92), T["settings_note"], F_BODY, MUTED)
    text(d, (CONTENT_X + PAD, y + 132), T["back"], F_NAV, TEXT)
    footer(d, T, T["cancelled"])
    return img


def page_language(T):
    img = gradient(); d = ImageDraw.Draw(img)
    header(d, T); sidebar(img, d, T, 3); content_panel(d)
    section(d, T["language"])
    y = HEADER + PAD + 62
    gap = 20
    cw = (CONTENT_W - 2 * PAD - gap) // 2
    for i, (title, sub, checked) in enumerate(T["language_cards"]):
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
    text(d, (CONTENT_X + PAD, y + 120), T["language_note"], F_BODY, MUTED)
    text(d, (CONTENT_X + PAD, y + 160), T["back"], F_NAV, TEXT)
    footer(d, T, T["cancelled"])
    return img


def page_delay(T):
    img = gradient(); d = ImageDraw.Draw(img)
    header(d, T); sidebar(img, d, T, 3); content_panel(d)
    section(d, T["delay"])
    y = HEADER + PAD + 62
    gap = 12
    bw = (CONTENT_W - 2 * PAD - 2 * gap) // 3
    for i, v in enumerate(T["delay_values"]):
        x = CONTENT_X + PAD + (i % 3) * (bw + gap)
        yy = y + (i // 3) * 67
        sel = v == T["delay_selected"]
        d.rounded_rectangle([x, yy, x + bw, yy + 55], radius=6,
                            fill=BLUE_DIM if sel else CARD,
                            outline=BLUE if sel else BORDER, width=2 if sel else 1)
        text(d, (x + bw // 2, yy + 27), v, F_NAV, TEXT if sel else MUTED, anchor="mm")
    text(d, (CONTENT_X + PAD, y + 218), T["delay_note"], F_BODY, MUTED)
    text(d, (CONTENT_X + PAD, y + 266), T["back"], F_NAV, TEXT)
    footer(d, T, T["cancelled"])
    return img


def page_info(T):
    img = gradient(); d = ImageDraw.Draw(img)
    header(d, T); sidebar(img, d, T, 4); content_panel(d)
    section(d, T["info"], badge=T["readonly"])
    x = CONTENT_X + PAD
    y = HEADER + PAD + 62
    colw = (CONTENT_W - 2 * PAD) // 2
    for i, (label, value) in enumerate(T["fields"]):
        cx = x + (i % 2) * colw
        cy = y + (i // 2) * 86
        text(d, (cx, cy), label, F_BODY, MUTED)
        vf = ImageFont.truetype(FONT, 20) if len(value) > 22 else F_VALUE
        for j, line in enumerate(wrap(d, value, vf, colw - 24)[:2]):
            text(d, (cx, cy + 20 + j * 24), line, vf, TEXT)
    text(d, (x, y + 330), T["info_note"], F_BODY, MUTED)
    text(d, (x, y + 372), T["back"], F_NAV, TEXT)
    footer(d, T, T["cancelled"])
    return img


def charger(T):
    img = Image.new("RGB", (W, H), (0, 0, 0))
    d = ImageDraw.Draw(img)
    text(d, (W // 2, H // 2 - 52), T["charger1"], ImageFont.truetype(FONT, 34), TEXT, anchor="mm")
    text(d, (W // 2, H // 2 + 8), T["charger2"], ImageFont.truetype(FONT, 34), TEXT, anchor="mm")
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
    names = ["main", "advanced", "settings", "language", "delay", "info"]
    for lang, T in TEXTS.items():
        suffix = T["suffix"]
        pages = [page_environment(T), page_advanced(T), page_settings(T),
                 page_language(T), page_delay(T), page_info(T)]
        page_dir = OUT / f"pages{suffix}"
        page_dir.mkdir(exist_ok=True)
        (ROOT / "docs" / "images").mkdir(parents=True, exist_ok=True)
        for name, p in zip(names, pages):
            p.save(page_dir / f"{name}.png", optimize=True)
            p.save(ROOT / "docs" / "images" / f"menu-{name}{suffix}.png", optimize=True)
        overview(pages).save(OUT / f"pages-overview{suffix}.png", optimize=True)
        charger(T).save(ROOT / "docs" / "images" / f"menu-charger{suffix}.png", optimize=True)
        print("wrote", OUT / f"pages-overview{suffix}.png")


if __name__ == "__main__":
    main()
