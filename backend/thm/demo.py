"""Fills a database with a month of made-up data so the dashboard can be previewed.

    python -m thm.demo demo.sqlite3

Then: THM_DB=demo.sqlite3 THM_TOKEN=x flask --app "thm.app:create_app()" run
The numbers are synthetic, not measurements.
"""

import random
import sys
from datetime import datetime, timedelta
from zoneinfo import ZoneInfo

from . import db, ingest

NODES = ((1, 50), (2, 100), (3, 200), (4, 400))


def seed(path, tz_name="America/Los_Angeles", days=30, seed_value=7):
    rnd = random.Random(seed_value)
    tz = ZoneInfo(tz_name)
    conn = db.connect(path)
    today = datetime.now(tz).replace(hour=0, minute=0, second=0, microsecond=0)
    start = today - timedelta(days=days)

    # Heartbeats: quieter at night, louder by day, and each node quieter with distance
    t = start
    while t < datetime.now(tz):
        t += timedelta(minutes=15)
        busy = 7 <= t.hour < 22
        for node, dist in NODES:
            base = (58 if busy else 50) - 3 * (node - 1) + rnd.gauss(0, 1.5)
            if rnd.random() < 0.3:  # a train in this interval lifts the average
                base += 6 + rnd.random() * 4
            ts_ms = int(t.timestamp() * 1000)
            ingest.store_heartbeat(conn, f"dev{node}", {"node": node, "dist_ft": dist, "amb": base - 6,
                                                       "leq": round(base, 1), "soc": 90, "busy": False}, ts_ms)

    # About 30 trains a day, spread across the clock
    for d in range(days + 1):
        day = start + timedelta(days=d)
        for _ in range(30):
            when = day + timedelta(seconds=rnd.randrange(86_400))
            if when > datetime.now(tz):
                continue
            t0 = int(when.timestamp())
            lead_ms = int(rnd.choice([16, 17, 18, 19, 13, 22, 9]) * 1000 + rnd.randrange(900))
            arrival = 30_000
            horn_start = arrival - lead_ms
            pattern = rnd.random() > 0.15
            blasts = ([[horn_start, 2500], [horn_start + 3500, 2500], [horn_start + 7000, 1000],
                       [horn_start + 9000, max(1500, arrival - horn_start - 9000)]] if pattern
                      else [[horn_start, 1000], [horn_start + 2000, 1000], [horn_start + 4000, 1000]])
            for node, dist in NODES:
                fall = 3 * (node - 1)
                data = {"v": 1, "node": node, "dist_ft": dist, "t0": t0, "t0_ms": 0,
                        "amb": round(52 - fall + rnd.gauss(0, 1), 1),
                        "peak": round(100 - 1.5 * fall + rnd.gauss(0, 1), 1),
                        "sel": round(108 - 1.5 * fall + rnd.gauss(0, 1), 1), "act": 60000,
                        "horn": {"s": horn_start, "e": blasts[-1][0] + blasts[-1][1], "pat": pattern,
                                 "peak": round(97 - 2 * fall + rnd.gauss(0, 1.5), 1), "n": len(blasts),
                                 "more": 0, "b": blasts},
                        "train": {"s": arrival + rnd.randrange(-400, 400), "e": arrival + 20_000 + rnd.randrange(20_000),
                                  "leq": round(84 - fall, 1), "max": round(88 - fall, 1)}}
                if node <= 2:
                    data["bell"] = {"s": arrival - 24_000 - rnd.randrange(8_000), "e": arrival + 25_000,
                                    "n": 160, "iv": 340}
                ingest.store_crossing_event(conn, f"dev{node}", data, when.isoformat())
    conn.close()


if __name__ == "__main__":
    seed(sys.argv[1] if len(sys.argv) > 1 else "demo.sqlite3")
