#!/usr/bin/env python3
"""Run the decoder of tools/kplug.html in Node and check its lines and hints.

The decoder is the script between the DECODER START and DECODER END markers.
The test runs its self-test, then feeds one scripted capture (Toyota frames
that decode, fail their CRC or break up, a radio decode, a retune, a
PMV-107J frame and a long quiet spell) and checks each line and hint.
Needs Node; skipped without it.
"""
from __future__ import annotations

import os
import shutil
import subprocess
import sys
import tempfile

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))

NODE_SCRIPT = r"""
const K = require(process.argv[1]);
const r = K.selftest();
for (const line of r.lines) console.log('SELF ' + line);

const noise = K.noiseMaker(3);
const bytes = [];
/* Pad each piece to whole stream bytes so markers land right after it. */
const piece = (samples) => {
    const pad = (7 - (samples.length % 7)) % 7;
    for (const b of K.toStream(samples.concat(noise(pad)))) bytes.push(b);
};
const burst = (chips, spc) => piece(K.chipsToSamples(chips, spc, false));
const good = K.toyotaChips(false);
bytes.push(K.MARK_SET, 0x01, 1);
piece(noise(20000));
burst(good, K.SPC_52);
bytes.push(K.MARK_HIT, 1);
piece(noise(20000));
burst(good, K.SPC_52);
piece(noise(30000));
burst(K.toyotaChips(true), K.SPC_52);
piece(noise(20000));
burst(good.slice(0, 94), K.SPC_52);
piece(noise(20000));
burst(good.slice(0, 94), K.SPC_52);
piece(noise(20000));
bytes.push(K.MARK_SET, 0x01 | (15 << 3), 2 | 4);
piece(noise(20000));
burst(K.pmvChips(), K.SPC_100);
piece(noise(31 * K.FS));
bytes.push(K.MARK_GAP);

const hex = (a) => Array.from(a, (x) => x.toString(16).toUpperCase().padStart(2, '0')).join(' ');
for (const [f, v] of [['band', 1], ['ifp', 2], ['offset', 15], ['sens', 2], ['light', 1],
                      ['clear', 0], ['report', 0], ['demod', 1]]) {
    console.log(`cmd ${f}=${v}: ${hex(K.command(f, v))}`);
}
const show = (ev) => {
    if (ev.kind !== 'live') return console.log(ev.kind + ' ' + ev.text);
    const r = ev.radio;
    console.log(`live ${r.band} ${r.offset} ${r.ifr} sens=${r.sensLevel} light=${r.light}`);
};
const st = new K.Stream(24, show);
st.feed(bytes);
st.flush();
for (const min of [15, 1]) {
    st.leaderboard(min).forEach((r, i) => console.log(
        `board${min} ${i + 1}: ${r.band} ${r.offset} ${r.sens} ok=${r.ok} fail=${r.fail} ` +
        `partial=${r.partial} best=${r.best} ranked=${r.ranked} sec=${Math.round(r.seconds)}`));
}

/* OOK: TPMS CAP on AM audio. Every OOK model in both AM polarities, a bad CRC,
   then slicer chatter that must not pass for a frame. */
const ob = [K.MARK_SET, 0x01, 1 | 8];
const opiece = (smp) => {
    const pad = (7 - (smp.length % 7)) % 7;
    for (const b of K.toStream(smp.concat(new Array(pad).fill(smp[smp.length - 1])))) ob.push(b);
};
for (const inv of [false, true]) {
    for (const ch of [K.schraderChips(false), K.schraderChips(true), K.bmwChips(), K.eg53Chips(),
                      K.nissanChips()]) {
        opiece(K.ookToSamples(ch, inv));
        opiece(new Array(3000).fill(inv ? 1 : 0));
    }
}
opiece(K.noiseMaker(5)(3 * K.FS));
opiece(new Array(3000).fill(0));
ob.push(K.MARK_GAP);
const os = new K.Stream(24, (ev) => {
    if (ev.kind === 'live') console.log(`olive demod=${ev.radio.demod} ook=${ev.radio.ook}`);
    else console.log('o' + ev.kind + ' ' + ev.text);
});
os.feed(ob);
os.flush();
console.log('ostats ' + JSON.stringify(os.stats));

/* A demod switch right after a burst: the burst was sliced as FSK, so it must
   be decoded as FSK even though the OOK record arrives before the flush. */
const db = [K.MARK_SET, 0x01, 1];
const dpiece = (smp) => {
    const pad = (7 - (smp.length % 7)) % 7;
    for (const b of K.toStream(smp.concat(noise(pad)))) db.push(b);
};
dpiece(noise(4000));
dpiece(K.chipsToSamples(good, K.SPC_52, false));
dpiece(noise(2000));
db.push(K.MARK_SET, 0x01, 1 | 8, K.MARK_GAP);
const ds = new K.Stream(24, (ev) => {
    if (ev.kind !== 'live') console.log('d' + ev.kind + ' ' + ev.text);
});
ds.feed(db);
ds.flush();

/* Records broken by lost bytes: a marker inside one, or a bad v byte. */
let lives = 0;
const fs = new K.Stream(24, (ev) => {
    if (ev.kind !== 'live') return;
    lives++;
    console.log(`flive sens=${ev.radio.sensLevel} ook=${ev.radio.ook}`);
});
fs.feed([K.MARK_SET, 0x01, K.MARK_SET, 0x01, 2, K.MARK_SET, 0x01, 0x21, K.MARK_GAP]);
console.log('flives ' + lives);

/* An EG53 frame with a bad checksum fills all 120 bits but is not "full". */
const eg = K.eg53Chips().slice();
const k = 1 + 2 * 100;
[eg[k], eg[k + 1]] = [eg[k + 1], eg[k]];
const eb = [K.MARK_SET, 0x01, 1 | 8];
const epiece = (smp) => {
    const pad = (7 - (smp.length % 7)) % 7;
    for (const b of K.toStream(smp.concat(new Array(pad).fill(smp[smp.length - 1])))) eb.push(b);
};
epiece(new Array(3000).fill(0));
epiece(K.ookToSamples(eg, false));
epiece(new Array(3000).fill(0));
eb.push(K.MARK_GAP);
const es = new K.Stream(24, (ev) => { if (ev.kind === 'fail') console.log('efail ' + ev.text); });
es.feed(eb);
es.flush();
es.leaderboard(0).forEach((r) => console.log(`eboard best=${r.best} fail=${r.fail}`));
"""

