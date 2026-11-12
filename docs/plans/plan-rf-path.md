# TPMSRX RF path plan

Ordered by dependency. Context: TEMP433 is a thermometer AM/OOK front-end; TPMSRX needs fixed TPMS frequencies and an FSK-capable AF path.

**Status (implemented in `tpmsrx_app.c`):** mute clear after `tones_off_rx`, parked 315/433.92 only (no sweep), shared IF + path-owned AF for FSK↔OOK, `set_agc(false)` while listening with restore on exit. No optional tight dwell.

## 1. Fix AF mute after PLL lock / mode switch

**Why first:** `tones_off_rx()` (`BK4819_TurnsOffTones_TurnsOnRX`) relocks the PLL via `REG_30` and forces AF mute (`REG_47` → mute). If demod / unmute runs before that, or is incomplete on a path switch, PA4 stays silent and every later change looks like “no sensors.”

**Where today:** `applyRf()` in `tpmsrx_app.c` (~182–201). Call order is already “lock then demod,” but:

- FSK only writes `REG_47 = 0x6942` (bypass); no durable unmute contract on every switch.
- OOK calls `set_af(APP_AF_AM)` but does not clear leftover FSK `REG_47` / filter state.
- Callers: launch (`app_main`), band key/menu, `cyclePref` / AUTO `setPath`.

**Plan:**

1. Keep PLL latch as: write `REG_38`/`REG_39` → `tones_off_rx()` → then AF/IF.
2. After every lock, fully program the active path (FSK: `REG_2B` bypass, `REG_73` AFC off, `REG_43` wide, `REG_47` bypass; OOK: `set_af(APP_AF_AM)` + clear bypass leftovers if OOK remains).
3. Confirm no second mute after those writes on mode or band change.

**Verify:** After FSK↔OOK and 315↔433.92 toggles, AF is not mute (`REG_47` ≠ mute value); live RSSI / PA4 activity still moves; at least one known burst can be heard by the slicer (amp rises in `dectpms_push`).

---

## 2. Frequency tuning (TEMP433 “point 4,” TPMS-shaped)

**Why second:** Useless to demod until the PLL sits on a TPMS carrier. Do **not** copy TEMP433’s wide 433.80–434.04 / 10 kHz thermometer sweep.

**Where today:** `FREQ_315` / `FREQ_433` (`31500000` / `43392000`, ×10 Hz), `g.cfg.band433`, flavour defaults in `app_main`, band toggle key `2` / menu. Already closer to TPMS than TEMP433; tighten and document, don’t widen.

**Plan:**

1. **EU:** Park on `43392000` (default / EU2 lock).
2. **NA:** Keep `31500000` with band toggle and NA/NA2 flavour defaults (`band433 = 0`).
3. Leave `REG_33` UHF LNA selection as-is unless mute/freq work shows antenna path still wrong (follow-up if VHF launch still dead).

### Recommendation: frequency sweep — **drop it (park only)**

| | Thermometers (TEMP433) | Automotive TPMS |
|---|---|---|
| Why off-freq | Cheap SAW resonators, often ±50–75 kHz around 433.92 | Crystal / tighter SAW; rtl_433-class RX parks on **315.000** or **433.920** |
| Burst length | Long / periodic, can wait for next sweep hit | Short (tens of ms); miss the hop = miss the packet |
| TEMP433 AUTO | 433.80–434.04, **25×10 kHz**, center every other visit | Wrong shape for TPMS |

**Why not a TEMP433-like or even a “tight” continuous sweep in this build:**

1. **Tolerance:** TPMS does not need ±120 kHz coverage. IF already uses wide `REG_43` (~25 kHz-class); that plus parked PLL covers normal crystal/SAW error. A thermometer-width grid wastes dwell on empty channels.
2. **FSK path:** `listen()` must stream PA4 at **38.4 kHz** through preamble + payload. Retuning mid-burst (PLL settle + item‑1 unmute) shreds Manchester timing. Any sweep would have to run **only between** quiet gaps — complex, easy to get wrong, low gain for TPMS.
3. **Mute fix:** Every step is `applyRf` / `tones_off_rx` → mute → restore AF. A 10 kHz grid multiplies mute/unmute races and RSSI settle holes; parking once after item 1 is the stable contract.

**If field evidence later shows systematic ±offset (unlikely for Ford/Toyota/Schrader):** make an **optional** debug dwell, not default AUTO — e.g. ±20–30 kHz at **20–25 kHz** steps (3–5 points), only while `dectpms` is idle/`ST_HUNT` with no amp, full item‑1 unmute after each hop. Do **not** ship TEMP433’s ±120 kHz / 10 kHz thermometer sweep.

**Decision for this plan:** **Drop sweep. Park on 315.00 or 433.92 only.** Revisit optional tight dwell only after mute + parked FSK are proven on air.

**Verify:** Status line shows the intended band; `REG_38`/`REG_39` read back as 315.000 or 433.920; EU flavour cold-start → 433.92; NA → 315; key `2` flips and retunes without mute regression (item 1). No frequency hopper in the listen loop.

---

## 3. Demod: FSK path at ~38.4 kHz (vs TEMP433 AM/OOK)

**Why last:** Needs unmuted AF (1) on a parked TPMS frequency (2).

**Where today:** Sample loop in `listen()` uses `FS = 38400`. FSK branch in `applyRf` already aims at raw AF; OOK uses `APP_AF_AM`. `dectpms.c` slicer expects 38.4 kHz chip timing (`STEP_FSK` / `STEP_OOK`). TEMP433 uses 20 kHz AM envelope — do not adopt that rate or its PPM/PWM table.

**Plan:**

1. Treat **FSK + 38.4 kHz** as the primary TPMS receive path for Ford/Toyota flavours: after mute fix, ensure FSK always applies:
   - `REG_43` wide IF (confirm `0x7E2C` vs TEMP433 `0x7E28` on hardware; pick one known-good).
   - `REG_2B` RX filters bypassed.
   - `REG_73` AFC off.
   - Fixed/stable gain if needed (`set_agc(false)`), consistent with a steady slicer.
   - `REG_47` raw/bypass after mute (not TEMP433-only `APP_AF_AM`).
2. Keep OOK as a second path for Schrader / EU2 / NA2; do not delete it in this pass unless review says FSK-only.
3. Do not change decoder CRC/framing in this RF plan except as needed to re-init after `applyRf`.

**Verify:** Host tests still pass (`test/run_tests.sh`). On radio: FSK mode + correct band → Ford/Toyota row appears; OOK mode still works for a known OOK sensor if in flavour. Confirm sample pacing remains 38.4 kHz (`CYC_PER_SAMPLE`).

---

## Out of scope (this plan)

- TEMP433 AUTO frequency sweep / 20 kHz capture (explicitly rejected for TPMS; see §2).
- Optional ±20–30 kHz debug dwell (deferred until parked RX is proven).
- New sensor parsers or UI.
- Commits/PRs until this plan is approved and items are implemented.
