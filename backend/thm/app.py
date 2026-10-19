"""HTTP service: receives the Particle webhook and serves crossing results.

Environment:
  THM_DB     SQLite file (default thm.sqlite3)
  THM_TOKEN  shared secret the Particle webhook sends as X-THM-Token (required)
  THM_TZ     local timezone for night hours and daily metrics (default America/Los_Angeles)
"""

import hmac
import json
import os

from flask import Flask, abort, g, jsonify, request

from . import compliance, db, ingest, metrics, report


def create_app(db_path=None, token=None, tz_name=None):
    app = Flask(__name__)
    app.config["DB_PATH"] = db_path or os.environ.get("THM_DB", "thm.sqlite3")
    app.config["TOKEN"] = token if token is not None else os.environ.get("THM_TOKEN", "")
    app.config["TZ"] = tz_name or os.environ.get("THM_TZ", "America/Los_Angeles")
    if not app.config["TOKEN"]:
        raise RuntimeError("THM_TOKEN must be set so only your Particle webhook can post events")

    def conn():
        if "conn" not in g:
            g.conn = db.connect(app.config["DB_PATH"])
        return g.conn

    @app.teardown_appcontext
    def close(_exc):
        c = g.pop("conn", None)
        if c is not None:
            c.close()

    def crossing_detail(row):
        rows = conn().execute(
            "SELECT * FROM node_events WHERE crossing_id = ? ORDER BY dist_ft", (row["id"],)
        ).fetchall()
        events = [db.event_from_row(r) for r in rows]
        summary = compliance.evaluate(events)
        return {"id": row["id"], "ref_ms": row["ref_ms"], **summary}

    @app.post("/particle")
    def particle():
        sent = request.headers.get("X-THM-Token", "")
        if not hmac.compare_digest(sent, app.config["TOKEN"]):
            abort(401)
        body = request.get_json(silent=True) or request.form.to_dict()
        try:
            kind, crossing_id = ingest.handle_webhook(conn(), body)
        except (ValueError, KeyError, TypeError, json.JSONDecodeError) as exc:
            return jsonify({"error": str(exc)}), 400
        return jsonify({"kind": kind, "crossing_id": crossing_id})

    @app.get("/api/crossings")
    def crossings():
        limit = min(int(request.args.get("limit", 50)), 500)
        rows = conn().execute("SELECT * FROM crossings ORDER BY ref_ms DESC LIMIT ?", (limit,)).fetchall()
        return jsonify([crossing_detail(r) for r in rows])

    @app.get("/api/crossings/<int:crossing_id>")
    def crossing(crossing_id):
        row = conn().execute("SELECT * FROM crossings WHERE id = ?", (crossing_id,)).fetchone()
        if row is None:
            abort(404)
        return jsonify(crossing_detail(row))

    @app.get("/api/crossings/<int:crossing_id>/summary")
    def crossing_summary(crossing_id):
        row = conn().execute("SELECT * FROM crossings WHERE id = ?", (crossing_id,)).fetchone()
        if row is None:
            abort(404)
        detail = crossing_detail(row)
        text = report.crossing_text(row["id"], row["ref_ms"], detail, app.config["TZ"])
        return text, 200, {"Content-Type": "text/plain; charset=utf-8"}

    @app.get("/api/daily/<day>")
    def daily(day):
        return jsonify(metrics.daily_summary(conn(), day, app.config["TZ"]))

    @app.get("/health")
    def health():
        return jsonify({"ok": True})

    return app
