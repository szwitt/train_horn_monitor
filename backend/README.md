# Backend

Receives each node's `crossing` and `heartbeat` events from a Particle webhook, groups the four nodes' reports into one record per train, runs the FRA checks, and computes the community noise metrics for the quiet zone case at crossing 085770G (Atchinson Rd, Olympia).

## What it produces

**Per train** (`GET /api/crossings/<id>` as JSON, `/summary` as text):

```
Crossing 1 at 2026-10-19 23:10:32 PDT
Train took 25 s to pass
Horn lead 16.0 s
Bell lead 32.7 s

Levels by distance:
  50 ft (node 1): ambient 49.0 peak 96.0 SEL 107.0 dB(A)
  ...

FRA checks:
  Horn timing (49 CFR 222.21(b)): Pass. Horn started 16.0 s before arrival (15 to 20 s)
  Horn pattern (49 CFR 222.21(a)): Pass. Long-long-short-long heard (LLSL at 50 ft; 4 of 4 nodes agree)
  Horn held to crossing (49 CFR 222.21(a)): Pass. Horn sounded until the train reached the crossing
  Horn loudness (49 CFR 229.129): Indicator. 92.0 dB(A) at 100 ft, below the 96 dB(A) minimum (wayside estimate only)
  Warning time (49 CFR 234.225): Pass. Bell started 32.7 s before arrival (at least 20 s)
```

**Per day** (`GET /api/daily/2026-10-19`): day-night level (DNL) for each node, number of crossings, crossings at night (10 pm to 7 am) and night crossings with the horn sounded. DNL needs at least 80% of a day's 15-minute heartbeats.

## How the checks decide

| Check | Pass | Other results |
| --- | --- | --- |
| Horn timing | Horn starts 15 to 25 s before arrival (20 to 25 s is the good-faith allowance) | 11.4 to 15 s is **unverified**: legal only above 60 mph, where the horn may start 1/4 mile out. Under 11.4 s or over 25 s fails. |
| Horn pattern | Long-long-short-long heard at any node | Fails if no node heard it |
| Horn held to crossing | Last blast ends within 3 s of arrival | Fails otherwise |
| Horn loudness | Always an **indicator**, from the node nearest 100 ft | Wayside readings can't reproduce the FRA test |
| Warning time | Bell starts at least 20 s before arrival | Fails under 20 s |

Arrival is the train start heard at the node nearest the track. Horn start is the earliest horn heard by any node.

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

For anything beyond testing, run it behind HTTPS (Particle webhooks need a public URL), for example with gunicorn behind a reverse proxy.

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
