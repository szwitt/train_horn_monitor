import json
from datetime import date, datetime, timedelta, timezone
from zoneinfo import ZoneInfo

import pytest

from thm import compliance, db, ingest, metrics
from thm.app import create_app

TOKEN = "test-token"
TZ = "America/Los_Angeles"
# 2026-10-19 23:10:00 Pacific (night), as epoch seconds
NIGHT_T0 = int(datetime(2026, 10, 19, 23, 10, tzinfo=ZoneInfo(TZ)).timestamp())


def node_report(node, dist_ft, t0=NIGHT_T0, horn_start=16_700, arrival=32_700, bell_start=0, pattern=True,
                blasts=None, horn_peak=98.0, horn_end=None, train=True, bell=True, horn=True):
    """A crossing report shaped like the firmware's, times relative to t0."""
    blasts = blasts or [[horn_start, 2500], [horn_start + 3500, 2500], [horn_start + 7000, 1000],
                        [horn_start + 9000, 7000]]
    data = {"v": 1, "node": node, "dist_ft": dist_ft, "t0": t0, "t0_ms": 0, "amb": 50.0 - node,
            "peak": 99.0 - 3 * node, "sel": 110.0 - 3 * node, "act": 60000}
    if bell:
        data["bell"] = {"s": bell_start, "e": 59000, "n": 170, "iv": 340}
    if horn:
        data["horn"] = {"s": horn_start, "e": horn_end if horn_end is not None else arrival, "pat": pattern,
                        "peak": horn_peak - 6 * (node - 1), "n": len(blasts), "more": 0, "b": blasts}
    if train:
        data["train"] = {"s": arrival, "e": arrival + 25_000, "leq": 85.0 - 3 * node, "max": 88.0 - 3 * node}
    return data


def webhook(event, data, device="dev1", published_at="2026-10-20T06:12:00.000Z"):
    return {"event": event, "data": json.dumps(data), "coreid": device, "published_at": published_at}


@pytest.fixture
def client(tmp_path):
    app = create_app(db_path=str(tmp_path / "t.sqlite3"), token=TOKEN, tz_name=TZ)
    return app.test_client()


def post(client, body, token=TOKEN):
    return client.post("/particle", json=body, headers={"X-THM-Token": token})


# ---- Grouping ----

def test_four_nodes_group_into_one_crossing(client):
    ids = set()
    for node, dist in ((1, 50), (2, 100), (3, 200), (4, 400)):
        # Each node's clock and detection differ by up to a second or so
        r = post(client, webhook("crossing", node_report(node, dist, t0=NIGHT_T0 + node % 2), device=f"d{node}"))
        assert r.status_code == 200
        ids.add(r.get_json()["crossing_id"])
    assert len(ids) == 1

    crossings = client.get("/api/crossings").get_json()
    assert len(crossings) == 1
    assert [n["dist_ft"] for n in crossings[0]["nodes"]] == [50, 100, 200, 400]


def test_separate_trains_get_separate_crossings(client):
    a = post(client, webhook("crossing", node_report(1, 50))).get_json()["crossing_id"]
    b = post(client, webhook("crossing", node_report(1, 50, t0=NIGHT_T0 + 600))).get_json()["crossing_id"]
    assert a != b


def test_same_node_twice_in_window_starts_new_crossing(tmp_path):
    conn = db.connect(str(tmp_path / "g.sqlite3"))
    first = ingest.store_crossing_event(conn, "d1", node_report(1, 50), "x")
    second = ingest.store_crossing_event(conn, "d1", node_report(1, 50, t0=NIGHT_T0 + 30), "x")
    assert first != second


def test_rejects_bad_token(client):
    r = post(client, webhook("crossing", node_report(1, 50)), token="wrong")
    assert r.status_code == 401


def test_rejects_unknown_event(client):
    r = post(client, webhook("status", {"node": 1}))
    assert r.status_code == 400


def test_requires_token_configured(tmp_path):
    with pytest.raises(RuntimeError):
        create_app(db_path=str(tmp_path / "x.sqlite3"), token="")


# ---- Compliance ----

def evaluate(*reports):
    events = []
    for r in reports:
        e = dict(r)
        e["t0_abs_ms"] = ingest.event_t0_ms(r)
        events.append(e)
    return compliance.evaluate(events)


def status(summary, rule):
    return next(c["status"] for c in summary["checks"] if c["rule"] == rule)


def test_compliant_crossing():
    s = evaluate(node_report(1, 50), node_report(2, 100), node_report(4, 400, pattern=False))
    assert s["horn_lead_s"] == 16.0
    assert s["bell_lead_s"] == 32.7
    assert s["train_duration_s"] == 25.0
    assert status(s, "horn_timing") == "pass"
    assert status(s, "horn_pattern") == "pass"
    assert status(s, "horn_until_arrival") == "pass"
    assert status(s, "warning_time") == "pass"
    assert status(s, "horn_level") == "indicator"
    level = next(c for c in s["checks"] if c["rule"] == "horn_level")
    assert "at 100 ft" in level["detail"]


@pytest.mark.parametrize("lead_s,expected", [
    (22.0, "pass"),        # good-faith allowance
    (27.0, "fail"),        # too early
    (13.0, "unverified"),  # only legal above 60 mph
    (9.0, "fail"),         # too late even at 79 mph
])
def test_horn_timing_bands(lead_s, expected):
    arrival = 40_000
    s = evaluate(node_report(1, 50, horn_start=arrival - int(lead_s * 1000), arrival=arrival))
    assert status(s, "horn_timing") == expected


