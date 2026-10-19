"""SQLite storage for node events, heartbeats and grouped crossings."""

import json
import sqlite3

SCHEMA = """
CREATE TABLE IF NOT EXISTS crossings (
    id INTEGER PRIMARY KEY,
    ref_ms INTEGER NOT NULL           -- reference time used to group node events
);
CREATE INDEX IF NOT EXISTS crossings_ref ON crossings(ref_ms);

CREATE TABLE IF NOT EXISTS node_events (
    id INTEGER PRIMARY KEY,
    crossing_id INTEGER NOT NULL REFERENCES crossings(id),
    device_id TEXT NOT NULL,
    node INTEGER NOT NULL,
    dist_ft INTEGER NOT NULL,
    t0_ms INTEGER NOT NULL,
    ref_ms INTEGER NOT NULL,
    received_at TEXT NOT NULL,
    payload TEXT NOT NULL             -- the node's crossing JSON, as published
);
CREATE INDEX IF NOT EXISTS node_events_crossing ON node_events(crossing_id);

CREATE TABLE IF NOT EXISTS heartbeats (
    id INTEGER PRIMARY KEY,
    device_id TEXT NOT NULL,
    node INTEGER NOT NULL,
    dist_ft INTEGER NOT NULL,
    ts_ms INTEGER NOT NULL,           -- end of the interval the Leq covers
    amb REAL,
    leq REAL,
    soc REAL,
    busy INTEGER
);
CREATE INDEX IF NOT EXISTS heartbeats_node_ts ON heartbeats(node, ts_ms);
"""


def connect(path):
    conn = sqlite3.connect(path)
    conn.row_factory = sqlite3.Row
    conn.execute("PRAGMA foreign_keys = ON")
    conn.execute("PRAGMA journal_mode = WAL")  # several web workers share the file
    conn.executescript(SCHEMA)
    return conn


def event_from_row(row):
    """A node event as a dict: the published payload plus its stored columns."""
    event = json.loads(row["payload"])
    event["device_id"] = row["device_id"]
    event["t0_abs_ms"] = row["t0_ms"]
    return event
