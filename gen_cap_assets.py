#!/usr/bin/env python3
# TPMS CAP texts and icon, read by asset_read so they cost no overlay bytes.
# Uses app_assets.py of the F4HWN app SDK (folder in $SDK, set by build.sh).
import os
import sys

sys.dont_write_bytecode = True
sys.path.insert(0, os.environ.get("SDK", "sdk"))
from app_assets import Assets

a = Assets("TPMS_CAP")
for name, value in (
    ("T_CAP", "CAP"),
    ("T_KIF", "K IF"),
    ("T_SENS", " SENS "),
    ("T_TOYT", "TOYT "),
    ("T_HITS", "/72 HITS "),
    ("T_SENT", "SENT "),
    ("T_GAPS", "KB GAPS "),
    ("T_NOUART", "NO K-PLUG UART"),
    ("T_UART", "K-PLUG 115200: KPLUG.HTML"),
    ("T_HELP1", "2 BAND * IF 3 SENS 0 LIGHT"),
    ("T_HELP2", "ARROWS OFS 4 DEMOD 1 SPK 5 CLR"),
):
    a.text(name, value)
# Indexed by g.band433, g.ook and g.sens.
a.table("T_BAND", ["315.00 ", "433.92 "])
a.table("T_DEMOD", ["FSK", "OOK"])
# SENS levels as in TPMS EU: 0 LOW, 1 MID, 2 HIGH.
a.table("T_SENSN", ["LOW", "MID", "HIGH"])
# F4HWN gFontLight, as in TPMS EU: 9 status columns, LSB is the top pixel.
a.u8("LIGHT_ICON", [0x0C, 0x12, 0x21, 0x6D, 0x79, 0x6D, 0x21, 0x12, 0x0C])
a.main()
