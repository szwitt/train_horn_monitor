# Crossing lights node

A dedicated Boron that watches the crossing's flashing lights from off the railroad right of way. Nothing is attached to the crossing equipment. It gives the backend a measured start for each warning, so the warning time check (49 CFR 234.225, at least 20 s) no longer depends on hearing the bell. It is separate from the sound nodes, so those can be moved without losing the lights.

## Hardware

| Part | Connection |
| --- | --- |
| Particle Boron (LTE) | |
| BH1750 light sensor, lamp A (Adafruit #4681) | VIN to 3V3, GND, SDA to **D0**, SCL to **D1**, ADDR to **GND** (address 0x23) |
| BH1750 light sensor, lamp B | Same bus, ADDR to **3V3** (address 0x5C) |
| LiPo battery and 2 W solar panel | As on the sound nodes: battery on the JST jack, 5 V solar charger into USB |

The BH1750 was picked over the TSL2591 because two of them can share the I2C bus (the TSL2591's address is fixed). The Adafruit breakouts already have their I2C pull-ups.

Mount each sensor at the back of a dark tube about 10 cm long and 1 to 2 cm wide, with red gel over the front, and aim it at one lamp of the same flasher head, so that A sees the left lamp and B the right. The narrow view keeps the sky and passing cars out. Two lamps that light in turn are what tell the lights apart from sun glare or a reflection, which would light both sensors at once.

### Choosing the spot

- Stand where the node will go during a train and film the lights with a phone. If the lamp face shows clearly in the video, with no branches or parked cars in the way, the sensor will see it.
- The lamps point at road traffic, so pick a spot in front of a lamp, along the road, 30 to 100 m out.
- Avoid aiming toward the low winter sun if you can. A sensor that is saturated by direct sun can't confirm the lamps alternating, so that activation is missed rather than reported wrongly. The `drop` and `la`/`lb` heartbeat fields help spot this.
- Get the landowner's permission in writing, since the data may be used in the quiet zone application.

## How detection works

- Each sensor is read 20 times a second on a timer, so readings keep their timing even while the radio is starting or stopping.
- A lamp counts as a flash when its reading over the last 4 seconds swings by at least 5 lux and by at least a quarter of the brightest reading. That works in daylight and at night without a fixed threshold.
- A lamp is **flashing** after 3 evenly spaced flashes in a row at 27 to 86 per minute (crossing lights run at 35 to 65).
- With two lamps, an activation starts when both are flashing and exactly one is lit at least 60% of the time. With one lamp (`lamps=1`), that lamp alone starts it.
- The start time is the **first** flash of the confirming run, not the moment it was confirmed. The activation ends 4 s after no lamp is flashing, and the end time is the last moment a lamp was lit. Once started, either lamp keeps it going, so a truck hiding one lamp doesn't split it.

Thresholds are in [`src/lights_config.h`](src/lights_config.h).

## Power

The radio is off except when there is something to send. After a power-up it stays online for 5 minutes, for the clock, settings and firmware updates. After that it connects once per activation and once every 6 hours for a heartbeat, publishes, stays on for another minute, then turns the radio off. If it can't connect within 5 minutes it tries again 15 minutes later, and keeps everything queued. The status LED is kept dark.

Estimated average draw is about 6 to 8 mA, which is 0.15 to 0.2 Ah a day: more than 10 days on the stock 2000 mAh battery, or year round with a 2 W panel. Measure a real unit with a USB power meter before relying on it. A firmware update can only reach the node while it is online, so send updates in the 5 minutes after a reset, or set `online=1` first.

## Published events

`crossing` (one per activation), with `kind` set to `lights` so the backend can tell it from the sound nodes:

```json
{"v":1,"kind":"lights","node":5,"dist_ft":120,"t0":1760000001,"t0_ms":234,
 "flash":{"s":0,"e":95000,"n":79,"fpm":50.0,"alt":true,"lamps":2}}
```

| Field | Meaning |
| --- | --- |
| `t0`, `t0_ms` | First flash, epoch seconds plus milliseconds |
| `flash.s`, `flash.e` | Start and end, milliseconds from `t0` |
| `flash.n`, `flash.fpm` | Flashes counted and flashes per minute |
| `flash.alt` | Two lamps were seen lighting in turn |
| `flash.lamps` | Sensors in use |

`heartbeat` (every 6 hours): `kind`, `node`, `dist_ft`, `soc` (battery %, -1 on an Argon), `la` and `lb` (current lux), `lamps`, `sensors` (sensors found at power-up), `n` (activations since the last heartbeat), `drop` (readings lost because the queue overflowed, should stay 0).

The backend joins each activation to the train that arrived while the lights were flashing, and uses its start for the warning time check. The existing `crossing` and `heartbeat` webhooks carry these events, so no Particle setup changes are needed.

## Setup

1. Compile and flash from this folder: `particle compile boron flasher` (or open the folder in Particle Workbench).
2. Set it up with the `config` cloud function in the 5 minutes after power-up, for example `node=5,dist=120,lamps=2`. Give it a node number the sound nodes don't use. `dist` is its distance from the track, for the record only.
3. Check the `lux_a` and `lux_b` cloud variables while a train goes by. Each one should jump by at least a few lux, in turn, as its lamp flashes.

| Setting | Meaning |
| --- | --- |
| `node` | Node number (default 5) |
| `dist` | Distance from the track in feet |
| `lamps` | `2` (default) or `1` if only one lamp can be seen |
| `test` | `1` on a bench unit: publish `test-crossing` and `test-heartbeat`, which the backend ignores |
| `online` | `1` keeps the radio on (bench use only, costs battery) |
| `demo` | `demo=60` flashes LEDs on D6 and D7 in turn for 60 s (see below) |

## Bench test with blinking LEDs

The same firmware runs on the Argon bench unit.

1. Wire a red LED with a 330 ohm resistor from **D6** to GND, and another from **D7** to GND. Place the D6 LED a few centimeters in front of sensor A and the D7 LED in front of sensor B. The hoods can stay off in a dim room.
2. Set test mode and keep the radio on: `test=1,online=1,node=9`.
3. Run `demo=60`. The LEDs flash in turn at 50 per minute for 60 s.
4. About 4 s after they stop, the Particle console shows a `test-crossing` event with `"e"` near 60000, `"fpm"` near 50 and `"alt":true`.
5. To check that glare is rejected, cover one sensor and run `demo=60` again. Nothing should be published. Then set `lamps=1` and repeat, and it should be.

## Tests

The detection logic has no Particle dependencies and runs on a PC with the sound node tests, against synthetic daylight, night, clouds, headlights, hazard lights, a saturated sensor and a lamp hidden by a truck:

```sh
make -C test
```
