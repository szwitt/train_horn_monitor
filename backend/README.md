# Backend

Receives each node's `crossing` and `heartbeat` events from a Particle webhook, groups the four sound nodes' reports and the crossing lights node's report into one record per train, runs the FRA checks, and computes the community noise metrics for the quiet zone case at crossing 085770G (Atchinson Rd, Olympia).

## What it produces

**Per train** (`GET /api/crossings/<id>` as JSON, `/summary` as text):

```
Crossing 1 at 2026-10-19 23:10:32 PDT
Train took 25 s to pass
Horn lead 16.0 s
Bell lead 32.7 s
Lights lead 32.7 s
Lights flashed for 62 s

Levels by distance:
  50 ft (node 1): ambient 49.0 peak 96.0 SEL 107.0 dB(A)
  ...

FRA checks:
  Horn timing (49 CFR 222.21(b)): Pass. Horn started 16.0 s before arrival (15 to 20 s)
  Horn pattern (49 CFR 222.21(a)): Pass. Long-long-short-long heard (LLSL at 50 ft; 4 of 4 nodes agree)
  Horn held to crossing (49 CFR 222.21(a)): Pass. Horn sounded until the train reached the crossing
  Horn loudness (49 CFR 229.129): Indicator. 92.0 dB(A) at 100 ft, below the 96 dB(A) minimum (wayside estimate only)
  Warning time (49 CFR 234.225): Pass. Lights started 32.7 s before arrival (at least 20 s); bell heard 32.7 s before
```

**Per day** (`GET /api/daily/2026-10-19`): day-night level (DNL) for each node, number of crossings, crossings at night (10 pm to 7 am) and night crossings with the horn sounded. DNL needs at least 80% of a day's 15-minute heartbeats.

## How the checks decide

| Check | Pass | Other results |
| --- | --- | --- |
| Horn timing | Horn starts 15 to 25 s before arrival (20 to 25 s is the good-faith allowance) | 11.4 to 15 s is **unverified**: legal only above 60 mph, where the horn may start 1/4 mile out. Under 11.4 s or over 25 s fails. |
| Horn pattern | Long-long-short-long heard at any node | Fails if no node heard it |
| Horn held to crossing | Last blast ends within 3 s of arrival | Fails otherwise |
| Horn loudness | Always an **indicator**, from the node nearest 100 ft | Wayside readings can't reproduce the FRA test |
| Warning time | Lights (or, without the lights node, the bell) start at least 20 s before arrival | Fails under 20 s. Lights with no train heard is **not detected**. |

Arrival is the train start heard at the node nearest the track. Horn start is the earliest horn heard by any node. Lights start is the first flash seen by the [crossing lights node](../flasher/README.md); its report joins the train that arrived while the lights were flashing (within 10 s either side), and is left out of the levels table and the day-night levels.

## Dashboard and embeds

`/dashboard` shows headline counters, horn use by hour of day, day-night level by distance, and a log of recent crossings that links to each train's page (`/crossing/<id>`). Pages refresh every 5 minutes and follow the viewer's light or dark setting.

Each part can be embedded on another site with an iframe:

| Widget | URL |
| --- | --- |
| Counters: trains today, night horns this week, horn timing pass rate | `/embed/stats` |
| Trains sounding the horn by hour (night hours highlighted) | `/embed/horn-hours` |
| Day-night level by distance | `/embed/dnl` |
| Recent crossings | `/embed/log` |

```html
<iframe src="https://<your-host>/embed/horn-hours" width="100%" height="380"
        style="border:0" title="Train horns by hour, Atchinson Rd crossing"></iframe>
```

Add `?theme=light` or `?theme=dark` to force a theme, and `?days=90` to change the window (charts default to 30 days). The same numbers are available as JSON for sites that draw their own charts, with cross-origin reads allowed: `/api/stats`, `/api/series/horn-hours`, `/api/series/dnl`, `/api/log`.

To preview with a month of made-up data:

```sh
python -m thm.demo demo.sqlite3
THM_DB=demo.sqlite3 THM_TOKEN=x flask --app "thm.app:create_app()" run
```

## Deploying on Render

`render.yaml` at the repo root is a Render Blueprint: one web service with a 1 GB persistent disk for the database.

1. In Render, choose New, then Blueprint, and pick this repository.
2. After the first deploy, copy the generated `THM_TOKEN` from the service's Environment page.
3. Set up the Particle webhooks below with `https://<service>.onrender.com/particle` and that token.

The disk keeps the data across deploys. A service with a disk runs as a single instance, which is plenty for four nodes.

## Running it

```sh
cd backend
pip install -r requirements.txt
export THM_TOKEN=$(python -c "import secrets; print(secrets.token_urlsafe(24))")
flask --app "thm.app:create_app()" run --host 0.0.0.0 --port 8080
```

| Variable | Default | Meaning |
| --- | --- | --- |
| `THM_TOKEN` | (required) | Shared secret the webhook sends in the `X-THM-Token` header |
| `THM_DB` | `thm.sqlite3` | SQLite database file |
| `THM_TZ` | `America/Los_Angeles` | Local time zone for night hours and daily metrics |

For anything beyond testing, run it behind HTTPS (Particle webhooks need a public URL), for example with `gunicorn "thm.app:create_app()"` behind a reverse proxy, or use the Render setup above.

## Particle webhook setup

Create two webhooks in the Particle console (Integrations, Webhook), one for event `crossing` and one for `heartbeat`:

- URL: `https://<your-host>/particle`
- Request format: JSON (the default template is fine)
- Extra header: `X-THM-Token: <your THM_TOKEN>`

## Tests

```sh
pip install -r requirements-dev.txt
python -m pytest -q
```
