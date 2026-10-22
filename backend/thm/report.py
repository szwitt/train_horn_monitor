"""Plain-language summary of one crossing."""

from datetime import datetime
from zoneinfo import ZoneInfo

STATUS_WORDS = {
    "pass": "Pass",
    "fail": "FAIL",
    "unverified": "Unverified",
    "not_detected": "Not detected",
    "indicator": "Indicator",
}

RULE_NAMES = {
    "horn_timing": "Horn timing (49 CFR 222.21(b))",
    "horn_pattern": "Horn pattern (49 CFR 222.21(a))",
    "horn_until_arrival": "Horn held to crossing (49 CFR 222.21(a))",
    "horn_level": "Horn loudness (49 CFR 229.129)",
    "warning_time": "Warning time (49 CFR 234.225)",
}


def crossing_text(crossing_id, ref_ms, summary, tz_name):
    tz = ZoneInfo(tz_name)
    when = datetime.fromtimestamp(ref_ms / 1000, tz).strftime("%Y-%m-%d %H:%M:%S %Z")
    lines = [f"Crossing {crossing_id} at {when}"]
    if summary["train_duration_s"] is not None:
        lines.append(f"Train took {summary['train_duration_s']:.0f} s to pass")
    if summary["horn_lead_s"] is not None:
        lines.append(f"Horn lead {summary['horn_lead_s']:.1f} s")
    if summary["bell_lead_s"] is not None:
        lines.append(f"Bell lead {summary['bell_lead_s']:.1f} s")
    if summary["lights_lead_s"] is not None:
        lines.append(f"Lights lead {summary['lights_lead_s']:.1f} s")
    if summary["lights_duration_s"] is not None:
        lines.append(f"Lights flashed for {summary['lights_duration_s']:.0f} s")
    lines.append("")
    lines.append("Levels by distance:")
    for n in summary["nodes"]:
        parts = [f"  {n['dist_ft']} ft (node {n['node']}):"]
        if n["ambient_dba"] is not None:
            parts.append(f"ambient {n['ambient_dba']:.1f}")
        if n["peak_dba"] is not None:
            parts.append(f"peak {n['peak_dba']:.1f}")
        if n["sel_dba"] is not None:
            parts.append(f"SEL {n['sel_dba']:.1f}")
        lines.append(" ".join(parts) + " dB(A)")
    lines.append("")
    lines.append("FRA checks:")
    for c in summary["checks"]:
        lines.append(f"  {RULE_NAMES.get(c['rule'], c['rule'])}: {STATUS_WORDS[c['status']]}. {c['detail']}")
    return "\n".join(lines)
