#!/usr/bin/env python3
"""Draw the TPMS RX screens into docs/.

The 3x5 glyphs are gFont3x5 from App/font.c of
armel/uv-k1-k5v3-firmware-custom (Apache-2.0, Copyright 2023 Dual Tachyon).
GUI_DisplaySmallest walks three columns, LSB at the top, and advances 4 px.
"""
import os
import struct
import zlib

# ASCII 32..127. Index is ord(c) - 32. Column byte, low bit is the top pixel.
FONT = (
    (0x00, 0x00, 0x00), (0x00, 0x17, 0x00), (0x03, 0x00, 0x03), (0x1F, 0x0A, 0x1F),
    (0x0A, 0x1F, 0x05), (0x19, 0x04, 0x13), (0x0F, 0x17, 0x1C), (0x00, 0x03, 0x00),
    (0x00, 0x0E, 0x11), (0x11, 0x0E, 0x00), (0x05, 0x02, 0x05), (0x04, 0x0E, 0x04),
    (0x10, 0x08, 0x00), (0x04, 0x04, 0x04), (0x00, 0x10, 0x00), (0x18, 0x04, 0x03),
    (0x1E, 0x11, 0x0F), (0x02, 0x1F, 0x00), (0x19, 0x15, 0x12), (0x11, 0x15, 0x0A),
    (0x07, 0x04, 0x1F), (0x17, 0x15, 0x09), (0x1E, 0x15, 0x1D), (0x19, 0x05, 0x03),
    (0x1F, 0x15, 0x1F), (0x17, 0x15, 0x0F), (0x00, 0x0A, 0x00), (0x10, 0x0A, 0x00),
    (0x04, 0x0A, 0x11), (0x0A, 0x0A, 0x0A), (0x11, 0x0A, 0x04), (0x01, 0x15, 0x03),
    (0x0E, 0x15, 0x16), (0x1E, 0x05, 0x1E), (0x1F, 0x15, 0x0A), (0x0E, 0x11, 0x11),
    (0x1F, 0x11, 0x0E), (0x1F, 0x15, 0x15), (0x1F, 0x05, 0x05), (0x0E, 0x15, 0x1D),
    (0x1F, 0x04, 0x1F), (0x11, 0x1F, 0x11), (0x08, 0x10, 0x0F), (0x1F, 0x04, 0x1B),
    (0x1F, 0x10, 0x10), (0x1F, 0x06, 0x1F), (0x1F, 0x0E, 0x1F), (0x0E, 0x11, 0x0E),
    (0x1F, 0x05, 0x02), (0x0E, 0x19, 0x1E), (0x1F, 0x0D, 0x16), (0x12, 0x15, 0x09),
    (0x01, 0x1F, 0x01), (0x0F, 0x10, 0x1F), (0x07, 0x18, 0x07), (0x1F, 0x0C, 0x1F),
    (0x1B, 0x04, 0x1B), (0x03, 0x1C, 0x03), (0x19, 0x15, 0x13), (0x1F, 0x11, 0x11),
    (0x02, 0x04, 0x08), (0x11, 0x11, 0x1F), (0x02, 0x01, 0x02), (0x10, 0x10, 0x10),
    (0x01, 0x02, 0x00), (0x1A, 0x16, 0x1C), (0x1F, 0x12, 0x0C), (0x0C, 0x12, 0x12),
    (0x0C, 0x12, 0x1F), (0x0C, 0x1A, 0x16), (0x04, 0x1E, 0x05), (0x0C, 0x2A, 0x1E),
    (0x1F, 0x02, 0x1C), (0x00, 0x1D, 0x00), (0x10, 0x20, 0x1D), (0x1F, 0x0C, 0x12),
    (0x11, 0x1F, 0x10), (0x1E, 0x0E, 0x1E), (0x1E, 0x02, 0x1C), (0x0C, 0x12, 0x0C),
    (0x3E, 0x12, 0x0C), (0x0C, 0x12, 0x3E), (0x1C, 0x02, 0x02), (0x14, 0x1E, 0x0A),
    (0x02, 0x1F, 0x12), (0x0E, 0x10, 0x1E), (0x0E, 0x18, 0x0E), (0x1E, 0x1C, 0x1E),
    (0x12, 0x0C, 0x12), (0x06, 0x28, 0x1E), (0x1A, 0x1E, 0x16), (0x04, 0x1B, 0x11),
    (0x00, 0x1B, 0x00), (0x11, 0x1B, 0x04), (0x02, 0x03, 0x01), (0x12, 0x17, 0x12),
)

W, H = 128, 64
# Match K5Viewer / handset captures (lavender LCD), hard 4×4 blocks.
# Temp 433 docs use green-grey; Greg's UK/EU shots are Arduco lavender.
BG = (228, 223, 235)
FG = (41, 32, 62)
SCALE = 4

