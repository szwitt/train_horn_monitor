"""Turns Particle webhook calls into stored node events and heartbeats.

Each node publishes one "crossing" event per train. Events from the four
nodes that describe the same train are grouped into one crossing by time.
"""

import json
from datetime import datetime

# Node events whose reference times are this close belong to the same train.
GROUP_WINDOW_MS = 60_000


def parse_published_at(value):
    """Particle's published_at (ISO 8601, UTC) to epoch milliseconds."""
    return int(datetime.fromisoformat(value.replace("Z", "+00:00")).timestamp() * 1000)


def event_t0_ms(data):
    return int(data["t0"]) * 1000 + int(data.get("t0_ms", 0))


def event_ref_ms(data):
    """Best single time for the train at this node: train start, else horn, else bell."""
    t0 = event_t0_ms(data)
    for key in ("train", "horn", "bell"):
        if key in data:
            return t0 + int(data[key]["s"])
    return t0


def store_crossing_event(conn, device_id, data, received_at):
    """Stores one node's crossing report and returns the crossing id it joined."""
    ref = event_ref_ms(data)
    node = int(data["node"])
    rows = conn.execute(
        "SELECT id, ref_ms FROM crossings WHERE ref_ms BETWEEN ? AND ? ORDER BY ABS(ref_ms - ?)",
        (ref - GROUP_WINDOW_MS, ref + GROUP_WINDOW_MS, ref),
    ).fetchall()
    crossing_id = None
    for row in rows:
        taken = conn.execute(
            "SELECT 1 FROM node_events WHERE crossing_id = ? AND node = ?", (row["id"], node)
        ).fetchone()
        if not taken:
            crossing_id = row["id"]
            break
    if crossing_id is None:
        crossing_id = conn.execute("INSERT INTO crossings (ref_ms) VALUES (?)", (ref,)).lastrowid

    conn.execute(
        "INSERT INTO node_events (crossing_id, device_id, node, dist_ft, t0_ms, ref_ms, received_at, payload)"
        " VALUES (?, ?, ?, ?, ?, ?, ?, ?)",
        (crossing_id, device_id, node, int(data.get("dist_ft", 0)), event_t0_ms(data), ref, received_at,
         json.dumps(data, separators=(",", ":"))),
    )
    conn.commit()
    return crossing_id


def store_heartbeat(conn, device_id, data, ts_ms):
    conn.execute(
        "INSERT INTO heartbeats (device_id, node, dist_ft, ts_ms, amb, leq, soc, busy) VALUES (?, ?, ?, ?, ?, ?, ?, ?)",
        (device_id, int(data["node"]), int(data.get("dist_ft", 0)), ts_ms, data.get("amb"), data.get("leq"),
         data.get("soc"), 1 if data.get("busy") else 0),
    )
    conn.commit()


def handle_webhook(conn, body):
    """Handles one Particle webhook body. Returns (kind, id or None)."""
    name = body.get("event", "")
    raw = body.get("data", "")
    data = json.loads(raw) if isinstance(raw, str) else raw
    device_id = body.get("coreid", "")
    published_at = body.get("published_at", "")

    if name == "crossing":
        return "crossing", store_crossing_event(conn, device_id, data, published_at)
    if name == "heartbeat":
        store_heartbeat(conn, device_id, data, parse_published_at(published_at))
        return "heartbeat", None
    raise ValueError(f"unknown event {name!r}")
