#!/usr/bin/env python3
# Uses app_assets.py of the F4HWN app SDK (folder in $SDK, set by build.sh).
import os
import sys

sys.dont_write_bytecode = True
sys.path.insert(0, os.environ.get("SDK", "sdk"))
from app_assets import Assets

TITLE = "TPMS"
UI = [
    ("T_TITLE", TITLE),
    ("T_FSK", "FSK"),
    ("T_OOK", "OOK"),
    ("T_AUTO", "AUTO"),
    ("T_AF", "A:F"),
    ("T_AO", "A:O"),
    ("T_WAIT", "LISTENING"),
    ("T_PSI", "psi"),
    ("T_C", "C"),
    *{
        "eu": [
            ("T_FORD", "FORD"), ("T_TOYT", "TOYT"), ("T_SCHR", "SCHR"),
            ("T_REGION", "UK/EU"),
            ("T_CARS1", "FSK: FORD|TOYOTA"),
            ("T_CARS2", "OOK: SCHRADER"),
        ],
        "na": [
            ("T_FORD", "FORD"), ("T_TOYT", "TOYT"), ("T_SCHR", "SCHR"),
            ("T_REGION", "US/CA"),
            ("T_CARS1", "FSK: FORD|TOYOTA"),
            ("T_CARS2", "OOK: SCHRADER"),
        ],
        "eu2": [
            ("T_EG53", "EG53"), ("T_BMW", "BMW"),
            ("T_REGION", "UK/EU"),
            ("T_CARS1", "OOK: VAUXHALL|OPEL|SAAB"),
            ("T_CARS2", "OOK: BMW|MINI"),
        ],
        "na2": [
            ("T_EG53", "EG53"), ("T_NISS", "NISS"),
            ("T_REGION", "US/CA"),
            ("T_CARS1", "OOK: CHEVROLET|GM"),
            ("T_CARS2", "OOK: SUBARU|NISSAN"),
        ],
    }[os.environ.get("FLAVOUR", "eu")],
    ("T_MENU", "MENU"),
    ("T_BAND", "BAND"),
    ("T_MODE", "MODE"),
    ("T_CLR", "CLEAR"),
    ("T_HELP1", "UP/DN SELECT"),
    ("T_HELP2", "MENU CHANGE  EXIT BACK"),
    # Free-row shortcuts on the main list (0–3 sensors). Omit locked keys.
    *{
        "eu": [("T_HINT1", "2 BAND  * MODE"), ("T_HINT2", "0 LIGHT  MENU")],
        "na": [("T_HINT1", "2 BAND  * MODE"), ("T_HINT2", "0 LIGHT  MENU")],
        "eu2": [("T_HINT1", "0 LIGHT"), ("T_HINT2", "MENU")],
        "na2": [("T_HINT1", "2 BAND"), ("T_HINT2", "0 LIGHT  MENU")],
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
# EU2: CLEAR only (fixed 433.92 OOK). NA2: BAND+CLEAR (OOK locked, band free).
# EU/NA: BAND+MODE+CLEAR.
_flavour = os.environ.get("FLAVOUR", "eu")
a.const("MENU_ITEMS", {"eu2": 1, "na2": 2}.get(_flavour, 3))
a.main()
