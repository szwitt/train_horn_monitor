"""FRA checks for one crossing, built from the node events grouped into it.

Rules (see the design brief for sources):
- 49 CFR 222.21(b): horn starts 15 to 20 s before the locomotive enters the
  crossing (up to 25 s tolerated in good faith); above 60 mph, no more than
  1/4 mile out, which can be under 15 s.
- 49 CFR 222.21(a): long-long-short-long, repeated or prolonged until the
  locomotive occupies the crossing.
- 49 CFR 229.129: 96 to 110 dB(A) at 100 ft forward of the locomotive. A
  wayside reading can only be an indicator.
- 49 CFR 234.225: warning system gives at least 20 s warning.
"""

HORN_MIN_S = 15.0
HORN_MAX_S = 20.0
HORN_GOOD_FAITH_MAX_S = 25.0
QUARTER_MILE_FT = 1320.0
MAX_TIMETABLE_MPH = 79.0  # crossing 085770G
HORN_MIN_AT_MAX_SPEED_S = QUARTER_MILE_FT / (MAX_TIMETABLE_MPH * 5280.0 / 3600.0)  # 11.4 s
HORN_HELD_TOLERANCE_S = 3.0
BELL_MIN_S = 20.0
HORN_LEVEL_MIN_DBA = 96.0
HORN_LEVEL_MAX_DBA = 110.0
HORN_LEVEL_REF_FT = 100

PASS, FAIL, UNVERIFIED, NOT_DETECTED, INDICATOR = "pass", "fail", "unverified", "not_detected", "indicator"


def _abs(event, key, field="s"):
    return event["t0_abs_ms"] + int(event[key][field])


def _nearest(events, key):
    with_key = [e for e in events if key in e]
    return min(with_key, key=lambda e: e.get("dist_ft", 0)) if with_key else None


def _check(rule, status, detail):
    return {"rule": rule, "status": status, "detail": detail}


def blast_letters(horn):
    """Blast sequence as letters, e.g. 'LLSL' (long is 1.5 s or more, as on the node)."""
    return "".join("L" if dur >= 1500 else "S" for _, dur in horn.get("b", []))


