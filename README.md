# TPMS RX

[![Build](https://github.com/swooningfish/uv-k1-k5v3_tpmsrx/actions/workflows/build.yml/badge.svg)](https://github.com/swooningfish/uv-k1-k5v3_tpmsrx/actions/workflows/build.yml)

Receive-only tyre-pressure (TPMS) apps for the
[F4HWN firmware](https://github.com/armel/uv-k1-k5v3-firmware-custom) on the
Quansheng UV-K1 and UV-K5 V3. Each app shows a live list of the sensors it
hears: model, id, pressure, temperature, signal strength, and how long ago
each one was heard. The radio never transmits.

![Listening for a sensor](docs/main_screen.png)

## Which app

Pick the app for the region the car was sold in and the type of sensor it
uses. If you are not sure, install both apps for your region.

| App | Region | Band | Demod | Sensors |
|---|---|---|---|---|
| `TPMS EU` | UK and Europe | 433.92 MHz (can switch to 315) | FSK | Ford, Toyota |
| `TPMS NA` | United States and Canada | 315 MHz (can switch to 433.92) | FSK | Ford, Toyota |
| `TPMS EU2` | UK and Europe | 433.92 MHz, fixed | OOK | Schrader MRXGG4; Vauxhall, Opel, Saab; BMW and Mini (older Schrader sensor) |
| `TPMS NA2` | United States and Canada | 315 MHz, fixed | OOK | Schrader MRXGG4; Chevrolet and other GM; Subaru; Nissan and Infiniti |
| `TPMS DBG` | Either | 433.92 MHz (can switch) | FSK or OOK | No decoding: records raw bursts to find out why a list app hears nothing |

- `TPMS EU` and `TPMS NA` are the same app with a different starting band.
  A band you change is remembered.
- **Upgrading from before v1.3.0?** Schrader MRXGG4 moved from `TPMS EU` /
  `TPMS NA` to `TPMS EU2` / `TPMS NA2`, and the AUTO mode is gone. Each app
  now has one demod.
- BMW and Mini: only the older Schrader sensor (often sold as MRXBC5A4 or
  MRXBMW433) is decoded. Newer FSK sensors are not.
- Subaru and Nissan send the same kind of packet and cannot be told apart.
  Both show as `NISS` on the Nissan pressure scale, so **a Subaru tyre reads
  about a quarter higher than it really is**. Chevrolet and GM use a
  different sensor (`EG53`) and read correctly.

All five apps build from the same sources (`tpmsrx_app.c`, `dectpms.c`, and
`tpmsdbg_app.c` for `TPMS DBG`) with a flavour flag, so there are no separate
regional code trees.

## Install

You need a UV-K1 or UV-K5 V3 running F4HWN Labs or Fusion with overlay apps
(app API level 3 or higher; tested against the v6.3.0 SDK). The apps can be
installed side by side, one per free slot.

1. Download the zip for your app from the
   [Releases](https://github.com/swooningfish/uv-k1-k5v3_tpmsrx/releases)
   page (`TPMSEU.zip`, `TPMSNA.zip`, `TPMSEU2.zip`, `TPMSNA2.zip`,
   `TPMSDBG.zip`) and unzip it to get the `.app` file. Releases are zipped
   because GitHub rejects `.app` uploads. Each
   [build workflow run](https://github.com/swooningfish/uv-k1-k5v3_tpmsrx/actions)
   also has the raw `.app` files as artifacts.
2. Open [UV Studio Apps](https://armel.github.io/uvstudio/#apps) in your
   browser, connect the radio, and install the `.app` from a local file into a
   free slot.
3. On the radio, open the Apps menu (`F` then `7`), select the app, and press
   `MENU`.

## Using the app

### Keys

| Key | Action |
|---|---|
| `MENU` | Open the options menu |
| `1` | Speaker on or off. A speaker icon shows on the status line while it is on |
| `2` | Switch between 315.00 and 433.92 MHz (`TPMS EU` and `TPMS NA` only) |
| `0` | Force backlight on or off. While forced, the screen stays lit and a light bulb shows on the status line. Off goes back to lighting the screen for 10 seconds after each key. Saved on exit |
| `EXIT` | Quit, or close the menu. Once the screen has gone dark, the first press only lights it |

With the speaker on you hear what the decoder hears. Hiss means the receiver
is working, and a short rasp or click is a sensor sending. Silence means the
audio is muted or there is no signal.

A good decode blinks the green LED, even with the backlight off.

### Options menu

![Options menu](docs/menu.png)

| Item | What it does |
|---|---|
| `BAND` | `315.00` / `433.92`. Only in `TPMS EU` and `TPMS NA` |
| `SENS` | Sensitivity: `LOW` / `MID` / `HIGH` (default `MID`). `HIGH` hears weaker, more distant sensors; `LOW` ignores more noise and far-off cars. Saved on exit |
| `CLEAR` | Wipe the list |

`UP` / `DOWN` (the navigation keys on the UV-K1) select an item. `MENU`, `2`
or `5` change it. `EXIT` closes the menu. The menu also shows the app version.
Decoding pauses while the menu is open, so a sensor that sends then is missed.

### The screen

The status line shows, from left to right:

- `TPMS` (or `MENU` in the menu).
- A light bulb only while the backlight is forced on with `0`. It starts on
  when the radio's own backlight setting is always on (on firmware that lets
  apps read it); the radio then keeps the screen lit even with the bulb off.
  A key that lights the screen for 10 seconds, such as `1` for the speaker,
  does not show it.
- The band and demod, for example `433.92 FSK` or `315.00 OOK`.
- A speaker icon while the speaker is on. Its space is always reserved, so
  nothing shifts when it appears.
- The battery percent and icon.

Before any sensor is heard, the screen shows `LISTENING <band> <region>` and
the cars that app decodes. While there are three or fewer sensors, the free
rows show key hints (`1 SPK`, `2 BAND`, `0 LIGHT`).

The list holds up to seven sensors. A sensor that is heard again updates its
own row.

![A few sensors](docs/list.png)

```
FORD A1B2C3D4 36psi 18C -67 4s
TOYT 00F211A9 28psi 1C -72 12s
```

Each row is: model, id, pressure in whole PSI, temperature in whole °C,
signal in dBm, and age (seconds, then minutes, then hours). Models are
`FORD`, `TOYT`, `SCHR`, `EG53`, `BMW` and `NISS`. `NISS` has no temperature.
The id has as many hex digits as the sensor sends, as rtl_433 prints it: 8 for
`FORD` and `TOYT`, 7 for `SCHR`, and 6 for the rest. A row that would be wider
than the screen, such as `-12C -105 45s`, drops the space after the
temperature (`-12C-105 45s`).

On `TPMS EU2` and `TPMS NA2` the signal is read about 2 ms into the burst,
while the sensor is still sending.

Drawing the screen pauses decoding, so the list only redraws on a key press
or 200 ms after a decode. Ages are as of the last redraw.

The waiting and menu screens for all four list apps are in
[docs/variants.md](docs/variants.md).

### Troubleshooting

If the list stays on `LISTENING`:

1. Wake the sensor: use a TPMS tool, let a little air out, or drive a short
   distance. Most sensors send rarely while parked.
2. Try the other app for your region. `TPMS EU` / `TPMS NA` hear FSK sensors,
   `TPMS EU2` / `TPMS NA2` hear OOK sensors.
3. Turn the speaker on (`1`) and listen for a rasp when the sensor sends.
4. Run `TPMS DBG`. It does not decode, but it shows whether any signal reaches
   the radio and records each raw burst, and its `MENU` wizard suggests what
   to change. See [docs/debug.md](docs/debug.md).

If `TPMS DBG` records bursts but no list app shows the sensor, that sensor
model is probably not supported.

## How a reading is checked

The field layouts follow [`rtl_433`](https://github.com/merbanan/rtl_433).
Every packet must pass its checksum before it is shown.

- **Ford** (`TPMS EU`, `TPMS NA`). FSK. Preamble `55 56`, or `AA A9` when the
  signal arrives inverted. Manchester decoding gives 8 bytes, and bytes 0 to 6
  must sum to byte 7. Pressure is a 9-bit value in quarter PSI (flag bit
  `0x20` is the ninth bit). Temperature is `(byte & 0x7f) − 56` °C, and is
  left out when bit `0x80` is set.
- **Toyota** (`TPMS EU`, `TPMS NA`). FSK. 12-bit preamble `A9E` (or `561`
  inverted), then differential Manchester to 9 bytes. CRC-8, polynomial
  `0x07`, initial value `0x80`, and the pressure must match an inverted copy
  of itself. PSI is `raw ÷ 4 − 7`. Temperature is `raw − 40` °C.
- **Schrader EG53MA4** (`TPMS EU2` for Vauxhall, Opel and Saab; `TPMS NA2` for
  Chevrolet and other GM). OOK Manchester, 120 bits; the first 40 are
  dropped. The next 9 bytes must sum to the checksum byte. Pressure is
  `byte × 2.5` kPa. The packet holds Fahrenheit, shown as Celsius.
- **Schrader MRXBC5A4** (`TPMS EU2`, BMW and Mini). OOK Manchester, 61 bits,
  starting with `0111 1111 1111 1111`. 24-bit id. Pressure is 1 kPa per step,
  temperature is `byte − 50` °C. The two check bits are weak, so a reading is
  also rejected above 450 kPa or outside −40 to 85 °C.
- **Nissan NIS315G3 and Subaru SMD3MA4** (`TPMS NA2`). OOK PCM with sync
  `555E`, then Manchester. PSI is `raw ÷ 4` (Nissan scale, see the Subaru note
  under [Which app](#which-app)). No temperature.
- **Schrader MRXGG4** (`TPMS EU2`, `TPMS NA2`). OOK Manchester, 68 bits; the
  sync nibble is dropped. CRC-8, polynomial `0x07`, initial value `0xF0`.
  Pressure is `byte × 2.5` kPa, temperature `byte − 50` °C. It has no sync
  word of its own, so it is tried last.

## How it works

```
FSK:  RF -> BK4829 raw AF, wide IF, AFC off -> PA4 at 38.4 kHz
          -> slicer -> PCM bits -> Manchester or differential Manchester
          -> checksum -> row

OOK:  RF -> BK4829 AM -> PA4 at 38.4 kHz
          -> slicer -> Manchester bits -> checksum -> row

NA2:  the same OOK path, plus a second pass on the raw bits for Nissan and Subaru
```

The app tunes the receiver to 315.000 or 433.920 MHz and switches on the UHF
antenna path where it can read that switch back. Quitting restores the
channel you were on.

## Build

With Docker, this builds all five apps into `build/` and runs the host parser
tests. The image is built on first use.

```sh
./build-docker.sh
```

Without Docker you need `arm-none-eabi-gcc`, Python 3 and bash:

```sh
python3 tools/fetch_sdk.py   # downloads the F4HWN app SDK into sdk/
./build.sh                   # writes build/TPMSEU.app, TPMSNA.app, TPMSEU2.app, TPMSNA2.app, TPMSDBG.app
```

- Build one app: `./build.sh eu`, `na`, `eu2`, `na2` or `dbg`.
- Set `PYTHON` if Python 3 is not called `python3`, for example
  `PYTHON=python ./build.sh`.
- Run the host parser tests (no radio needed): `./test/run_tests.sh`.
- Redraw the screen pictures in `docs/`: `python3 tools/mock_lcd.py`.
- The release version is `APP_VER` in `build.sh`.

Code sizes in v1.3.1 (each app must fit the 4 KB app space): `TPMS EU` 3892 B,
`TPMS NA` 3888 B, `TPMS EU2` 3892 B, `TPMS NA2` 3948 B, `TPMS DBG` 4060 B.

## License

Apache License 2.0, see [LICENSE](LICENSE) and [NOTICE](NOTICE).
