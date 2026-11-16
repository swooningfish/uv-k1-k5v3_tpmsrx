# Toyota test settings

`TPMS EU` and `TPMS NA` have two radio settings for testing Toyota (and Ford)
reception. `TPMS EU3` has the same two for Renault and Ford (see
[TPMS EU3: the RENA count](#tpms-eu3-the-rena-count)). Both are saved on exit. Changing either one, or the band, retunes
the radio and resets the test line.

Before any sensor is listed, the line under the cars shows them:

```
-5K IF7E2C TOYT 40/72
```

- `-5K` is the tuning offset from 315.00 or 433.92 MHz, in kHz. `UP` / `DOWN`
  move it 5 kHz at a time, from −40 to +35 kHz. One more step wraps to the
  other end. The default is `0K`.
- `IF7E2C` is the BK4819 IF / demod register (`REG_43`). `*` steps through
  four presets, and `F` then `*` steps back one:

  | Preset | Meaning |
  |---|---|
  | `7E2C` | Default. 25 kHz mode, normal audio gain after the FM demod |
  | `7E28` | 25 kHz mode, demod gain bit flipped (6 dB step) |
  | `7E0C` | 12.5 kHz mode: more audio per kHz of deviation, but wide FSK clips sooner |
  | `7E08` | 12.5 kHz mode, demod gain bit flipped |

- `TOYT 40/72` is the most bits one Toyota sync has decoded, out of the 72 in a
  frame, since the last retune or SENS change. Noise alone seldom gets past
  about 12.

## Reading the count

Wake a Toyota sensor next to the radio. Every key below resets the count, so
change one setting, wake the sensor two or three times, then read the count.
On the UV-K1, `UP` / `DOWN` are `RIGHT` / `LEFT`.

| `TOYT` count | Meaning | Keys to try, waking the sensor after each |
|---|---|---|
| Stays near 0–12 | No Toyota preamble heard | `2` if the band may be wrong. Then `UP` once (`5K`), `DOWN` twice (`-5K`), `DOWN` once (`-10K`), `UP` three times (`10K`). Back at `0K`, `*` to try each preset |
| Rises to 20–70 | Bursts are heard but break up | `UP` / `DOWN` to find the offset with the highest count. At that offset, `*` to step through `7E28`, `7E0C` and `7E08` |
| Reaches 72, no row | Whole frames decode but fail the CRC | Bit errors near the end, or a sensor that only looks like this Toyota format. At the best preset, `UP` and `DOWN` one step each way |
| A `TOYT` row appears | Working | None. Note the offset and preset on the test line; they are saved on exit |

The test line only shows while the list is empty. If another sensor fills a
row, `MENU` then `CLEAR` brings it back. To return to the defaults, press
`UP` / `DOWN` until the line reads `0K`, then `*` until it reads `7E2C`.

## Which Toyota sensors

The decoder follows rtl_433's "Toyota TPMS" (Pacific PMV-C210, as in the Auris
and Corolla): 52 µs chips. The Pacific PMV-107J used on many US Toyotas at
315 MHz is a different format (100 µs chips, 66 bits) and is not decoded on
the radio.

To see each Toyota burst bit by bit on a PC, PMV-107J included, use
`TPMS CAP` with the K-plug programming cable and the browser page
`tools/kplug.html` (Chrome or Edge). See
[kplug.md](kplug.md).

## Screen capture

Close any screen capture (UV Studio or K5Viewer over the K-plug) while you
listen. The app sends the viewer a frame every half second, and on the
K-plug each frame takes up to 0.3 s, during which the app hears nothing.
Over USB the frames are short.

## See also

- [README](../README.md): the list apps, keys and screen.
- [How it works](how-it-works.md): the Toyota packet format and checks.

## TPMS EU3: the RENA count

`TPMS EU3` shows the same line with `RENA` in place of `TOYT`:

```
0K IF7E2C RENA 64/72
```

The count is the most bits one `AA A9` sync decoded. Renault and Ford share
that sync, so both raise it: a Ford frame is 64 bits and a Renault frame 72.
Read it with the table above, using `RENA` for `TOYT`, but `2` does nothing
(the band is fixed at 433.92). A Renault frame that reaches 72 and still shows
no row failed its CRC.
