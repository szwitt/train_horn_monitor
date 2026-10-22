"""Aggregates behind the dashboard and the embeddable widgets."""

import json
from datetime import datetime, timedelta
from zoneinfo import ZoneInfo

from . import compliance, db, metrics


def _crossing_events(conn, crossing_id):
    rows = conn.execute("SELECT * FROM node_events WHERE crossing_id = ?", (crossing_id,)).fetchall()
    return [db.event_from_row(r) for r in rows]


def _crossings_between(conn, start_ms, end_ms):
    return conn.execute(
        "SELECT id, ref_ms FROM crossings WHERE ref_ms >= ? AND ref_ms < ? ORDER BY ref_ms", (start_ms, end_ms)
    ).fetchall()


def _has_horn(conn, crossing_id):
    rows = conn.execute("SELECT payload FROM node_events WHERE crossing_id = ?", (crossing_id,)).fetchall()
    return any("horn" in json.loads(r["payload"]) for r in rows)


def stats(conn, tz_name, now=None):
    """Headline counters: trains today, night horns this week, horn timing results."""
    tz = ZoneInfo(tz_name)
    now = now or datetime.now(tz)
    today = now.astimezone(tz).date()
    day_start, day_end = metrics.day_bounds_ms(today, tz)
    week_start = day_start - 6 * 86_400_000
    month_start = day_start - 29 * 86_400_000

    trains_today = len(_crossings_between(conn, day_start, day_end))

    night_horns_7d = 0
    for c in _crossings_between(conn, week_start, day_end):
        if metrics.is_night(datetime.fromtimestamp(c["ref_ms"] / 1000, tz)) and _has_horn(conn, c["id"]):
            night_horns_7d += 1

    timing = {"pass": 0, "fail": 0, "unverified": 0, "not_detected": 0}
    for c in _crossings_between(conn, month_start, day_end):
        summary = compliance.evaluate(_crossing_events(conn, c["id"]))
        for check in summary["checks"]:
            if check["rule"] == "horn_timing":
                timing[check["status"]] = timing.get(check["status"], 0) + 1
    judged = timing["pass"] + timing["fail"] + timing["unverified"]

    return {
        "date": today.isoformat(),
        "timezone": tz_name,
        "trains_today": trains_today,
        "night_horns_7d": night_horns_7d,
        "horn_timing_30d": timing,
        "horn_timing_pass_pct_30d": round(100.0 * timing["pass"] / judged) if judged else None,
    }


def horn_by_hour(conn, tz_name, days=30, now=None):
    """Crossings with the horn sounded, by local hour of day, over the last N days."""
    tz = ZoneInfo(tz_name)
    now = now or datetime.now(tz)
    today = now.astimezone(tz).date()
    _, end_ms = metrics.day_bounds_ms(today, tz)
    start_ms = end_ms - days * 86_400_000
    counts = [0] * 24
    for c in _crossings_between(conn, start_ms, end_ms):
        if _has_horn(conn, c["id"]):
            counts[datetime.fromtimestamp(c["ref_ms"] / 1000, tz).hour] += 1
    return {
        "days": days,
        "timezone": tz_name,
        "hours": [{"hour": h, "count": counts[h], "night": metrics.is_night(datetime(2000, 1, 1, h))}
                  for h in range(24)],
    }


def dnl_series(conn, tz_name, days=30, now=None):
    """Day-night level per node per day (None where coverage was too thin)."""
    tz = ZoneInfo(tz_name)
    now = now or datetime.now(tz)
    last = now.astimezone(tz).date() - timedelta(days=1)  # today isn't complete yet
    dates = [last - timedelta(days=i) for i in range(days - 1, -1, -1)]
    nodes = conn.execute(
        "SELECT DISTINCT node, dist_ft FROM heartbeats WHERE leq IS NOT NULL ORDER BY dist_ft"  # sound nodes only
    ).fetchall()
    series = []
    for n in nodes:
        start_ms, _ = metrics.day_bounds_ms(dates[0], tz)
        _, end_ms = metrics.day_bounds_ms(dates[-1], tz)
        beats = conn.execute(
            "SELECT ts_ms, leq FROM heartbeats WHERE node = ? AND ts_ms > ? AND ts_ms <= ?",
            (n["node"], start_ms, end_ms + metrics.HEARTBEAT_MINUTES * 60_000),
        ).fetchall()
        pairs = [(b["ts_ms"], b["leq"]) for b in beats]
        values = []
        for d in dates:
            ldn, _ = metrics.dnl(pairs, d, tz)
            values.append(round(ldn, 1) if ldn is not None else None)
        series.append({"node": n["node"], "dist_ft": n["dist_ft"], "values": values})
    return {"timezone": tz_name, "dates": [d.isoformat() for d in dates], "series": series}


def crossing_log(conn, tz_name, limit=50):
    """Most recent crossings with their key numbers and check results."""
    tz = ZoneInfo(tz_name)
    rows = conn.execute("SELECT id, ref_ms FROM crossings ORDER BY ref_ms DESC LIMIT ?", (limit,)).fetchall()
    log = []
    for r in rows:
        s = compliance.evaluate(_crossing_events(conn, r["id"]))
        when = datetime.fromtimestamp(r["ref_ms"] / 1000, tz)
        nearest = s["nodes"][0] if s["nodes"] else {}
        log.append({
            "id": r["id"],
            "time": when.isoformat(),
            "night": metrics.is_night(when),
            "train_duration_s": s["train_duration_s"],
            "horn_lead_s": s["horn_lead_s"],
            "bell_lead_s": s["bell_lead_s"],
            "lights_lead_s": s["lights_lead_s"],
            "peak_dba": nearest.get("peak_dba"),
            "peak_dist_ft": nearest.get("dist_ft"),
            "checks": {c["rule"]: c["status"] for c in s["checks"]},
        })
    return log

