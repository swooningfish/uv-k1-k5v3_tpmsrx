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
    ("T_WAIT", "WAITING..."),
    ("T_PSI", "psi"),
    ("T_C", "C"),
    *{
        "eu": [("T_FORD", "FORD"), ("T_TOYT", "TOYT"), ("T_SCHR", "SCHR")],
        "na": [("T_FORD", "FORD"), ("T_TOYT", "TOYT"), ("T_SCHR", "SCHR")],
        "eu2": [("T_EG53", "EG53"), ("T_BMW", "BMW")],
        "na2": [("T_EG53", "EG53"), ("T_NISS", "NISS")],
    }[os.environ.get("FLAVOUR", "eu")],
    ("T_MENU", "MENU"),
    ("T_BAND", "BAND"),
    ("T_MODE", "MODE"),
    ("T_CLR", "CLEAR"),
    ("T_HELP1", "UP/DN SELECT"),
    ("T_HELP2", "MENU CHANGE  EXIT BACK"),
]
a = Assets("TPMS_RX")
ui_size = 0
for name, value in UI:
    pad = -(len(value) + 1) % 4
    a.text(name, value + "\0" * pad)
    ui_size += len(value) + 1 + pad
a.const("UI_SIZE", ui_size)
a.const("T_TITLE_CHARS", len(TITLE))
a.const("MENU_ITEMS", 3)
a.main()
