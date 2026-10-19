"""HTTP service: receives the Particle webhook and serves crossing results.

Environment:
  THM_DB     SQLite file (default thm.sqlite3)
  THM_TOKEN  shared secret the Particle webhook sends as X-THM-Token (required)
  THM_TZ     local timezone for night hours and daily metrics (default America/Los_Angeles)
"""

import hmac
import json
import os
from datetime import datetime
from zoneinfo import ZoneInfo

from flask import Flask, abort, g, jsonify, redirect, render_template, request, url_for

from . import analytics, compliance, db, ingest, metrics, report

EMBED_WIDGETS = ("stats", "horn-hours", "dnl", "log")
DEFAULT_DAYS = 30


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

    def days_arg():
        return max(1, min(int(request.args.get("days", DEFAULT_DAYS)), 365))

    @app.after_request
    def allow_embedding(resp):
        # Public read-only data, so external sites can fetch it directly
        if request.method == "GET" and request.path.startswith("/api/"):
            resp.headers["Access-Control-Allow-Origin"] = "*"
        return resp

    @app.context_processor
    def template_globals():
        return {"STATUS_WORDS": report.STATUS_WORDS, "RULE_NAMES": report.RULE_NAMES,
                "theme": request.args.get("theme", ""), "embed": False, "refresh": None}

    @app.get("/api/stats")
    def api_stats():
        return jsonify(analytics.stats(conn(), app.config["TZ"]))

    @app.get("/api/series/horn-hours")
    def api_horn_hours():
        return jsonify(analytics.horn_by_hour(conn(), app.config["TZ"], days_arg()))

    @app.get("/api/series/dnl")
    def api_dnl():
        return jsonify(analytics.dnl_series(conn(), app.config["TZ"], days_arg()))

    @app.get("/api/log")
    def api_log():
        limit = min(int(request.args.get("limit", 50)), 500)
        return jsonify(analytics.crossing_log(conn(), app.config["TZ"], limit))

    def widget_data(widget, days):
        tz = app.config["TZ"]
        data = {}
        if widget in ("all", "stats"):
            data["stats"] = analytics.stats(conn(), tz)
        if widget in ("all", "horn-hours"):
            data["hours"] = analytics.horn_by_hour(conn(), tz, days)
        if widget in ("all", "dnl"):
            data["dnl"] = analytics.dnl_series(conn(), tz, days)
        if widget in ("all", "log"):
            data["log"] = analytics.crossing_log(conn(), tz, 25)
        return data

    @app.get("/")
    def index():
        return redirect(url_for("dashboard"))

    @app.get("/dashboard")
    def dashboard():
        return render_template("dashboard.html", refresh=300, **widget_data("all", days_arg()))

    @app.get("/embed/<widget>")
    def embed(widget):
        if widget not in EMBED_WIDGETS:
            abort(404)
        return render_template("embed.html", widget=widget, embed=True, refresh=300,
                               **widget_data(widget, days_arg()))

    @app.get("/crossing/<int:crossing_id>")
    def crossing_page(crossing_id):
        row = conn().execute("SELECT * FROM crossings WHERE id = ?", (crossing_id,)).fetchone()
        if row is None:
            abort(404)
        when = datetime.fromtimestamp(row["ref_ms"] / 1000, ZoneInfo(app.config["TZ"]))
        return render_template("crossing.html", c=crossing_detail(row),
                               when=when.strftime("%Y-%m-%d %I:%M %p %Z"))

    @app.get("/health")
    def health():
        return jsonify({"ok": True})

    return app
