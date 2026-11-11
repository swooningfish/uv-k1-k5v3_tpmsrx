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
BG = (228, 224, 236)
FG = (28, 20, 48)
SCALE = 4


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


def inverse(fb, s, x, line, end_x, status):
    y0 = line * 8 if status else 8 + line * 8
    fill(fb, x, y0, end_x, y0 + 8, 1)
    text(fb, x, y0 + 1, s, 0)


def battery(fb):
    # Percentage sits in the 48 px reserved beside the icon.
    text(fb, 88, 1, "96%")
    fill(fb, 112, 1, 125, 6, 1)
    fill(fb, 113, 2, 116, 5, 0)
    fill(fb, 125, 2, 127, 5, 1)


def status(fb, title, band):
    inverse(fb, title, 2, 0, 2 + len(title) * 4, True)
    width = len(band) * 4
    text(fb, 128 - 48 - width, 1, band)
    battery(fb)


def center(fb, y, s):
    text(fb, max(0, (W - len(s) * 4) // 2), y, s)


def scene_waiting():
    fb = new_fb()
    status(fb, "TPMS", "433.92 FSK")
    center(fb, 8 + 10, "LISTENING")
    center(fb, 8 + 22, "FSK: FORD|TOYOTA")
    center(fb, 8 + 30, "OOK: SCHRADER")
    center(fb, 8 + 38, "UK/EU")
    label = "MENU"
    inverse(fb, label, 2, 6, 2 + len(label) * 4, False)
    return fb


def scene_menu():
    fb = new_fb()
    status(fb, "MENU", "433.92 FSK")
    rows = [
        (">BAND 433.92", 100),
        (" MODE AUTO", 78),
        (" CLEAR", 78),
    ]
    for i, (s, end) in enumerate(rows):
        inverse(fb, s, 2, i + 1, end, False)
    text(fb, 2, 8 + 41, "UP/DN SELECT")
    text(fb, 2, 8 + 47, "MENU CHANGE  EXIT BACK")
    return fb


def scene_list():
    fb = new_fb()
    status(fb, "TPMS", "433.92 FSK")
    rows = (
        "FORD A1B2C3D4 36psi 18C -67 4s",
        "TOYT 00F211A9 28psi 1C -72 12s",
        "SCHR 003A38B2 48psi 12C -81 2m",
    )
    for i, s in enumerate(rows):
        text(fb, 0, 8 + 8 + i * 6, s)
    return fb


def write_png(path, fb):
    sw, sh = W * SCALE, H * SCALE
    raw = bytearray()
    for y in range(sh):
        raw.append(0)
        sy = y // SCALE
        for x in range(sw):
            ink = fb[sy * W + (x // SCALE)]
            raw.extend(FG if ink else BG)
    def chunk(tag, data):
        return struct.pack(">I", len(data)) + tag + data + struct.pack(">I", zlib.crc32(tag + data) & 0xFFFFFFFF)
    png = b"\x89PNG\r\n\x1a\n"
    png += chunk(b"IHDR", struct.pack(">IIBBBBB", sw, sh, 8, 2, 0, 0, 0))
    png += chunk(b"IDAT", zlib.compress(bytes(raw), 9))
    png += chunk(b"IEND", b"")
    with open(path, "wb") as f:
        f.write(png)


def main():
    root = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
    docs = os.path.join(root, "docs")
    os.makedirs(docs, exist_ok=True)
    write_png(os.path.join(docs, "main_screen.png"), scene_waiting())
    write_png(os.path.join(docs, "menu.png"), scene_menu())
    write_png(os.path.join(docs, "list.png"), scene_list())
    print("wrote docs/main_screen.png docs/menu.png docs/list.png")


if __name__ == "__main__":
    main()