def evaluate(events):
    """Returns the crossing summary: timings, per-node levels and FRA checks."""
    train_node = _nearest(events, "train")
    arrival_ms = _abs(train_node, "train") if train_node else None
    horn_events = [e for e in events if "horn" in e]
    horn_start_ms = min((_abs(e, "horn") for e in horn_events), default=None)
    horn_end_ms = max((_abs(e, "horn", "e") for e in horn_events), default=None)
    horn_node = _nearest(events, "horn")
    bell_node = _nearest(events, "bell")
    bell_start_ms = _abs(bell_node, "bell") if bell_node else None

    summary = {
        "arrival_ms": arrival_ms,
        "train_duration_s": None,
        "horn_lead_s": None,
        "bell_lead_s": None,
        "nodes": sorted(
            (
                {
                    "node": e["node"],
                    "dist_ft": e.get("dist_ft"),
                    "ambient_dba": e.get("amb"),
                    "peak_dba": e.get("peak"),
                    "sel_dba": e.get("sel"),
                    "horn_peak_dba": e["horn"].get("peak") if "horn" in e else None,
                    "train_leq_dba": e["train"].get("leq") if "train" in e else None,
                }
                for e in events
            ),
            key=lambda n: n["dist_ft"] or 0,
        ),
        "checks": [],
    }
    checks = summary["checks"]

    if train_node:
        summary["train_duration_s"] = (int(train_node["train"]["e"]) - int(train_node["train"]["s"])) / 1000.0

    # Horn timing
    if arrival_ms is None:
        checks.append(_check("horn_timing", NOT_DETECTED, "No train passage detected, so lead time is unknown"))
    elif horn_start_ms is None:
        checks.append(_check("horn_timing", NOT_DETECTED, "No horn detected before the train arrived"))
    else:
        lead = (arrival_ms - horn_start_ms) / 1000.0
        summary["horn_lead_s"] = round(lead, 1)
        if HORN_MIN_S <= lead <= HORN_MAX_S:
            checks.append(_check("horn_timing", PASS, f"Horn started {lead:.1f} s before arrival (15 to 20 s)"))
        elif HORN_MAX_S < lead <= HORN_GOOD_FAITH_MAX_S:
            checks.append(_check("horn_timing", PASS,
                                 f"Horn started {lead:.1f} s before arrival, within the 25 s good-faith allowance"))
        elif lead > HORN_GOOD_FAITH_MAX_S:
            checks.append(_check("horn_timing", FAIL, f"Horn started {lead:.1f} s before arrival, more than 25 s"))
        elif lead >= HORN_MIN_AT_MAX_SPEED_S:
            checks.append(_check("horn_timing", UNVERIFIED,
                                 f"Horn started {lead:.1f} s before arrival; compliant only if the train was above "
                                 "60 mph and the horn began 1/4 mile out"))
        else:
            checks.append(_check("horn_timing", FAIL,
                                 f"Horn started {lead:.1f} s before arrival, under 15 s and too late even at 79 mph"))

    # Horn pattern
    if horn_node is None:
        checks.append(_check("horn_pattern", NOT_DETECTED, "No horn detected"))
    else:
        agreeing = sum(1 for e in horn_events if e["horn"].get("pat"))
        letters = blast_letters(horn_node["horn"])
        if horn_node["horn"].get("pat") or agreeing:
            checks.append(_check("horn_pattern", PASS,
                                 f"Long-long-short-long heard ({letters} at {horn_node['dist_ft']} ft; "
                                 f"{agreeing} of {len(horn_events)} nodes agree)"))
        else:
            checks.append(_check("horn_pattern", FAIL,
                                 f"Pattern not heard; blasts were {letters or 'none'} at {horn_node['dist_ft']} ft"))

    # Horn held until the train reached the crossing
    if arrival_ms is not None and horn_end_ms is not None:
        gap = (arrival_ms - horn_end_ms) / 1000.0
        if gap <= HORN_HELD_TOLERANCE_S:
            checks.append(_check("horn_until_arrival", PASS, "Horn sounded until the train reached the crossing"))
        else:
            checks.append(_check("horn_until_arrival", FAIL, f"Horn stopped {gap:.1f} s before the train arrived"))

    # Horn loudness indicator from the node nearest 100 ft
    if horn_events:
        ref = min(horn_events, key=lambda e: abs((e.get("dist_ft") or 0) - HORN_LEVEL_REF_FT))
        peak = ref["horn"].get("peak")
        where = f"{peak:.1f} dB(A) at {ref['dist_ft']} ft"
        if peak is not None and peak < HORN_LEVEL_MIN_DBA:
            detail = f"{where}, below the 96 dB(A) minimum (wayside estimate only)"
        elif peak is not None and peak > HORN_LEVEL_MAX_DBA:
            detail = f"{where}, above the 110 dB(A) maximum (wayside estimate only)"
        else:
            detail = f"{where}, within 96 to 110 dB(A) (wayside estimate only)"
        checks.append(_check("horn_level", INDICATOR, detail))

    # Crossing warning time, using the bell as the start of the warning
    if arrival_ms is None:
        checks.append(_check("warning_time", NOT_DETECTED, "No train passage detected"))
    elif bell_start_ms is None:
        checks.append(_check("warning_time", NOT_DETECTED, "Crossing bell not detected"))
    else:
        lead = (arrival_ms - bell_start_ms) / 1000.0
        summary["bell_lead_s"] = round(lead, 1)
        status = PASS if lead >= BELL_MIN_S else FAIL
        checks.append(_check("warning_time", status, f"Bell started {lead:.1f} s before arrival (at least 20 s)"))

    return summary