# (kind, text the line must contain), in order.
EXPECT = [
    ("cmd", "cmd band=1: A5 11 EE"),
    ("cmd", "cmd ifp=2: A5 22 DD"),
    ("cmd", "cmd offset=15: A5 3F C0"),
    ("cmd", "cmd sens=2: A5 42 BD"),
    ("cmd", "cmd light=1: A5 51 AE"),
    ("cmd", "cmd clear=0: A5 60 9F"),
    ("cmd", "cmd report=0: A5 70 8F"),
    ("cmd", "cmd demod=1: A5 81 7E"),
    # The dashboard feed runs ahead of the decoding, so both records come first.
    ("live", "live 433.92 0 7E2C sens=1 light=0"),
    ("live", "live 433.92 -5 7E2C sens=2 light=1"),
    ("settings", "⚙️ radio: 433.92 MHz FSK, offset +0 kHz, IF 25 kHz (7E2C), SENS MID"),
    ("ok", "✅ TOYOTA   OK  id 00F211A9  28.0 PSI  1 C"),
    ("radio", "📻 radio decoded TOYOTA"),
    ("ok", "✅ TOYOTA   OK  id 00F211A9"),
    ("hint", "💡 The PC decoded a TOYOTA frame that the radio did not"),
    ("fail", "❌ TOYOTA   72/72 bits, CRC FAIL"),
    ("hint", "💡 Whole frames arrive with a bit error"),
    ("partial", "🟠 TOYOTA   "),
    ("partial", "🟠 TOYOTA   "),
    ("hint", "💡 Bursts arrive but break up"),
    ("settings", "⚙️ radio: 433.92 MHz FSK, offset -5 kHz, IF 25 kHz (7E2C), SENS HIGH"),
    ("hint", "💡 Best so far: 433.92 MHz FSK, offset +0 kHz, IF 25 kHz (7E2C), SENS MID "
             "(TOYOTA decoded OK)"),
    ("ok", "✅ PMV-107J OK  id 0ABCDEF5  32.0 PSI  25 C"),
    ("hint", "💡 This sensor is a Pacific PMV-107J"),
    ("hint", "💡 Nothing above noise for 30 s"),
    # Ranked by results per minute; under 15 s a setting is listed but not ranked.
    ("board15", "board15 1: 433.92 -5 HIGH ok=1 fail=0 partial=0 best=100 ranked=true sec=32"),
    ("board15", "board15 2: 433.92 0 MID ok=2 fail=1 partial=2 best=100 ranked=false sec=3"),
    ("board1", "board1 1: 433.92 0 MID ok=2"),
    ("board1", "board1 2: 433.92 -5 HIGH ok=1"),
    # OOK, carrier on as 1 (+) then as 0 (-).
    ("olive", "olive demod=OOK ook=1"),
    ("osettings", "⚙️ radio: 433.92 MHz OOK, offset +0 kHz, IF 25 kHz (7E2C), SENS MID"),
    ("ook", "✅ SCHRADER OK  id 03A38B2  47.9 PSI  12 C (+)"),
    ("ofail", "❌ SCHRADER 68/68 bits, CHECK FAIL"),
    ("ook", "✅ BMW      OK  id 100C56  29.0 PSI  20 C (+)"),
    ("ook", "✅ EG53     OK  id ABCDEF  47.9 PSI  20 C (+)"),
    ("ook", "✅ NISSAN   OK  id 123456  32.0 PSI (+)"),
    ("ook", "✅ SCHRADER OK  id 03A38B2  47.9 PSI  12 C (-)"),
    ("ofail", "❌ SCHRADER 68/68 bits, CHECK FAIL"),
    ("ook", "✅ BMW      OK  id 100C56  29.0 PSI  20 C (-)"),
    ("ook", "✅ EG53     OK  id ABCDEF  47.9 PSI  20 C (-)"),
    ("ook", "✅ NISSAN   OK  id 123456  32.0 PSI (-)"),
    # No 'missed' hint in OOK (the radio cannot decode OOK), and chatter is no frame.
    ("ostats", 'ostats {"SCHRADER":[4,2,99],"BMW":[2,2,99],"EG53":[2,2,99],"NISSAN":[2,2,99]}'),
    # Samples before an FSK-to-OOK switch are decoded as FSK.
    ("dok", "✅ TOYOTA   OK  id 00F211A9"),
    # Only the intact record is taken: SENS HIGH, FSK.
    ("flive", "flive sens=2 ook=0"),
    ("flives", "flives 1"),
    # A failed EG53 check is not a full frame on the leaderboard.
    ("efail", "❌ EG53     120/120 bits, CHECK FAIL"),
    ("eboard", "eboard best=99 fail=1"),
]


