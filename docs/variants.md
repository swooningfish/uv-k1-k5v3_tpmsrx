# Variant screens

All four apps are built from the same `tpmsrx_app.c` with a flavour flag.
These LCD mocks (`python3 tools/mock_lcd.py`) show the waiting screen and the
options menu for each variant. The status line carries the active **band** and
**protocol** (FSK or OOK); car lines name the sensors that flavour decodes.

Regenerate with:

```sh
python3 tools/mock_lcd.py
```

## TPMS EU — UK / Europe, default 433.92 FSK

Ford and Toyota (FSK only). Band is changeable. Schrader MRXGG4 is in TPMS EU2.

![EU waiting](variants/eu_waiting.png)

![EU options menu](variants/eu_menu.png)

## TPMS NA — US / Canada, default 315.00 FSK

Same sensors as EU; first launch parks on 315 MHz. Schrader MRXGG4 is in TPMS NA2.

![NA waiting](variants/na_waiting.png)

![NA options menu](variants/na_menu.png)

## TPMS EU2 — UK / Europe, 433.92 OOK (fixed)

Schrader MRXGG4, Vauxhall / Opel / Saab (EG53) and BMW / Mini (MRXBC5A4). Band locked.

![EU2 waiting](variants/eu2_waiting.png)

![EU2 options menu](variants/eu2_menu.png)

## TPMS NA2 — US / Canada, default 315.00 OOK

Schrader MRXGG4, Chevrolet / GM (EG53), Subaru / Nissan. Band still toggles.

![NA2 waiting](variants/na2_waiting.png)

![NA2 options menu](variants/na2_menu.png)

## Shared gallery shots

Used from the main [README](../README.md) Screen section (EU examples):

| File | What it shows |
|---|---|
| [main_screen.png](main_screen.png) | Empty list / listening (EU) |
| [list.png](list.png) | Three sensors + free-row shortcuts (EU) |
| [menu.png](menu.png) | Options menu (EU) |
