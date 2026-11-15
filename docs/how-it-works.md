# How it works

## Signal path

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
  of itself. PSI is `raw ÷ 4 − 7`. Temperature is `raw − 40` °C. To tune for
  Toyota reception, see [Toyota test settings](toyota.md).
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
  under [Which app](../README.md#which-app)). No temperature.
- **Schrader MRXGG4** (`TPMS EU2`, `TPMS NA2`). OOK Manchester, 68 bits; the
  sync nibble is dropped. CRC-8, polynomial `0x07`, initial value `0xF0`.
  Pressure is `byte × 2.5` kPa, temperature `byte − 50` °C. It has no sync
  word of its own, so it is tried last.

## See also

- [README](../README.md): the list apps, keys and screen.
- [Build](build.md): building the apps and running the tests.
