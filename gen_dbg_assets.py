#!/usr/bin/env python3
# TPMS DBG texts and icon, read by asset_read so they cost no overlay bytes.
# Uses app_assets.py of the F4HWN app SDK (folder in $SDK, set by build.sh).
import os
import sys

sys.dont_write_bytecode = True
sys.path.insert(0, os.environ.get("SDK", "sdk"))
from app_assets import Assets

a = Assets("TPMS_DBG")
# Short labels first: their offsets fit a Thumb movs immediate.
for name, value in (
    ("T_DBG", "DBG"),
    ("T_W", "W"),
    ("T_N", " N"),
    ("T_K", " K"),
    ("T_S", " S"),
    ("T_RB", " Rb"),
    ("T_SW", " W"),
    ("T_M", " M"),
    ("T_L", " L"),
    ("T_GAP", "  "),
    ("T_LEFT", "S LEFT"),
    ("T_NONE", ""),
    ("T_HEARD", "HEARD A BURST?"),
    ("T_HISS", "SPEAKER ON. HEAR HISS?"),
    ("T_WAKE", "WAKE A SENSOR NOW"),
    ("T_DONE", "MENU WHEN DONE"),
    ("T_CLOSE", "MENU OR EXIT TO CLOSE"),
    ("T_PICK", "ARROWS PICK, MENU OK"),
):
    a.text(name, value)
# Indexed by g.wizSel: 0 Yes, 1 No.
a.table("T_YESNO", [" > YES    NO", "   YES  > NO"])
# Three body rows per verdict, in V_ order (tpmsdbg_app.c).
a.table("T_VERDICT", [
    "AF MUTED: F IS 0", "EXIT, RELAUNCH, CHECK F9 OR F7", "",
    "NO HISS: TURN VOLUME UP", "EXIT, THEN MENU TO RETRY", "",
    "RF AND SLICER OK", "USE LIST APP, SAME BAND/MODE", "LIST EMPTY? MODEL NOT IN APP",
    "NO RF SEEN, NOTHING HEARD", "MOVE CLOSER, CHECK BAND (2)", "AND THE ANTENNA",
    "HEARD IT, BUT NO RSSI RISE", "MOVE CLOSER AND RETRY", "",
    "RF SEEN, SLICER MISSED IT", "PRESS * FOR FSK/OOK, RETRY", "",
    "RF SEEN, BUT NOTHING HEARD", "CHECK F, PRESS * FOR DEMOD", "",
])
# Speaker: box, cone, one sound wave. 7 status columns, LSB is the top pixel.
a.u8("SPK_ICON", [0x1C, 0x1C, 0x3E, 0x7F, 0x00, 0x22, 0x1C])
a.main()