# App | status band+mode | listening headline | car lines | free-row hints | menu rows
VARIANTS = {
    "eu": {
        "app": "TPMS EU",
        "status": "433.92 FSK",
        "listen": "LISTENING 433.92 UK/EU",
        "cars": ("FSK: FORD|TOYOTA", "OOK: SCHRADER"),
        "hints": "2 BAND  * MODE  0 LIGHT",
        "menu": [(">BAND 433.92", 100), (" MODE FSK", 78), (" CLEAR", 78)],
        "menu_status": "433.92 FSK",
    },
    "na": {
        "app": "TPMS NA",
        "status": "315.00 FSK",
        "listen": "LISTENING 315.00 US/CA",
        "cars": ("FSK: FORD|TOYOTA", "OOK: SCHRADER"),
        "hints": "2 BAND  * MODE  0 LIGHT",
        "menu": [(">BAND 315.00", 100), (" MODE FSK", 78), (" CLEAR", 78)],
        "menu_status": "315.00 FSK",
    },
    "eu2": {
        "app": "TPMS EU2",
        "status": "433.92 OOK",
        "listen": "LISTENING 433.92 UK/EU",
        "cars": ("OOK: VAUXHALL|OPEL|SAAB", "OOK: BMW|MINI"),
        "hints": "0 LIGHT",
        "menu": [(">CLEAR", 100)],
        "menu_status": "433.92 OOK",
    },
    "na2": {
        "app": "TPMS NA2",
        "status": "315.00 OOK",
        "listen": "LISTENING 315.00 US/CA",
        "cars": ("OOK: CHEVROLET|GM", "OOK: SUBARU|NISSAN"),
        "hints": "2 BAND  0 LIGHT",
        "menu": [(">BAND 315.00", 100), (" CLEAR", 78)],
        "menu_status": "315.00 OOK",
    },
}


def new_fb():
    return [0] * (W * H)


def px(fb, x, y, ink):
    if 0 <= x < W and 0 <= y < H:
        fb[y * W + x] = 1 if ink else 0


def glyph(fb, x, y, ch, ink):
    i = ord(ch) - 32
    if i < 0 or i >= len(FONT):
        return
    cols = FONT[i]
    for col in range(3):
        bits = cols[col]
        for row in range(6):
            if bits & (1 << row):
                px(fb, x + col, y + row, ink)


def text(fb, x, y, s, ink=1):
    for ch in s:
        glyph(fb, x, y, ch, ink)
        x += 4
    return x


def fill(fb, x0, y0, x1, y1, ink):
    for y in range(y0, y1):
        for x in range(x0, x1):
            px(fb, x, y, ink)


def inverse(fb, s, x, line, end_x, status_bar):
    y0 = line * 8 if status_bar else 8 + line * 8
    fill(fb, x, y0, end_x, y0 + 8, 1)
    text(fb, x, y0 + 1, s, 0)


# Status-line battery: F4HWN BITMAP_BatteryLevel1 (16×7, 3 bars, tip on the right).
# Percent uses gFontSmall via UI_PrintStringSmallBufferNormal (not gFont3x5):
# 6 px glyph + 1 px advance = 7, LSB = top of the status page — same as the radio.
_BATTERY = (
    0b0011111111111111,
    0b1100000000000001,
    0b1000001101101101,
    0b1000001101101101,
    0b1000001101101101,
    0b1100000000000001,
    0b0011111111111111,
)

# gFontSmall digits + '%' only (App/font.c). Index is ASCII; 6 columns, bits 0..6.
_FONT_SMALL = {
    "%": (0x63, 0x13, 0x08, 0x04, 0x62, 0x61),
    "0": (0x3E, 0x41, 0x41, 0x41, 0x41, 0x3E),
    "1": (0x00, 0x40, 0x42, 0x7F, 0x40, 0x40),
    "2": (0x62, 0x51, 0x51, 0x49, 0x49, 0x46),
    "3": (0x22, 0x41, 0x49, 0x49, 0x49, 0x36),
    "4": (0x18, 0x14, 0x12, 0x11, 0x7F, 0x10),
    "5": (0x27, 0x45, 0x45, 0x45, 0x45, 0x39),
    "6": (0x3E, 0x49, 0x49, 0x49, 0x49, 0x32),
    "7": (0x01, 0x01, 0x71, 0x09, 0x05, 0x03),
    "8": (0x36, 0x49, 0x49, 0x49, 0x49, 0x36),
    "9": (0x46, 0x49, 0x49, 0x49, 0x29, 0x1E),
}


def text_small(fb, x, s):
    """UI_PrintStringSmallBufferNormal: 7 px/char, glyph columns at x+1..x+6."""
    for i, ch in enumerate(s):
        cols = _FONT_SMALL.get(ch)
        if cols is None:
            continue
        base = x + i * 7 + 1
        for c, bits in enumerate(cols):
            for row in range(7):
                if bits & (1 << row):
                    px(fb, base + c, row, 1)


