#!/usr/bin/env python3
"""Replays every crossing scenario through the node's detectors and the
backend's FRA checks, and compares the results with what each scenario
expects. No hardware needed.

  python3 tools/bench/run_scenarios.py [scenario_dir]

Builds test/replay if needed and generates any missing scenario files.
Exits non-zero if any scenario doesn't match.
"""

import json
import os
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(os.path.dirname(HERE))
sys.path.insert(0, HERE)
sys.path.insert(0, os.path.join(ROOT, "backend"))

from scenarios import SCENARIOS  # noqa: E402
from thm import compliance  # noqa: E402

LEAD_TOLERANCE_S = 1.5


def replay(scenario_dir, name):
    base = os.path.join(scenario_dir, name)
    out = subprocess.run([os.path.join(ROOT, "test", "replay"), base + ".wav", base + ".meter.csv"],
                         check=True, capture_output=True, text=True)
    events = []
    for line in out.stdout.splitlines():
        e = json.loads(line)
        e["t0_abs_ms"] = e["t0"] * 1000 + e["t0_ms"]
        events.append(e)
    return events


def compare(expect, events):
    """Returns a list of mismatches (empty when the scenario passes)."""
    if len(events) != 1:
        return [f"expected 1 crossing report, got {len(events)}"]
    e = events[0]
    problems = []
    for key in ("bell", "horn", "train"):
        if (key in e) != expect[key]:
            problems.append(f"{key} {'missing' if expect[key] else 'detected but not expected'}")
    if "pattern" in expect and "horn" in e and bool(e["horn"].get("pat")) != expect["pattern"]:
        problems.append(f"horn pattern {'not ' if expect['pattern'] else ''}found "
                        f"({compliance.blast_letters(e['horn']) or 'no blasts'})")

    summary = compliance.evaluate(events)
    for key in ("horn_lead_s", "bell_lead_s"):
        if key in expect:
            got = summary.get(key)
            if got is None or abs(got - expect[key]) > LEAD_TOLERANCE_S:
                problems.append(f"{key} {got} (expected about {expect[key]})")
    statuses = {c["rule"]: c["status"] for c in summary["checks"]}
    for rule, want in expect["checks"].items():
        if statuses.get(rule) != want:
            problems.append(f"{rule} {statuses.get(rule)} (expected {want})")
    return problems


def main():
    scenario_dir = sys.argv[1] if len(sys.argv) > 1 else os.path.join(ROOT, "bench-scenarios")
    subprocess.run(["make", "-s", "-C", os.path.join(ROOT, "test"), "replay"], check=True)
    missing = [s["name"] for s in SCENARIOS if not os.path.exists(os.path.join(scenario_dir, s["name"] + ".wav"))]
    if missing:
        subprocess.run([sys.executable, os.path.join(HERE, "make_scenarios.py"), scenario_dir,
                        "--only", ",".join(missing)], check=True)

    failed = 0
    for s in SCENARIOS:
        with open(os.path.join(scenario_dir, s["name"] + ".expect.json")) as f:
            expect = json.load(f)
        problems = compare(expect, replay(scenario_dir, s["name"]))
        print(f"{'ok  ' if not problems else 'FAIL'} {s['name']:18s} {s['about']}")
        for p in problems:
            print(f"       {p}")
        failed += bool(problems)
    print(f"\n{len(SCENARIOS) - failed} of {len(SCENARIOS)} scenarios match")
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())
