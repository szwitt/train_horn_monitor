"""Community noise metrics for the quiet zone case."""

import json
import math
from datetime import date, datetime, time, timedelta
from zoneinfo import ZoneInfo

NIGHT_START_HOUR = 22  # 10 pm
NIGHT_END_HOUR = 7  # 7 am
NIGHT_PENALTY_DB = 10.0
HEARTBEAT_MINUTES = 15
FULL_DAY_INTERVALS = 24 * 60 // HEARTBEAT_MINUTES
MIN_COVERAGE = 0.8  # share of a day's heartbeats needed for a DNL value


def is_night(dt):
    return dt.hour >= NIGHT_START_HOUR or dt.hour < NIGHT_END_HOUR


def day_bounds_ms(day, tz):
    start = datetime.combine(day, time(0), tzinfo=tz)
    end = start + timedelta(days=1)
    return int(start.timestamp() * 1000), int(end.timestamp() * 1000)


def dnl(heartbeats, day, tz):
    """Day-night level (Ldn) for one node and one local day.

    heartbeats: iterable of (ts_ms, leq) where leq covers the 15 minutes
    ending at ts_ms. Returns (ldn or None, intervals used).
    """
    start_ms, end_ms = day_bounds_ms(day, tz)
    total = 0.0
    count = 0
    for ts_ms, leq in heartbeats:
        if leq is None:
            continue
        mid_ms = ts_ms - HEARTBEAT_MINUTES * 30_000
        if not start_ms <= mid_ms < end_ms:
            continue
        mid = datetime.fromtimestamp(mid_ms / 1000, tz)
        level = leq + (NIGHT_PENALTY_DB if is_night(mid) else 0.0)
        total += 10 ** (level / 10.0)
        count += 1
    if count < MIN_COVERAGE * FULL_DAY_INTERVALS:
        return None, count
    return 10 * math.log10(total / count), count


def daily_summary(conn, day, tz_name):
    """Per-node DNL plus crossing and night horn counts for a local day."""
    tz = ZoneInfo(tz_name)
    if isinstance(day, str):
        day = date.fromisoformat(day)
    start_ms, end_ms = day_bounds_ms(day, tz)

    nodes = []
    sound_nodes = "SELECT DISTINCT node, dist_ft FROM heartbeats WHERE leq IS NOT NULL ORDER BY dist_ft"
    for row in conn.execute(sound_nodes):
        beats = conn.execute(
            "SELECT ts_ms, leq FROM heartbeats WHERE node = ? AND ts_ms > ? AND ts_ms <= ?",
            (row["node"], start_ms, end_ms + HEARTBEAT_MINUTES * 60_000),
        ).fetchall()
        ldn, used = dnl(((b["ts_ms"], b["leq"]) for b in beats), day, tz)
        nodes.append({
            "node": row["node"],
            "dist_ft": row["dist_ft"],
            "dnl_dba": round(ldn, 1) if ldn is not None else None,
            "intervals": used,
        })

    crossings = conn.execute(
        "SELECT id, ref_ms FROM crossings WHERE ref_ms >= ? AND ref_ms < ?", (start_ms, end_ms)
    ).fetchall()
    night = sum(1 for c in crossings if is_night(datetime.fromtimestamp(c["ref_ms"] / 1000, tz)))
    night_horns = 0
    for c in crossings:
        if not is_night(datetime.fromtimestamp(c["ref_ms"] / 1000, tz)):
            continue
        rows = conn.execute("SELECT payload FROM node_events WHERE crossing_id = ?", (c["id"],)).fetchall()
        if any("horn" in json.loads(r["payload"]) for r in rows):
            night_horns += 1

    return {
        "date": day.isoformat(),
        "timezone": tz_name,
        "crossings": len(crossings),
        "night_crossings": night,
        "night_horn_crossings": night_horns,
        "nodes": nodes,
    }
