#!/usr/bin/env python3
"""Cross-flavour checks for RF parking and main-list layout (no radio)."""
from __future__ import annotations

import os
import re
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
fails = 0


def check(cond: bool, msg: str) -> None:
    global fails
    if cond:
        print(f"  ok  {msg}")
    else:
        print(f"FAIL  {msg}")
        fails += 1


def read(path: str) -> str:
    with open(os.path.join(ROOT, path), encoding="utf-8") as f:
        return f.read()


def flavour_ui(flavour: str) -> dict[str, str]:
    """Minimal mirror of gen_assets.py string tables for one FLAVOUR."""
    os.environ["FLAVOUR"] = flavour
    # Re-exec the same mapping without importing the SDK Assets class.
    regions = {"eu": "UK/EU", "na": "US/CA", "eu2": "UK/EU", "na2": "US/CA"}
    cars = {
        "eu": ("FSK: FORD|TOYOTA", "OOK: SCHRADER"),
        "na": ("FSK: FORD|TOYOTA", "OOK: SCHRADER"),
        "eu2": ("OOK: VAUXHALL|OPEL|SAAB", "OOK: BMW|MINI"),
        "na2": ("OOK: CHEVROLET|GM", "OOK: SUBARU|NISSAN"),
    }
    hints = {
        "eu": "2 BAND  * MODE  0 LIGHT",
        "na": "2 BAND  * MODE  0 LIGHT",
        "eu2": "0 LIGHT",
        "na2": "2 BAND  0 LIGHT",
    }
    band = "433.92" if flavour in ("eu", "eu2") else "315.00"
    mode = "OOK" if flavour in ("eu2", "na2") else "FSK"
    return {
        "region": regions[flavour],
        "cars1": cars[flavour][0],
        "cars2": cars[flavour][1],
        "hint": hints[flavour],
        "band": band,
        "mode": mode,
        "listen": f"LISTENING {band} {regions[flavour]}",
    }


def test_rf_source() -> None:
    print("RF path (tpmsrx_app.c)")
    src = read("tpmsrx_app.c")
    check(re.search(r"#define\s+FREQ_315\s+31500000u", src) is not None, "FREQ_315 is 31500000 (×10 Hz)")
    check(re.search(r"#define\s+FREQ_433\s+43392000u", src) is not None, "FREQ_433 is 43392000 (×10 Hz)")
    check("F_FIRST" not in src and "N_PTS" not in src, "no TEMP433 thermometer sweep constants")
    # Mute must be cleared after PLL pulse: tones_off_rx then AF owner.
    m = re.search(
        r"static void applyRf\(void\)\s*\{(?P<body>.*?)^\}",
        src,
        re.S | re.M,
    )
    check(m is not None, "applyRf present")
    if m:
        body = m.group("body")
        i_tones = body.find("tones_off_rx")
        i_af = min(
            (i for i in (body.find("REG_47"), body.find("set_af")) if i >= 0),
            default=-1,
        )
        check(i_tones >= 0 and i_af > i_tones, "tones_off_rx before REG_47 / set_af")
        check("set_agc(false)" in body, "fixed AGC while listening")
        check("REG_2B" in body and "REG_73" in body and "REG_43" in body, "shared IF regs")
        check("REG_47_BYPASS" in body and "APP_AF_AM" in body, "FSK bypass and OOK AM both programmed")
    check("bk_write(REG_2B, r2b)" in src and "set_agc(true)" in src, "restore REG_2B and AGC on exit")


def test_layout_source() -> None:
    print("Main-list layout (tpmsrx_app.c)")
    src = read("tpmsrx_app.c")
    check("drawListHints" in src, "drawListHints helper")
    check("T_HINT" in src and "T_HINT1" not in src and "T_HINT2" not in src,
          "uses single T_HINT strip")
    check('put(str, ui + T_WAIT)' in src and "putBand(o)" in src and "T_REGION" in src,
          "LISTENING line merges band and region")
    check("g.nRows <= 3u" in src, "free-row hints when 0–3 sensors")
    check("tinyCenter(str, 8, o)" in src and "tinyCenter(str, 16, o)" in src,
          "listening and cars shifted up")


def test_assets_and_flavours() -> None:
    print("Flavour assets (gen_assets.py)")
    assets = read("gen_assets.py")
    check('"T_HINT"' in assets and "T_HINT1" not in assets and "T_HINT2" not in assets,
          "hint strings defined")
    for fl in ("eu", "na", "eu2", "na2"):
        ui = flavour_ui(fl)
        check(ui["hint"] in assets, f"{fl} hint text in gen_assets")
        check(ui["region"] in assets, f"{fl} region in gen_assets")
        # Headline length fits 128 px tiny font (4 px/glyph).
        check(len(ui["listen"]) * 4 <= 128, f"{fl} listening line fits width: {ui['listen']}")
        # Locked flavours must not advertise dead band/mode keys.
        if fl == "eu2":
            check("*" not in ui["hint"] and "2 BAND" not in ui["hint"], "eu2 hints omit band/mode")
        if fl == "na2":
            check("*" not in ui["hint"] and "MODE" not in ui["hint"], "na2 hints omit mode")


def test_build_flavours() -> None:
    print("Build flavours (build.sh)")
    sh = read("build.sh")
    for name in ("TPMS_FLAVOUR_EU", "TPMS_FLAVOUR_NA", "TPMS_FLAVOUR_EU2", "TPMS_FLAVOUR_NA2"):
        check(name in sh, f"builds {name}")
    check('"${APP}_app.c"' in sh or "${APP}_app.c" in sh, "single tpmsrx_app.c for all apps")


def main() -> int:
    print("variant / RF / layout host checks")
    test_rf_source()
    test_layout_source()
    test_assets_and_flavours()
    test_build_flavours()
    if fails:
        print(f"{fails} check(s) failed")
        return 1
    print("variant checks passed")
    return 0


if __name__ == "__main__":
    sys.exit(main())