def decoder_js() -> str:
    """Pull the decoder script out of tools/kplug.html.

    @return JavaScript source.
    """
    with open(os.path.join(ROOT, "tools", "kplug.html"), encoding="utf-8") as fh:
        html = fh.read()
    body = html.split("<!-- DECODER START", 1)[1].split("<!-- DECODER END", 1)[0]
    return body.split("<script>", 1)[1].rsplit("</script>", 1)[0]


def main() -> int:
    """Run the checks.

    @return 0 when they pass or Node is missing, else 1.
    """
    node = shutil.which("node")
    if not node:
        print("  skip  Node not found: kplug.html decoder not run")
        return 0
    with tempfile.TemporaryDirectory() as tmp:
        js_path = os.path.join(tmp, "kplug_decoder.js")
        with open(js_path, "w", encoding="utf-8") as fh:
            fh.write(decoder_js())
        res = subprocess.run([node, "-e", NODE_SCRIPT, js_path], capture_output=True,
                             encoding="utf-8", check=False)
    lines = [ln.rstrip("\r") for ln in res.stdout.splitlines()]
    fails = 0
    if res.returncode != 0:
        print("FAIL  node exited " + str(res.returncode) + "\n" + res.stderr)
        return 1
    selfs = [ln for ln in lines if ln.startswith("SELF ")]
    ok = len(selfs) == 6 and all(ln.startswith("SELF ok") for ln in selfs)
    print(f"  {'ok' if ok else 'FAIL'}  kplug.html self-test ({len(selfs)} checks)")
    fails += 0 if ok else 1
    events = [ln for ln in lines if not ln.startswith("SELF ")]
    if any(ln.startswith("ohint") and "radio did not" in ln for ln in events):
        print("FAIL  OOK gave the radio-missed hint, but the radio cannot decode OOK")
        fails += 1
    i = 0
    for kind, text in EXPECT:
        while i < len(events) and not (events[i].startswith(kind + " ") and text in events[i]):
            i += 1
        found = i < len(events)
        print(f"  {'ok' if found else 'FAIL'}  {kind}: {text[:60]}")
        if not found:
            fails += 1
            i = 0
    if fails:
        print("decoder output:\n  " + "\n  ".join(events))
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