def test_wrong_pattern_fails():
    blasts = [[10_000, 1000], [12_000, 1000], [14_000, 1000]]
    s = evaluate(node_report(1, 50, horn_start=10_000, arrival=26_000, pattern=False, blasts=blasts))
    assert status(s, "horn_pattern") == "fail"
    detail = next(c["detail"] for c in s["checks"] if c["rule"] == "horn_pattern")
    assert "SSS" in detail


def test_horn_stopped_early_fails():
    s = evaluate(node_report(1, 50, horn_end=25_000, arrival=32_700))
    assert status(s, "horn_until_arrival") == "fail"


def test_short_warning_time_fails():
    s = evaluate(node_report(1, 50, bell_start=20_000, arrival=32_700))
    assert status(s, "warning_time") == "fail"


def test_no_horn_or_train_not_detected():
    s = evaluate(node_report(1, 50, horn=False, train=False))
    assert status(s, "horn_timing") == "not_detected"
    assert status(s, "horn_pattern") == "not_detected"
    assert status(s, "warning_time") == "not_detected"


def test_summary_text(client):
    post(client, webhook("crossing", node_report(1, 50)))
    text = client.get("/api/crossings/1/summary").get_data(as_text=True)
    assert "2026-10-19 23:10" in text
    assert "Horn timing (49 CFR 222.21(b)): Pass." in text
    assert "50 ft (node 1)" in text


# ---- Day-night level ----

def full_day_heartbeats(day, day_leq, night_leq):
    tz = ZoneInfo(TZ)
    start = datetime(day.year, day.month, day.day, tzinfo=tz)
    beats = []
    for i in range(1, metrics.FULL_DAY_INTERVALS + 1):
        end = start + timedelta(minutes=15 * i)
        mid = end - timedelta(minutes=7.5)
        beats.append((int(end.timestamp() * 1000), night_leq if metrics.is_night(mid) else day_leq))
    return beats


def test_dnl_applies_night_penalty():
    day = date(2026, 10, 19)
    ldn, used = metrics.dnl(full_day_heartbeats(day, 55.0, 55.0), day, ZoneInfo(TZ))
    assert used == 96
    # 15 day hours at 55, 9 night hours at 65: 10*log10((15*10^5.5 + 9*10^6.5)/24) = 61.4
    assert ldn == pytest.approx(61.4, abs=0.1)


def test_dnl_needs_coverage():
    day = date(2026, 10, 19)
    beats = full_day_heartbeats(day, 55.0, 55.0)[:40]
    ldn, used = metrics.dnl(beats, day, ZoneInfo(TZ))
    assert ldn is None and used == 40


def test_daily_endpoint(client):
    day = date(2026, 10, 19)
    for ts_ms, leq in full_day_heartbeats(day, 55.0, 50.0):
        published = datetime.fromtimestamp(ts_ms / 1000, timezone.utc).isoformat().replace("+00:00", "Z")
        hb = {"v": 1, "node": 1, "dist_ft": 50, "amb": 45.0, "leq": leq, "soc": 90, "busy": False}
        assert post(client, webhook("heartbeat", hb, published_at=published)).status_code == 200
    post(client, webhook("crossing", node_report(1, 50)))  # 11:10 pm, with horn

    d = client.get("/api/daily/2026-10-19").get_json()
    assert d["crossings"] == 1
    assert d["night_crossings"] == 1
    assert d["night_horn_crossings"] == 1
    assert d["nodes"][0]["intervals"] == 96
    # 15 day hours at 55, 9 night hours at 50 + 10: 10*log10((15*10^5.5 + 9*10^6)/24) = 57.6
    assert d["nodes"][0]["dnl_dba"] == pytest.approx(57.6, abs=0.1)


# ---- Dashboard and embeds ----

def test_stats_and_horn_hours(tmp_path):
    from thm import analytics
    conn = db.connect(str(tmp_path / "s.sqlite3"))
    ingest.store_crossing_event(conn, "d1", node_report(1, 50), "x")  # 11:10 pm, compliant
    # 9:10 pm (day), horn only 7.7 s before arrival
    ingest.store_crossing_event(conn, "d1", node_report(1, 50, t0=NIGHT_T0 - 7200, horn_start=25_000), "x")
    now = datetime(2026, 10, 19, 23, 59, tzinfo=ZoneInfo(TZ))

    s = analytics.stats(conn, TZ, now)
    assert s["trains_today"] == 2
    assert s["night_horns_7d"] == 1
    assert s["horn_timing_30d"]["pass"] == 1 and s["horn_timing_30d"]["fail"] == 1
    assert s["horn_timing_pass_pct_30d"] == 50

    h = analytics.horn_by_hour(conn, TZ, 30, now)
    assert h["hours"][23]["count"] == 1 and h["hours"][21]["count"] == 1
    assert h["hours"][23]["night"] and not h["hours"][12]["night"]


@pytest.mark.parametrize("path", ["/dashboard", "/embed/stats", "/embed/horn-hours", "/embed/dnl",
                                  "/embed/log?theme=dark", "/crossing/1"])
def test_pages_render(client, path):
    post(client, webhook("crossing", node_report(1, 50)))
    r = client.get(path)
    assert r.status_code == 200
    assert b"<!doctype html>" in r.data


def test_unknown_widget_404(client):
    assert client.get("/embed/nope").status_code == 404


def test_api_allows_cross_origin_reads(client):
    r = client.get("/api/stats")
    assert r.status_code == 200
    assert r.headers["Access-Control-Allow-Origin"] == "*"
    assert "Access-Control-Allow-Origin" not in client.get("/dashboard").headers
