# Bench testing

Two ways to exercise the detectors without waiting for a train: replay the scenarios on a PC, or play them through a speaker at a bench unit. Both use the same scenario files, so a result on the bench can be compared with the PC.

## The scenarios

`tools/bench/scenarios.py` defines nine crossings around a train that reaches the crossing 100 s into each recording. The first 100 s are quiet so the node's ambient baseline settles.

| Scenario | What happens | FRA result it should produce |
| --- | --- | --- |
| `compliant` | Bell 30 s ahead, long-long-short-long from 17 s out, held to arrival | All checks pass |
| `horn_early` | Horn starts 30 s out | Horn timing fails (over 25 s) |
| `horn_quarter_mile` | Horn starts 13 s out | Horn timing unverified (legal only above 60 mph) |
| `horn_late` | Horn starts 9 s out | Horn timing fails (under 11.4 s) |
| `no_short_blast` | Three long blasts | Horn pattern fails |
| `horn_stops_early` | Pattern ends 8 s before the train arrives | Horn held to crossing fails |
| `short_warning` | Bell starts 12 s before the train | Warning time fails (under 20 s) |
| `no_horn` | Bell and train, no horn | Horn checks not detected |
| `bell_only` | Gates cycle with no train | Report with a bell and no train |

The sounds are the same synthetic sources as the host tests: a three-note horn chord in the 250 to 800 Hz band, 2.1 kHz bell strikes three times a second, and low train rumble. Add new cases, including real recordings, by adding an entry to `scenarios.py`.

## Replay on a PC

```sh
python3 tools/bench/run_scenarios.py
```

This builds `test/replay`, writes the scenario files to `bench-scenarios/` (about 8 MB each, not committed), runs each one through the node's detectors and then the backend's FRA checks, and compares the results with what the scenario expects. CI runs it on every pull request.

To replay any recording, including one made at the crossing, give it a meter trace (`t_ms,dba` rows every 125 ms) and run:

```sh
make -C test replay
test/replay recording.wav recording.meter.csv
```

It prints the `crossing` JSON the node would publish.

## Play at a bench unit

The bench unit is an Argon wired exactly like a field node (see the README). It runs the same firmware, so CI compiles it for both boards.

1. Flash the Argon and set it to test mode with the `config` function: `node=9,dist=50,cal=0,test=1`. In test mode it publishes `test-crossing` and `test-heartbeat`, which the backend's `crossing` and `heartbeat` webhooks don't match, so bench trains never reach the live record.
2. Generate the audio: `python3 tools/bench/make_scenarios.py`.
3. Place a speaker about 1 m from the microphone in a quiet room, and play one `.wav` file all the way through, including the quiet start.
4. Watch the result in the Particle console's event log as `test-crossing`, and compare it with that scenario's `.expect.json`.

Detection is measured against the room's own ambient level, so a speaker at ordinary volume is enough. The horn needs to be at least 10 dB above the room, and the bell at least 6 dB. Only the absolute levels in the report (peak, SEL, horn loudness) will be lower than a real train's.

## Checking the meter

The scenarios can't check the Gravity meter's calibration. For that, hold a 94 dB, 1 kHz acoustic calibrator, or a reference sound level meter, against each node. Compare it with the `dba` cloud variable, and set `cal` to the difference before the node goes out.
