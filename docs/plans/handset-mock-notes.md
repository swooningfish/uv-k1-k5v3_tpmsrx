# Handset vs mock notes (UV Studio)

Greg’s UK/EU K5Viewer shots (lavender palette) are the style target for docs
mocks — not Temp 433’s green-grey LCD PNGs.

## Captures

- Menu: `assets/image-91971ca4-…png` — options menu, `433.92 OOK`, `81%`
- Listening: `assets/image-18b009ef-…png` — EU2 cars, older listening layout

## Findings

| Area | Handset (UV Studio) | Mocks / code |
|---|---|---|
| Colors | K5Viewer grey `#e5e0ec` / `#2a213f`, LCD pixel effect, pixel size 7 (6×7 cell, 5×6 dot) | `tools/mock_lcd.py` matches |
| Battery icon | F4HWN 16×7 outline + 3 bars + tip | Same bitmap in mock; live app uses `draw_battery()` |
| Menu capsules | All rows `print_inverse` (selected wider) | Matches `drawMenu` |
| Listening layout on shot | Pre-change: `LISTENING` alone + region line, no free-row hints | Current `draw()`: merged `LISTENING <band> <region>` + hints |

## Rebuild

`.app` images are produced by `./build-docker.sh` / `./build.sh` into
`build/` (gitignored). Flash the flavour Greg needs after a successful local
or CI build.