def battery(fb, pct="78%"):
    # Icon flush right (x=112..127), matching ST7565_DrawLine(LCD_WIDTH - 16, …).
    for y, bits in enumerate(_BATTERY):
        for i in range(16):
            if bits & (1 << (15 - i)):
                px(fb, 112 + i, y, 1)
    # Percent left of the icon — F4HWN UI_DrawStatusBattery (7 * strlen).
    text_small(fb, 112 - 7 * len(pct), pct)


def status(fb, title, band):
    inverse(fb, title, 2, 0, 2 + len(title) * 4, True)
    width = len(band) * 4
    text(fb, 128 - 48 - width, 1, band)
    battery(fb)


def center(fb, y, s):
    text(fb, max(0, (W - len(s) * 4) // 2), y, s)


def hint_strip(fb, s):
    """Right-aligned on the MENU capsule baseline (print_inverse line 6 → y=57)."""
    w = len(s) * 4
    x = 2 if w >= 126 else 126 - w
    text(fb, x, 57, s)


def scene_waiting(v):
    fb = new_fb()
    status(fb, "TPMS", v["status"])
    center(fb, 8, v["listen"])
    center(fb, 16, v["cars"][0])
    if v["cars"][1]:
        center(fb, 24, v["cars"][1])
    label = "MENU"
    inverse(fb, label, 2, 6, 2 + len(label) * 4, False)
    hint_strip(fb, v["hints"])
    return fb


def scene_menu(v):
    fb = new_fb()
    status(fb, "MENU", v["menu_status"])
    for i, (s, end) in enumerate(v["menu"]):
        inverse(fb, s, 2, i + 1, end, False)
    text(fb, 2, 8 + 41, "UP/DN SELECT")
    text(fb, 2, 8 + 47, "MENU CHANGE")
    exit_s = "EXIT BACK"
    text(fb, 126 - len(exit_s) * 4, 8 + 47, exit_s)
    return fb


def scene_list():
    """EU list with three sensors; key strip on the MENU baseline."""
    fb = new_fb()
    status(fb, "TPMS", "433.92 FSK")
    rows = (
        "FORD A1B2C3D4 36psi 18C -67 4s",
        "TOYT 00F211A9 28psi 1C -72 12s",
        "SCHR 003A38B2 48psi 12C -81 2m",
    )
    for i, s in enumerate(rows):
        text(fb, 0, 8 + i * 6, s)
    label = "MENU"
    inverse(fb, label, 2, 6, 2 + len(label) * 4, False)
    hint_strip(fb, "2 BAND  * MODE  0 LIGHT")
    return fb


def write_png(path, fb):
    """Upscale 1 LCD pixel → SCALE×SCALE solid block.

    Indexed 2-colour PNG (UV Studio lavender palette): every output byte is 0 or 1,
    so viewers cannot invent grey anti-alias between ink and background.
    """
    sw, sh = W * SCALE, H * SCALE
    # Filter None + one index byte per pixel.
    raw = bytearray()
    for y in range(sh):
        raw.append(0)
        sy = y // SCALE
        row = fb[sy * W : (sy + 1) * W]
        for x in range(sw):
            raw.append(1 if row[x // SCALE] else 0)

    def chunk(tag, data):
        return struct.pack(">I", len(data)) + tag + data + struct.pack(
            ">I", zlib.crc32(tag + data) & 0xFFFFFFFF
        )

    # PLTE: index 0 = BG, 1 = FG (RGB triples).
    plte = bytes(BG) + bytes(FG)
    png = b"\x89PNG\r\n\x1a\n"
    png += chunk(b"IHDR", struct.pack(">IIBBBBB", sw, sh, 8, 3, 0, 0, 0))
    png += chunk(b"PLTE", plte)
    png += chunk(b"IDAT", zlib.compress(bytes(raw), 9))
    png += chunk(b"IEND", b"")
    with open(path, "wb") as f:
        f.write(png)


def main():
    root = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
    docs = os.path.join(root, "docs")
    variants = os.path.join(docs, "variants")
    os.makedirs(variants, exist_ok=True)

    eu = VARIANTS["eu"]
    write_png(os.path.join(docs, "main_screen.png"), scene_waiting(eu))
    write_png(os.path.join(docs, "menu.png"), scene_menu(eu))
    write_png(os.path.join(docs, "list.png"), scene_list())

    written = ["docs/main_screen.png", "docs/menu.png", "docs/list.png"]
    for key, v in VARIANTS.items():
        w = f"docs/variants/{key}_waiting.png"
        m = f"docs/variants/{key}_menu.png"
        write_png(os.path.join(root, w), scene_waiting(v))
        write_png(os.path.join(root, m), scene_menu(v))
        written.extend([w, m])
    print("wrote " + " ".join(written))


if __name__ == "__main__":
    main()
