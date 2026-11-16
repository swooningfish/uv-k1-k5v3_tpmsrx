# TPMS CAP and the K-plug decoder

`TPMS CAP` lets a PC see every burst the radio hears, bit by bit. Use it when
a list app stays empty for a sensor that should decode, Toyota in particular.

The radio parks and slices exactly like the list apps: same band, tuning
offset, IF presets, SENS levels and slicer, with the FSK audio of
`TPMS EU` / `TPMS NA` or the OOK (AM) audio of `TPMS EU2` / `TPMS NA2` (key
`4`). Instead of decoding on the radio, it
sends every sliced audio sample out of the K-plug, the two-pin audio
connector the programming cable plugs into, the one UV Studio uses. On the
PC, the browser page `tools/kplug.html` reads the cable and runs the rtl_433
decoders:

| Demod | Sensors |
|---|---|
| FSK | **Toyota** (Pacific PMV-C210), as `TPMS EU` / `TPMS NA`; **PMV-107J** (Pacific, many US Toyotas at 315 MHz), which no radio app decodes; **Ford**; **Renault**, as `TPMS EU3` |
| OOK | **Schrader MRXGG4**, **GM EG53**, **BMW / Mini** and **Nissan / Subaru**, as `TPMS EU2` / `TPMS NA2` |

Only the FSK decoders fit on the radio, so in OOK the decoding is all on the
PC: no 📻 "radio decoded" lines, and no hint about frames the radio missed.

For each burst you get how many bits decoded, whether the CRC passed, and the
id, pressure and temperature when it checks out. That shows whether the radio
hears the sensor at all, how far each burst decodes, and whether the sensor is
a format the radio apps do not decode. The page also shows the radio's
settings, lets you change them from the PC, and gives hints on what to change.

## What you need

