#!/usr/bin/env python3
"""Writes the crossing scenarios as audio files for bench and replay testing.

For each scenario in scenarios.py this writes, into the output folder:
  <name>.wav          mono 16-bit audio to play at the bench unit, or replay on a PC
  <name>.meter.csv    the dB(A) the Gravity meter would read every 125 ms (replay only)
  <name>.expect.json  what the node and the FRA checks should report

The sounds match the host tests: a three-note horn chord in the 250-800 Hz
band, decaying 2.1 kHz bell strikes three times a second, and low rumble
for the train. Standard library only.

Usage: python3 tools/bench/make_scenarios.py [out_dir] [--rate 22050] [--only name,...]
"""

import argparse
import array
import json
import math
import os
import random
import sys
import wave

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from scenarios import ARRIVAL_S, SCENARIOS, duration  # noqa: E402

HORN_HZ = (311.0, 370.0, 466.0)
BELL_HZ = 2100.0
BELL_PERIOD_S = 0.33
METER_STEP_S = 0.125

# Levels the meter reads from each source alone, dB(A)
AMBIENT_DBA = 50.0
BELL_DBA = 72.0
TRAIN_DBA = 85.0
HORN_DBA = 98.0


def _in(spans, t):
    return any(a <= t < b for a, b in spans)


def render(s, rate):
    """Returns (samples as floats, peak) for one scenario."""
    rng = random.Random(42)
    n = int(duration(s) * rate)
    bell_on, bell_off = s["bell"]
    train = (ARRIVAL_S, ARRIVAL_S + 40.0) if s["train"] else None
    blasts = s["blasts"]
    # Rumble low-pass coefficient scaled so its bandwidth doesn't depend on the rate
    alpha = 1.0 - math.exp(-2 * math.pi * 160.0 / rate)
    rumble = 0.0
    out = array.array("f", bytes(4 * n))
    tau = 2 * math.pi
    for i in range(n):
        t = i / rate
        v = 0.002 * rng.gauss(0.0, 1.0)
        if blasts and _in(blasts, t):
            v += 0.15 * (math.sin(tau * HORN_HZ[0] * t) + math.sin(tau * HORN_HZ[1] * t) +
                         math.sin(tau * HORN_HZ[2] * t))
        if bell_on <= t < bell_off:
            since = math.fmod(t - bell_on, BELL_PERIOD_S)
            v += 0.3 * math.exp(-since * 12.0) * math.sin(tau * BELL_HZ * t)
        if train and train[0] <= t < train[1]:
            rumble += alpha * (rng.gauss(0.0, 1.0) - rumble)
            v += 1.2 * rumble
        out[i] = v
    return out


def meter_trace(s):
    """(t_ms, dBA) every 125 ms: energy sum of the sources sounding at that moment."""
    bell_on, bell_off = s["bell"]
    rows = []
    steps = int(duration(s) / METER_STEP_S)
    for k in range(steps):
        t = k * METER_STEP_S
        e = 10 ** (AMBIENT_DBA / 10)
        if bell_on <= t < bell_off:
            e += 10 ** (BELL_DBA / 10)
        if s["train"] and ARRIVAL_S <= t < ARRIVAL_S + 40.0:
            e += 10 ** (TRAIN_DBA / 10)
        if s["blasts"] and _in(s["blasts"], t):
            e += 10 ** (HORN_DBA / 10)
        rows.append((int(round(t * 1000)), 10 * math.log10(e)))
    return rows


def write_wav(path, samples, rate):
    peak = max(1e-9, max(abs(v) for v in samples))
    scale = 0.89 * 32767 / peak  # about -1 dBFS
    pcm = array.array("h", (int(v * scale) for v in samples))
    if sys.byteorder == "big":
        pcm.byteswap()
    with wave.open(path, "wb") as w:
        w.setnchannels(1)
        w.setsampwidth(2)
        w.setframerate(rate)
        w.writeframes(pcm.tobytes())


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("out_dir", nargs="?", default="bench-scenarios")
    ap.add_argument("--rate", type=int, default=22050, help="audio sample rate in Hz (default 22050)")
    ap.add_argument("--only", help="comma-separated scenario names")
    args = ap.parse_args(argv)

    wanted = set(args.only.split(",")) if args.only else None
    os.makedirs(args.out_dir, exist_ok=True)
    for s in SCENARIOS:
        if wanted and s["name"] not in wanted:
            continue
        base = os.path.join(args.out_dir, s["name"])
        write_wav(base + ".wav", render(s, args.rate), args.rate)
        with open(base + ".meter.csv", "w") as f:
            f.write("t_ms,dba\n")
            f.writelines(f"{t},{d:.1f}\n" for t, d in meter_trace(s))
        with open(base + ".expect.json", "w") as f:
            json.dump({"name": s["name"], "about": s["about"], "arrival_s": ARRIVAL_S,
                       "duration_s": duration(s), **s["expect"]}, f, indent=2)
        print(f"{s['name']:18s} {duration(s):5.0f} s  {s['about']}")


if __name__ == "__main__":
    main()
