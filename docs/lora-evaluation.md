# LoRa instead of Particle cellular: evaluation

Question: should the four sound nodes and the crossing lights node at 085770G move off Particle cellular onto a LoRa network (ESP32 or similar), given that every node is within 1 to 2 km of the others? Cost is an input, not a constraint.

Status: analysis only, November 2026. Prices are approximate US retail and should be checked before ordering. Power figures are estimates until a unit is measured.

## Recommendation in short

1. **Yes, move the field links to LoRa, but keep the Borons.** Add a 900 MHz LoRa FeatherWing to each Boron and send reports over LoRa to a gateway you own. Turn the cellular radio off in normal operation and use it only as a maintenance channel (firmware updates, emergency config). The detection code, PDM microphone driver and meter wiring stay exactly as they are.
2. **Use plain LoRa point-to-point, not LoRaWAN.** Five nodes and one gateway that you own on both ends do not need a network server. Point-to-point also lets the gateway broadcast a time beacon, which brings node clocks to within a few milliseconds of each other instead of about a second.
3. **Gateway: an ESP32-S3 LoRa board on home Wi-Fi** with an outdoor antenna, if your house (or a friendly neighbor's) is within about 2 km of all nodes. If not, use a fifth Boron with a LoRa FeatherWing at a solar site as a cellular gateway, which still cuts Particle usage by about 90%.
4. **Do a range test before buying anything else.** Two ESP32 LoRa boards (about $50) settle whether the trees and terrain between the crossing and the gateway site allow it.
5. **ESP32 for the field nodes is workable but not the best fit.** It needs 4 to 6 times the power of the Boron's nRF52840 while listening continuously, its ADC needs an external converter to read the Gravity meter accurately, and the firmware port is the largest of the options. If you later want to drop Particle entirely, the RAK4631 (the same nRF52840 chip as the Boron, with a LoRa radio built in) is the better full replacement.

The honest finding on cost: at the current design, Particle's free tier is not close to its limits and cannot send you a bill. The real Particle risks are a cliff (pause or a $299/month block), a policy change you don't control, and a pause during the month landing as a gap in your quiet zone evidence. LoRa removes all three and also improves timing accuracy, which is the bigger technical win.

## What the nodes send today

From `src/train_horn_monitor.cpp`, `src/Episode.cpp` and `flasher/src/crossing_lights.cpp`:

| Message | Who | How often | Size (JSON) |
| --- | --- | --- | --- |
| `crossing` | each sound node | once per train, about 30 a day | 250 to 700 bytes, under 1 KB |
| `heartbeat` | each sound node | every 15 min, 96 a day | about 90 bytes |
| `crossing` (`kind: lights`) | lights node | once per activation, about 30 a day | about 200 bytes |
| `heartbeat` | lights node | every 6 h, 4 a day | about 90 bytes |

Inbound: the `config` Particle function (`node=, dist=, cal=, test=`), rarely used, plus firmware updates.

Timing: `nowMs()` anchors to `Time.now()`, which Particle syncs from the cloud to the whole second. Node clocks therefore agree only to roughly ±0.5 to 1 s. Sound takes about 3 s to cross 1 km, so cross-node timing comparisons today are coarse.

## Particle: honest cost and risk

### Current usage

Particle counts one data operation (DO) per publish per 1,024 bytes; the webhook it triggers does not count again. OTA updates, device vitals and keep-alives don't count as DOs, though they use cellular MB. ([Particle data operations](https://docs.particle.io/getting-started/billing/data-operations/))

| | Per month | Free tier limit | Share |
| --- | --- | --- | --- |
| Data operations (4 sound nodes + lights node, as designed) | about 16,000 | 100,000 | 16% |
| Earlier planning estimate with margin | about 34,000 | 100,000 | 34% |
| Cellular data | about 8 to 10 MB | 100 MB pooled | 10% |

### What happens at the limit

On the free plan, going over does not charge you. All data operations stop for the rest of the billing month, and going over the cellular pool pauses every SIM in the account (the cellular page mentions a three-day window to upgrade first). The only way to buy more is a Basic block: **$299 a month**, 720K DOs and 540 MB, minimum one block. ([Migrating to Basic](https://docs.particle.io/getting-started/billing/migrating-to-basic), [Cellular data](https://docs.particle.io/getting-started/billing/cellular-data/))

### What could push you over

| Change | DOs per month | Result |
| --- | --- | --- |
| As designed | about 16K | Fine |
| Add 4 more nodes (second crossing) | about 32K | Fine |
| 1-minute Leq published as its own event, 4 nodes | about 190K | Over: pause or $299/month |
| Same 1-minute Leq batched into the 15-minute heartbeat | about 16K | Fine (one 1 KB publish per 15 min) |
| A firmware bug that publishes in a loop at 1/s | 2.6M per node | Over within a day or two; account-wide pause |
| Particle changes the free tier | Unknown | Outside your control |

Particle's own wording is that the free plan "is a place for prototyping products", and the docs make no commitment about notice before limits change. Particle has reworked its pricing several times since 2020.

### Particle summary

- **Cost risk today: low.** You are well under the limits and cannot be billed without upgrading.
- **Real risks:** a month-long pause from a bug or a design change (lost evidence nights), the $299 cliff if you outgrow the free tier, and dependence on a vendor's free tier for a multi-year record.
- **Cheap mitigations if you stay on Particle:** a daily publish cap in firmware (for example 400 publishes a day per node, then heartbeats only) and batching any new time series into 1 KB publishes.

## LoRa options

All options below use the US 902 to 928 MHz ISM band. Railroad ATCS radio sits at 896 to 901 and 935 to 940 MHz and PTC near 220 MHz, so this band does not overlap railroad signaling. Nothing gets mounted on BNSF property.

### Option A: plain LoRa point-to-point to your own gateway (recommended)

Each node sends a compact binary packet to a single gateway, which acknowledges it. The gateway turns it back into the same JSON the backend already parses and posts it to Render.

- **Pros:** no network server, no third party, full control of radio settings, easy time beacon and downlink config, simplest gateway hardware (a $20 to $25 board).
- **Cons:** you write the small protocol yourself (acknowledge, retry, de-duplicate, encrypt, beacon), about 300 to 500 lines shared by nodes and gateway. One gateway is a single point of failure unless nodes buffer (they will; see below).
- **Library:** [RadioLib](https://github.com/jgromes/RadioLib) supports the SX1276 (RFM95W) and SX1262 on ESP32 and nRF52.

### Option B: LoRaWAN with your own gateway

A commercial 8-channel gateway (RAKwireless RAK7268V2, Seeed SenseCAP M2 or Dragino LPS8N, about $100 to $250) runs a built-in network server, or forwards to The Things Network (TTN), which then calls a webhook on the backend.

- **Pros:** standard, encrypted, acknowledged and adaptive data rate out of the box; easy to grow to more nodes, more crossings or a second gateway.
- **Cons:** US915 rules limit payload size by data rate (11 bytes at the slowest rate, 242 bytes at SF7), so the crossing report must stay small and the nodes must be close enough for fast rates. LoRaWAN stacks on Particle Device OS are fiddly. Time sync is limited to the `DeviceTimeReq` command, which is coarser and server-dependent. More moving parts than five nodes need.

### Option C: public networks (TTN community gateways or Helium)

- **TTN:** free, fair use limit of 30 s uplink airtime per device per day and 10 downlinks per day. These nodes need about 10 s a day, so it fits. But there is no service guarantee and it depends on someone else's gateway being in range; in practice you'd still install your own (Option B).
- **Helium IoT:** pay per packet, under a dollar a year at this volume, but coverage near Atchinson Rd is unverified, hotspot counts have fallen since 2023, and there's no guarantee.
- **Verdict:** not suitable as the only path for evidence you need to be continuous.

### Comparison

| | A: point-to-point | B: LoRaWAN own gateway | C: public network |
| --- | --- | --- | --- |
| Recurring cost | $0 | $0 (TTN) | $0 to <$1/yr |
| Third-party dependence | None (Render only) | TTN, or none with built-in server | High |
| Gateway cost | $20 to $25 + antenna | $100 to $250 + antenna | $0 if covered |
| Time sync | 1 to 5 ms (beacon) | tens of ms, server dependent | Poor |
| Custom code | Protocol + gateway | Decoder only | Decoder only |
| Scales to more crossings | OK (add gateways) | Best | OK |

## Airtime, payload and FCC fit

Binary-encoding the crossing report (epoch time, levels in 0.1 dB, relative times in 10 ms steps, up to 12 horn blasts) brings it from 250 to 700 bytes of JSON to about **60 to 110 bytes**. A heartbeat is about 20 bytes.

Time on air (SX127x/SX126x, coding rate 4/5, 8-symbol preamble):

| Payload | SF7 / 125 kHz | SF8 / 125 kHz | SF9 / 125 kHz | SF7 / 500 kHz | SF9 / 500 kHz |
| --- | --- | --- | --- | --- | --- |
| 24 B (heartbeat) | 62 ms | 113 ms | 206 ms | 15 ms | 51 ms |
| 110 B (crossing) | 185 ms | 328 ms | 595 ms | 46 ms | 149 ms |
| 242 B | 379 ms | 666 ms | 1,189 ms | 95 ms | 297 ms |

The US has no duty-cycle limit, but FCC Part 15.247 caps a hopping transmitter at 400 ms per channel in 20 s, or requires at least 500 kHz bandwidth as a digital transmission system. Two settings fit cleanly:

- **SF7 or SF8 at 125 kHz with channel hopping.** Each packet under 400 ms. Best sensitivity per byte.
- **SF9 or SF10 at 500 kHz, fixed channel.** Simpler, no dwell limit, about 3 dB less sensitive than SF7/125.

Per node per day: 30 reports × about 0.2 s + 96 heartbeats × about 0.06 s is about **12 s of airtime**, a fraction of a percent of the channel. Five nodes will not collide meaningfully, and acknowledgements with random back-off handle the rest.

Reports go out after the episode closes (30 s of quiet), so the train itself is not blocking the path when a node transmits.

### Range

With 20 dBm transmit power and the settings above, the link budget is about 140 dB. In open suburban terrain that is several km; through Olympia's tree cover with nodes near ground level it may be 1 km or less. The deciding factor is the gateway antenna height: an outdoor antenna on a roof or mast at 6 to 10 m usually turns a marginal link into a solid one. This is why the first step is a range test.

## Time sync between nodes

This is LoRa's biggest technical advantage over the current design.

- **Now:** each Boron's clock is set from the Particle cloud to the whole second; nodes agree to about ±0.5 to 1 s.
- **With a gateway beacon:** the gateway keeps time from NTP over home Wi-Fi (or a $15 GPS module with a PPS output) and broadcasts a time beacon every 10 minutes. Each node records the radio's receive-done interrupt and subtracts the known time on air. Between beacons the nRF52840's 32 kHz crystal (about ±20 ppm) drifts at most about 12 ms. Expected agreement between nodes: **about 1 to 5 ms.**

What that makes possible:

- **Comparing levels across distances at the same instant**, not just per-train summaries.
- **Sound arrival delays between nodes.** Sound covers about 1 m every 2.9 ms, so with millisecond clocks the horn's arrival order and delays across nodes locate where it was blown to within a few metres in principle (more like tens of metres in practice, given echoes and detection jitter).
- **Train speed** from the time the train passage starts at nodes spread along the track. Speed is what currently forces a horn lead time between 11.4 and 15 s at speeds over 60 mph to be marked "unverified"; a measured speed could settle those cases.

## Firmware updates and remote config

Particle's over-the-air updates are the main thing LoRa loses. Sending a firmware image over LoRa is impractical at this data rate. The options:

| Approach | How it works | Fits |
| --- | --- | --- |
| **Cellular kept as maintenance channel (hybrid)** | Gateway sends an "online for 15 min" command over LoRa; the Boron turns cellular on, takes a normal Particle OTA, and turns it off again | Boron nodes |
| BLE update on site | Walk within 10 to 30 m with a phone (nRF Connect DFU) | RAK4631 |
| Wi-Fi update on site | Node joins your phone's hotspot on command and pulls the image | ESP32 |
| Gateway OTA | Gateway on home Wi-Fi updates itself from GitHub releases | Any ESP32 gateway |

Config changes that today use the `config` function (`node`, `dist`, `cal`, `test`) become small LoRa downlinks: the gateway holds pending config and hands it out in the acknowledgement of the node's next heartbeat.

A Boron with cellular off still uses no data operations and costs nothing on the free plan, and Particle OTA only uses cellular MB, not DOs.

## Microcontroller choice for the field nodes

| | Boron (nRF52840) + LoRa FeatherWing | RAK4631 (nRF52840 + SX1262) | ESP32-S3 + SX1262 (Heltec V3, LilyGO T3-S3, RAK3312) |
| --- | --- | --- | --- |
| PDM mic (MP34DT01-M) | Works today | Works (nRF52 PDM, Arduino PDM library) | Works (I2S in PDM receive mode) |
| ICS-43434 I2S mic | No (64-clock frame) | No (same chip) | Yes, I2S standard mode handles it |
| Gravity meter ADC | Works today | Same ADC as Boron | Weak: the ESP32 ADC is nonlinear; add an ADS1115 for 1 dB accuracy (the meter is 50 dB per volt, so 20 mV of error is 1 dB) |
| Average draw, listening 24/7 | about 6 to 10 mA, cellular off | about 5 to 8 mA | about 30 to 45 mA |
| Solar in Olympia winter | 2 W panel, 2 Ah LiPo | 2 W panel, 2 Ah LiPo | 6 to 10 W panel, 6 to 10 Ah battery |
| Firmware port | Transport layer only | Device OS to Arduino: config storage, logging, PDM driver, transport | Same as RAK4631 plus I2S driver, ADC change, power tuning |
| Field firmware updates | Particle OTA on demand | BLE on site | Wi-Fi on site |
| Uses hardware in hand | Yes, all 4 Borons | No | No |

On the ICS-43434: the ESP32 does reopen it, and it has a published, flat sensitivity, so in principle a node could compute A-weighted levels itself and cross-check the Gravity meter. That is a measurement upgrade worth considering on its own merits later, not a reason to switch platforms now; the Gravity meter already gives calibrated dB(A).

Why not ESP32 for the field nodes: these nodes listen continuously, so the MCU never sleeps. The nRF52840 does the 16 kHz FFT work at a few mA; an ESP32-S3 needs several times that, which in an Olympia December (often under 1 peak sun hour a day) means a 6 to 10 W panel and a much bigger battery on every pole. The ESP32 is a good fit for the **gateway**, which is on mains power and Wi-Fi anyway.

## Gateway and backhaul

**Preferred: home Wi-Fi.** An ESP32-S3 LoRa board (Heltec WiFi LoRa 32 V3 or LilyGO T3-S3) indoors, connected by low-loss coax to a 915 MHz fiberglass antenna outside as high as practical, with a lightning arrestor at the entry. It:

- receives and acknowledges node packets, de-duplicates by node and sequence number;
- decodes binary to the existing JSON and POSTs it to the backend over HTTPS;
- keeps a queue in flash if the internet or Render is down and replays it later (nodes also keep their own queue until acknowledged);
- broadcasts the time beacon and hands out pending config;
- reports its own heartbeat with each node's last RSSI and SNR, so the dashboard can show link health.

Put it on a small UPS or USB power bank so a short outage doesn't drop reports.

**Fallback: cellular gateway.** If no Wi-Fi site sees all the nodes, a Boron with a LoRa FeatherWing at a solar site near the crossing does the same job and publishes to Particle. Batching one train's node reports into 1 KB publishes and the heartbeats into one hourly publish keeps usage near 2,000 to 5,000 DOs a month, a fraction of today's.

**Backend changes:** a new `/lora` endpoint (same token scheme as `/particle`) that takes the gateway's JSON. The gateway sends the same shape Particle does (`event`, `data`, `coreid`, `published_at`), so `thm/ingest.py`'s `handle_webhook` is reused unchanged. About 50 lines plus tests, and a link-health widget if wanted.

**Packet security:** point-to-point LoRa has no built-in protection. Since the data supports a quiet zone application, each packet should carry a per-node sequence number and an AES-128 message authentication code (key set at provisioning), so nobody can inject or replay readings. LoRaWAN includes this by default.

## Power and solar

| Node | Average draw (estimate) | Per day | Suggested Olympia sizing |
| --- | --- | --- | --- |
| Sound node, Boron with cellular on (today) | about 15 to 25 mA | 0.4 to 0.6 Ah | 5 W panel, larger battery |
| Sound node, Boron with cellular off + LoRa | about 6 to 10 mA | 0.15 to 0.25 Ah | 2 to 3 W panel, 2 Ah LiPo |
| Sound node, ESP32-S3 + LoRa | about 30 to 45 mA | 0.7 to 1.1 Ah | 6 to 10 W panel, 6 to 10 Ah |
| Lights node, Boron + LoRa | about 3 to 5 mA | under 0.1 Ah | 2 W panel |

The LoRa radio itself is negligible: about 120 mA for 0.2 s, 130 times a day, is under 0.01 Ah. Receive windows for acknowledgements and beacons add a little. Measure one unit with a USB power meter before finalizing panels.

## Bill of materials

Approximate prices, November 2026.

### Recommended hybrid (keep Borons)

| Item | Qty | Each | Total |
| --- | --- | --- | --- |
| Adafruit LoRa Radio FeatherWing, RFM95W 900 MHz (#3231) | 5 | $20 | $100 |
| u.FL to SMA bulkhead pigtail + 915 MHz 3 dBi antenna | 5 | $12 | $60 |
| Heltec WiFi LoRa 32 V3 (gateway) | 1 | $22 | $22 |
| 915 MHz outdoor fiberglass antenna, 5 to 6 dBi | 1 | $50 | $50 |
| LMR-240 coax (25 ft) + lightning arrestor | 1 | $45 | $45 |
| Heltec V3 pair for the range test (reused as spare gateway) | 2 | $22 | $44 |
| Boron for the lights node (already needed) | 1 | | already planned |
| **Total** | | | **about $320** |

### Full replacement (no Particle), for comparison

| Item | Qty | Each | Total |
| --- | --- | --- | --- |
| RAK4631 core + RAK19007 base (solar input, battery charger) | 5 | $35 | $175 |
| Enclosure antenna (SMA) | 5 | $10 | $50 |
| Gateway, antenna, coax, range-test boards as above | | | $160 |
| **Total** | | | **about $385**, Borons become spares |

### ESP32-S3 field nodes, for comparison

| Item | Qty | Each | Total |
| --- | --- | --- | --- |
| Heltec WiFi LoRa 32 V3 or LilyGO T3-S3 | 5 | $22 to $30 | $110 to $150 |
| ADS1115 ADC for the Gravity meter | 4 | $12 | $48 |
| 8 W panel + 10 Ah LiPo pack, instead of 2 W + 2 Ah | 5 | $60 | $300 |
| Gateway, antenna, coax, range-test boards as above | | | $160 |
| **Total** | | | **about $620 to $660**, Borons become spares |

## Porting effort

| Work | Hybrid (Boron + LoRa) | RAK4631 | ESP32-S3 |
| --- | --- | --- | --- |
| Detection code (`NodeProcessor`, detectors, episode) | Unchanged | Unchanged (already portable C++, host-tested) | Unchanged, retune for new sample rate |
| Mic driver | Unchanged | Swap to Arduino PDM | New I2S PDM driver |
| Meter ADC | Unchanged | Minor | ADS1115 driver, recalibrate |
| Transport (binary encoding, ack/retry, encryption, beacon, config downlink) | New | New | New |
| Platform (config storage, logging, power) | Cellular on/off control | Rewrite from Device OS | Rewrite from Device OS |
| Gateway firmware (ESP32-S3) | New | New | New |
| Backend `/lora` endpoint + link health | New, small | New, small | New, small |
| Bench harness (`tools/bench`, `test/replay`) | Add a binary round-trip test | Same | Same |
| Rough size | 2 to 3 PRs | 4 to 5 PRs | 5 to 6 PRs |

One unknown for the hybrid: RadioLib targets Arduino, so it needs a small hardware layer for Particle Device OS (SPI and interrupt calls). That is a known, bounded task; a minimal SX127x driver written directly against Device OS is the fallback.

The FeatherWing's SPI pins (SCK, MOSI, MISO) don't clash with the mic (D2, D3) or the meter (A0); its chip select, reset and interrupt pins are set by solder jumpers and can go on free pins such as D4, D5 and D6. On the lights node, D6 and D7 drive the bench LEDs, so pick other pins there.

## Hybrid options

1. **Recommended: LoRa for data, cellular for maintenance.** Borons with cellular off, LoRa to a home gateway, Particle OTA on demand. Near-zero DOs, vendor-independent data path, keeps OTA.
2. **Cellular gateway at the crossing.** If home is out of range: nodes on LoRa, one Boron gateway on cellular. Still one Particle device to manage, about 90% fewer DOs.
3. **Cellular as automatic fallback.** A node that gets no acknowledgement for, say, 6 hours turns cellular on and publishes its queue to Particle as it does today. The backend already accepts that. Covers a dead gateway at a small cost in complexity and battery.
4. **Mixed fleet during transition.** Migrate one node to LoRa and compare its reports with the cellular nodes for a couple of weeks before moving the rest. Nothing in the backend has to change for the cellular nodes.

## Risks

- **Range through trees** is the largest unknown. Mitigation: range test first; gateway antenna height; SF8 or a 500 kHz SF9/SF10 setting if needed.
- **Single gateway.** Nodes queue until acknowledged (several days fit in RAM or flash), and option 3 above adds a cellular fallback.
- **More code you own.** The protocol is small, but it's code Particle currently provides. The bench harness covers detection; add a transport round-trip test.
- **Home internet and power** become part of the data path. A UPS and the gateway's flash queue cover short outages.

## Next steps

1. Range test: two Heltec V3 boards, one at the intended gateway spot with the antenna as high as practical, one carried to each planned node location, logging RSSI and SNR at SF7/125 and SF9/500. Claude can write the test sketch.
2. If the range is good, order the hybrid BOM and build the transport layer and gateway on the bench (Argon plus FeatherWing works for bench testing, since the Argon has the same pinout).
3. Add the `/lora` endpoint and link-health heartbeat to the backend.
4. Move one node to LoRa, run it next to the cellular nodes for two weeks, then move the rest.
5. Later, if wanted: speed estimation from synced node timings, and an ICS-43434 or other calibrated mic as a cross-check of the Gravity meter.