- The K-plug programming cable (USB to the radio's speaker / mic jack), the
  same one UV Studio uses over the K-plug.
- `TPMS CAP` installed on the radio (`build/TPMSCAP.app`, installed with UV
  Studio like the other apps). It needs firmware whose app interface has
  `blit_page`; on an older one it returns straight to the Apps menu.
- Chrome or Edge on a desktop. They have Web Serial, which UV Studio needs
  too.

## Steps

1. Plug the cable into the radio and the PC.
2. Close UV Studio and K5Viewer. They use the same cable, and their screen
   frames would mix into the stream.
3. Start `TPMS CAP` on the radio. Its first row should read
   `K-PLUG 115200: KPLUG.HTML`.
4. Open `tools/kplug.html` in Chrome or Edge (double-click it; it is one
   self-contained file and needs no server or internet).
5. Press **Connect** and pick the cable's port.
6. Pick the demod for the sensor: **FSK** for Ford and Toyota, **OOK** for
   Schrader, GM, BMW / Mini and Nissan / Subaru (key `4`, or the card).
7. Wake the sensor next to the radio. Each burst appears in the log as it is
   decoded. Click a burst to see its raw chips.
8. Follow the **💡 What to try** box, waking the sensor after each change.
   Change the settings on the radio's keys or in the **📻 Radio settings**
   card.
9. Press **Save capture** and **Save log**, and send both back for debugging.
   **Open capture…** replays a saved capture in the page.

The page shows the data rate (about 5.5 KB/s while `TPMS CAP` runs), the
radio's band, offset and IF preset, and per model the bursts shown, the OK
decodes and the best partial decode. It warns when no data arrives or the
rate is wrong for the baud rate. **Run self-test** checks the decoders.

## Radio settings card

The card shows the band, demod, tuning offset, IF preset, SENS level and backlight
the radio last reported (it sends them every 2 s and after each change), and
has a control for each:

| Control | Does the same as |
|---|---|
| Band `315.00` / `433.92` | Key `2` |
| Demod `FSK` / `OOK` | Key `4` |
| Offset slider, `−5` / `+5` / `0` | `DOWN` / `UP`. The slider moves in 5 kHz steps from −40 to +35 kHz and sends when you let go; `0` goes back to the band centre |
| IF `25 kHz` (`7E2C`), `25 kHz alt` (`7E28`), `12.5 kHz` (`7E0C`), `12.5 kHz alt` (`7E08`) | `*` |
| Sens `LOW` / `MID` / `HIGH` | `3` |
| Light `Timed` / `On` | `0` |
| Clear radio counters | `5` |
| Refresh | Asks the radio to send its settings now |

A button you press shows a dashed outline until the radio reports the new
value, then the log gets a ⚙️ `Radio set` line. If the radio does not confirm
within 3 s, the log says so: an older `TPMS CAP` build only sends. The
controls work only while connected; a replayed capture shows the settings it
holds, read-only.

The IF presets are two settings of the BK4819's `REG_43`. The default is
**25 kHz (`7E2C`)**, the value every list app uses; the page marks it on the
button and on the IF tile.

| Preset | Code | Bandwidth | Gain bit |
|---|---|---|---|
| 25 kHz (**default**) | `7E2C` | 25 kHz: catches sensors that sit off frequency, as OOK sensors often do | As every list app uses it |
| 25 kHz alt | `7E28` | 25 kHz | Flipped: a 6 dB step in the gain after the FM demod |
| 12.5 kHz | `7E0C` | 12.5 kHz: narrower, so less noise, if the sensor is on frequency | As every list app uses it |
| 12.5 kHz alt | `7E08` | 12.5 kHz | Flipped |

The gain bit acts after the FM demod, so in OOK (AM) it probably changes
nothing: expect `25 kHz` and `25 kHz alt` to rank the same there. The card
says so while the radio is in OOK. The radio's own screen still shows the
code (`IF7E2C`).

## Leaderboard

The **🏆 Leaderboard** lists every setting (band, demod, offset, IF preset and SENS)
the radio has been on, with the time spent there and what the bursts did:

| Column | Meaning |
|---|---|
| Time | Stream time at that setting |
| ✅/min | Frames that passed their checks, per minute |
| ❌/min | Whole frames that failed their check, per minute |
| 🟠/min | Frames that broke up, per minute |
| Best | `✅ full` once a frame decoded there, else the most bits one burst gave |

Rows are ranked by ✅ per minute, then ❌, then the best bit count, then 🟠.
Counting per minute lets a short and a long stay compare fairly. A setting
with less time than **Rank after** (15 s by default) is listed below the
ranked ones, with the time it still needs. The row the radio is on is marked
`◀ now`; **Use** on any other row puts the radio back on that setting.
**Reset** starts the timings and counts again.

Bursts only come when the sensor sends, so give each setting the same chance:
keep waking the sensor while you test, or use the auto sweep.

## Auto sweep

**🔁 Auto sweep**, under the radio settings, gives every setting the same time:

1. Pick the offset range, and tick **all 4 presets** and **all 3 levels** to
   sweep IF and SENS too, and **both FSK and OOK** to run every step in each
   demod (the current one first). Otherwise those stay as they are. Sweeping
   both demods finds the sensor's type too: only one of them decodes it.
2. Set the time per setting (30 s by default). The **Plan** line under the
   boxes shows the steps and the total time, for example
   `Plan: 1 demod × 9 offsets × 1 IF × 1 SENS = 9 steps × 30 s ≈ 5 min`.
3. Press **Start sweep** and keep waking the sensor all through. A TPMS tool
   on repeat works best. See [How often sensors transmit](#how-often-sensors-transmit).

The page steps the radio through each setting, waits for the radio to confirm
it, and shows the step and the time left. **Rank after** is lowered to match
the step time so every step is ranked. At the end, the best setting is named
in the log; press **Use** on its leaderboard row to keep it. **Stop** ends the
sweep early. With every box ticked, 2 demods × 16 offsets × 4 presets × 3
levels is 384 steps, so narrow the range first.

### Sweep sizes

| Sweep | Steps | At 30 s a step |
|---|---|---|
| ±20 kHz (the default range), one demod, IF and SENS as they are | 9 | about 5 min |
| ±20 kHz, all 4 IF presets | 36 | about 18 min |
| ±20 kHz, both demods | 18 | about 9 min |
| Full −40 to +35 kHz, one demod | 16 | about 8 min |
| Everything ticked, full range | 384 | about 3.2 h |

Start narrow (offset only, around 0 kHz), then sweep IF and SENS at the best
offset. A 10–15 s step is enough when a trigger tool fires every few seconds.

## How often sensors transmit

The sweep and the leaderboard can only count bursts the sensor sends. Typical
behaviour, which varies by maker:

| Sensor state | Typical transmissions |
|---|---|
| Parked | Asleep: very rarely or not at all. rtl_433's notes on the Ford sensor: once after stopping, then about every 6 hours |
| Driving (motion detected, usually over about 20–25 km/h) | About once a minute, each a short burst of a few repeated frames (Ford about 4; the PMV-107J counts 1, 2, 3) |
| Fast pressure change (air let out quickly) | Straight away, then every few seconds while the pressure keeps changing (the PMV-107J sets a rapid-change flag) |
| TPMS trigger tool (125 kHz) | Straight away, a burst of frames (Ford sends 12 in learn mode) |

So for testing:

- On a parked car, do not wait: trigger the sensor with a tool, or let a
  little air out in short bursts.
- In a sweep, keep triggering all through, and make each step longer than the
  gap between triggers so every step gets several bursts. A step with no
  trigger records nothing, whatever the setting.
- Driving gives one burst a minute per wheel: too slow for a sweep, but fine
  to check that a setting you found keeps decoding.

## The radio screen

| Row | Meaning |
|---|---|
| `K-PLUG 115200: KPLUG.HTML` | Streaming, for `tools/kplug.html`. `NO K-PLUG UART` means this firmware has no K-plug UART set up, and nothing is sent |
| `0K IF7E2C SENS MID` | Tuning offset, IF preset and sensitivity, as on `TPMS EU` / `TPMS NA` |
| `TOYT 40/72 HITS 3` | The radio's own Toyota decode counter, and the packets the radio decoded itself (FSK only) |
| `SENT 120KB GAPS 9` | Bytes sent, and pauses in sampling (one per screen redraw) |

| Key | Action |
|---|---|
| `2` | Switch between 315.00 and 433.92 MHz |
| `4` | FSK (Ford, Toyota) or OOK (Schrader, GM, BMW / Mini, Nissan / Subaru). The status line shows `FSK` or `OOK` |
| `*` | Next IF preset (`7E2C`, `7E28`, `7E0C`, `7E08`). `F` then `*` goes back one |
| `3` | Sensitivity `LOW` / `MID` / `HIGH` (default `MID`), the same levels as `SENS` in the `TPMS EU` menu. `HIGH` hears weaker, more distant sensors; `LOW` ignores more noise. `F` then `3` goes back one |
| `F` | Step back: the next `*` or `3` goes to the previous setting. It applies to the next key only; press `F` again to cancel. See [Stepping back with F](../README.md#stepping-back-with-f) |
| `UP` / `DOWN` | Tuning offset 5 kHz up or down (`RIGHT` / `LEFT` on the UV-K1) |
| `1` | Speaker on or off |
| `0` | Force backlight on or off, as in `TPMS EU`. While forced, a light bulb shows on the status line. Otherwise the screen goes dark 10 s after the last key, and the first `EXIT` only lights it. Saved on exit |
| `5` | Clear the counters |
| `EXIT` | Quit. The K-plug goes back to 38400 baud |

Band, demod, preset, offset, SENS and the backlight option are saved on exit. They are separate from the
`TPMS EU` / `TPMS NA` settings.

## Reading the log

```
[    0.00s] ⚙️ radio: 433.92 MHz FSK, offset +0 kHz, IF 25 kHz (7E2C), SENS MID
[   12.41s] ✅ TOYOTA   OK  id 00F211A9  28.0 PSI  1 C (+) [pll]
[   12.41s] 📻 radio decoded TOYOTA
[   31.07s] 🟠 TOYOTA   52/72 bits (-) [rle]
[   44.90s] ❌ TOYOTA   72/72 bits, CRC FAIL  00F211A9468C80E3C7 (+) [rle]
[   44.90s] 💡 Whole frames arrive with a bit error. Press UP or DOWN on the radio …
[   58.12s] ✅ PMV-107J OK  id 0ABCDEF5  32.0 PSI  25 C (+) [rle]
[   61.30s] ⚙️ radio: 433.92 MHz OOK, offset +0 kHz, IF 25 kHz (7E2C), SENS MID
[   70.05s] ✅ SCHRADER OK  id 03A38B2  47.9 PSI  12 C (+) [rle]
[   81.77s] ❌ SCHRADER 68/68 bits, CHECK FAIL  F6703A38B2843EE8 (+) [rle]
[   92.40s] 🟠 OOK      66 bits, no known length (SCHRADER 68, BMW 61, EG53 120) (+) [rle]
```

- The time is stream time: seconds of samples received, pauses not counted.
- ✅ lines passed the CRC (the byte sum for Ford) and the pressure check.
- 🟠 `52/72 bits` means the sync was found but the frame broke after 52 bits.
  Noise alone rarely gets past about 12 bits (about 30 for PMV-107J, so a
  PMV-107J line needs 40 bits to show). Change the limit with **Min bits**.
- ❌ `CRC FAIL` means a whole frame decoded with a bit error, or a sensor that
  only looks like this format. The hex is the decoded frame. A frame of
  almost all 0 or all 1 bits is a stuck slicer, not a sensor, and is not
  shown.
- ⚙️ lines are the radio's band, demod, offset, IF preset and SENS level,
  shown when they change.
- OOK frames have no sync to count from, so they are told apart by length:
  Schrader 68 bits, BMW 61 (after a `7FFF` sync), GM EG53 120, and Nissan by
  its `555E` sync. A burst of another length shows as 🟠 `OOK … no known
  length`.
- 📻 lines are packets the radio's own decoder accepted.
- `(+)` / `(-)` is the polarity: of the FSK discriminator, or for OOK whether
  the carrier showed as 1 or as 0.
- Only the best match of each burst is shown: a short sync of one format often
  turns up inside another format's frame.
- `[pll]` used the radio's bit clock; `[rle]` timed each run of samples, as
  rtl_433 does. A burst found only by `[rle]` points at the radio's clock
  recovery.

## Hints

The page watches the bursts and puts a 💡 line in the log, and in the
**What to try** box, when it sees something to change. Each hint is given
once, and again after a retune (band, demod, offset, preset or SENS) if it
still applies.

| 💡 Hint | When | What to do |
|---|---|---|
| Bursts arrive but break up | Two partial decodes at one setting | Step the offset with `UP` / `DOWN`, waking the sensor after each step. Keep the one with the highest count, then try the presets with `*` and SENS with `3` |
| Whole frames arrive with a bit error | A full frame fails its check | One offset step each way, then the presets. If all fail the same way, send the capture: it may be another format |
| Best so far | After a retune, when the best setting changes | The setting that gave the best single result so far. Go back to it if nothing beats it; the leaderboard ranks them all |
| The PC decoded a frame the radio did not | A ✅ Toyota or Ford with no 📻 within half a second | The radio's bit clock is the weak spot. Send the capture |
| This sensor is a Pacific PMV-107J | A ✅ PMV-107J | The radio apps cannot decode it yet. Send the capture |
| OOK bursts match no known length | Two OOK bursts of another length | It may be another format: send the capture. Or try FSK with `4` |
| Nothing above noise for 30 s | 30 s of stream with no burst | Wake the sensor next to the radio, check the band with `2`, try SENS `HIGH` with `3` and the other demod with `4` |

## How it works

The radio samples the audio at 38400 Hz and slices it with the same code as
`TPMS EU`. Seven sliced samples go in each byte, so the stream is about
5.5 KB a second at 115200 baud. The bytes are sent between samples, so
sending does not stop sampling. Redrawing the screen does; the stream marks
each redraw as a gap.

Bytes on the wire:

| Byte | Meaning |
|---|---|
| `0xxxxxxx` | Seven sliced samples, oldest in bit 6 |
| `0x80` | Gap: sampling paused |
| `0x81 s v` | Settings: `s` bit 0 band (1 is 433.92), bits 2:1 IF preset, bits 6:3 offset in 5 kHz steps (two's complement); `v` bits 1:0 the SENS level (0 `LOW`, 1 `MID`, 2 `HIGH`), bit 2 the forced backlight, bit 3 the demod (1 is OOK) |
| `0x82 m` | The radio decoded a packet of model `m` (0 Ford, 1 Toyota; FSK only) |

Bytes from the PC are commands: `0xA5`, `c`, `c ^ 0xFF`. The inverted copy
keeps line noise from changing a setting. `c` holds a field in its high
nibble and a value in its low one:

| `c` | Sets |
|---|---|
| `0x1v` | Band (`v` 1 is 433.92) |
| `0x2v` | IF preset 0 to 3 |
| `0x3v` | Offset nibble (two's complement, 5 kHz steps) |
| `0x4v` | SENS level 0 to 2 |
| `0x5v` | Forced backlight 0 or 1 |
| `0x60` | Clear the radio's counters |
| `0x70` | Send the settings record now |
| `0x8v` | Demod (`v` 1 is OOK) |

The app interface reserves the K-plug port for apps but does not implement it
yet, so `TPMS CAP` uses the K-plug UART (USART1) directly. The resident
firmware has already set it up at 38400 baud for K5Viewer, with incoming bytes
going to a DMA ring for K5Viewer's keys. While it runs, `TPMS CAP` switches the
UART to 115200 baud and turns that DMA request off so commands reach the app;
it restores both on exit. If the UART is
not set up the way it expects, the app shows `NO K-PLUG UART` and leaves it
alone.

**Run self-test** in the page encodes one frame of each format and decodes it
from a stream in both polarities. `test/test_kplug.py` (part of
`./test/run_tests.sh`) runs the page's decoder in Node: the self-test, then a
scripted capture that checks each line, emoji and hint. It is skipped when
Node is not installed; the Docker image has it.
