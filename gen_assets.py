#!/usr/bin/env python3
# Uses app_assets.py of the F4HWN app SDK (folder in $SDK, set by build.sh).
import os
import sys

sys.dont_write_bytecode = True
sys.path.insert(0, os.environ.get("SDK", "sdk"))
from app_assets import Assets

TITLE = "TPMS"
# Version (APP_VER from build.sh) sits right aligned on the first menu help
# row. The row starts at x=2 with 4 px glyphs, so 31 cells end at x=126.
# Baking it into the string keeps the 4 KiB overlay free of drawing code.
HELP1 = "UP/DN SELECT"
VER = "V" + os.environ.get("APP_VER", "DEV")
HELP1_VER = HELP1 + " " * max(1, 31 - len(HELP1) - len(VER)) + VER
UI = [
    ("T_TITLE", TITLE),
    # One demod per app: EU / NA are FSK, EU2 / NA2 are OOK.
    ("T_DEMOD", "OOK" if os.environ.get("FLAVOUR", "eu") in ("eu2", "na2") else "FSK"),
    ("T_WAIT", "LISTENING"),
    ("T_PSI", "psi"),
    ("T_C", "C"),
    *{
        "eu": [
            ("T_FORD", "FORD"), ("T_TOYT", "TOYT"),
            ("T_REGION", "UK/EU"),
            ("T_CARS1", "FSK: FORD|TOYOTA"),
            ("T_CARS2", VER),
        ],
        "na": [
            ("T_FORD", "FORD"), ("T_TOYT", "TOYT"),
            ("T_REGION", "US/CA"),
            ("T_CARS1", "FSK: FORD|TOYOTA"),
            ("T_CARS2", VER),
        ],
        "eu2": [
            ("T_EG53", "EG53"), ("T_BMW", "BMW"), ("T_SCHR", "SCHR"),
            ("T_REGION", "UK/EU"),
            ("T_CARS1", "OOK: VAUXHALL|OPEL|SAAB"),
            ("T_CARS2", "OOK: BMW|MINI|SCHRADER"),
        ],
        "na2": [
            ("T_EG53", "EG53"), ("T_NISS", "NISS"), ("T_SCHR", "SCHR"),
            ("T_REGION", "US/CA"),
            ("T_CARS1", "OOK: CHEVROLET|GM|SCHRADER"),
            ("T_CARS2", "OOK: SUBARU|NISSAN"),
        ],
    }[os.environ.get("FLAVOUR", "eu")],
    ("T_MENU", "MENU"),
    ("T_BAND", "BAND"),
    ("T_CLR", "CLEAR"),
    ("T_HELP1", HELP1_VER),
    ("T_HELP2", "MENU CHANGE"),
    ("T_HELP3", "EXIT BACK"),
    # Listening-screen key strip (no MENU — that is the bottom-left capsule).
    # Omit band when the flavour locks it. No app switches demod.
    *{
        "eu": [("T_HINT", "2 BAND  0 LIGHT")],
        "na": [("T_HINT", "2 BAND  0 LIGHT")],
        "eu2": [("T_HINT", "0 LIGHT")],
        "na2": [("T_HINT", "0 LIGHT")],
    }[os.environ.get("FLAVOUR", "eu")],
]
a = Assets("TPMS_RX")
ui_size = 0
for name, value in UI:
    pad = -(len(value) + 1) % 4
    a.text(name, value + "\0" * pad)
    ui_size += len(value) + 1 + pad
a.const("UI_SIZE", ui_size)
a.const("T_TITLE_CHARS", len(TITLE))
# EU2 / NA2: CLEAR only (fixed band). EU / NA: BAND + CLEAR.
_flavour = os.environ.get("FLAVOUR", "eu")
a.const("MENU_ITEMS", {"eu2": 1, "na2": 1}.get(_flavour, 2))
a.main()
