"""Crossing scenarios for bench and replay testing.

Each scenario is a timeline of bell, horn blasts and train rumble around one
train arriving at ARRIVAL_S, plus what the node and the FRA checks should
report for it. Times are seconds from the start of the recording.
"""

ARRIVAL_S = 100.0  # leaves time for the node's ambient baseline to settle
TRAIN_S = 40.0  # how long the train takes to pass
TAIL_S = 45.0  # quiet after the train so the node closes the crossing


def llsl(lead_s, held=True):
    """Long-long-short-long starting lead_s before arrival.

    The last long blast runs to arrival when held, else stops 8 s early.
    """
    t = ARRIVAL_S - lead_s
    blasts = [(t, t + 3.0), (t + 4.0, t + 7.0), (t + 8.0, t + 9.0)]
    last_end = ARRIVAL_S if held else ARRIVAL_S - 8.0
    blasts.append((t + 10.0, max(t + 12.0, last_end)))
    return blasts


SCENARIOS = [
    {
        "name": "compliant",
        "about": "Bell 30 s ahead, LLSL horn from 17 s out held to arrival",
        "bell": (ARRIVAL_S - 30.0, ARRIVAL_S + TRAIN_S),
        "blasts": llsl(17.0),
        "train": True,
        "expect": {"bell": True, "horn": True, "pattern": True, "train": True,
                   "horn_lead_s": 17.0, "bell_lead_s": 30.0,
                   "checks": {"horn_timing": "pass", "horn_pattern": "pass",
                              "horn_until_arrival": "pass", "warning_time": "pass"}},
    },
    {
        "name": "horn_early",
        "about": "Horn starts 30 s out, beyond the 25 s good-faith limit",
        "bell": (ARRIVAL_S - 35.0, ARRIVAL_S + TRAIN_S),
        "blasts": llsl(30.0),
        "train": True,
        "expect": {"bell": True, "horn": True, "pattern": True, "train": True, "horn_lead_s": 30.0,
                   "checks": {"horn_timing": "fail", "horn_pattern": "pass",
                              "horn_until_arrival": "pass", "warning_time": "pass"}},
    },
    {
        "name": "horn_quarter_mile",
        "about": "Horn starts 13 s out, legal only above 60 mph",
        "bell": (ARRIVAL_S - 30.0, ARRIVAL_S + TRAIN_S),
        "blasts": llsl(13.0),
        "train": True,
        "expect": {"bell": True, "horn": True, "pattern": True, "train": True, "horn_lead_s": 13.0,
                   "checks": {"horn_timing": "unverified", "horn_pattern": "pass",
                              "horn_until_arrival": "pass", "warning_time": "pass"}},
    },
    {
        "name": "horn_late",
        "about": "Horn starts 9 s out, too late even at 79 mph",
        "bell": (ARRIVAL_S - 30.0, ARRIVAL_S + TRAIN_S),
        "blasts": [(ARRIVAL_S - 9.0, ARRIVAL_S - 6.0), (ARRIVAL_S - 5.0, ARRIVAL_S - 2.5),
                   (ARRIVAL_S - 1.5, ARRIVAL_S - 0.8), (ARRIVAL_S - 0.2, ARRIVAL_S + 2.0)],
        "train": True,
        "expect": {"bell": True, "horn": True, "pattern": True, "train": True, "horn_lead_s": 9.0,
                   "checks": {"horn_timing": "fail", "horn_pattern": "pass",
                              "horn_until_arrival": "pass", "warning_time": "pass"}},
    },
    {
        "name": "no_short_blast",
        "about": "Three long blasts, so the FRA pattern is missing",
        "bell": (ARRIVAL_S - 30.0, ARRIVAL_S + TRAIN_S),
        "blasts": [(ARRIVAL_S - 17.0, ARRIVAL_S - 13.0), (ARRIVAL_S - 12.0, ARRIVAL_S - 8.0),
                   (ARRIVAL_S - 7.0, ARRIVAL_S)],
        "train": True,
        "expect": {"bell": True, "horn": True, "pattern": False, "train": True, "horn_lead_s": 17.0,
                   "checks": {"horn_timing": "pass", "horn_pattern": "fail",
                              "horn_until_arrival": "pass", "warning_time": "pass"}},
    },
    {
        "name": "horn_stops_early",
        "about": "Pattern finishes 8 s before the train reaches the crossing",
        "bell": (ARRIVAL_S - 30.0, ARRIVAL_S + TRAIN_S),
        "blasts": llsl(20.0, held=False),
        "train": True,
        "expect": {"bell": True, "horn": True, "pattern": True, "train": True, "horn_lead_s": 20.0,
                   "checks": {"horn_timing": "pass", "horn_pattern": "pass",
                              "horn_until_arrival": "fail", "warning_time": "pass"}},
    },
    {
        "name": "short_warning",
        "about": "Bell starts only 12 s before the train",
        "bell": (ARRIVAL_S - 12.0, ARRIVAL_S + TRAIN_S),
        "blasts": llsl(17.0),
        "train": True,
        "expect": {"bell": True, "horn": True, "pattern": True, "train": True,
                   "horn_lead_s": 17.0, "bell_lead_s": 12.0,
                   "checks": {"horn_timing": "pass", "horn_pattern": "pass",
                              "horn_until_arrival": "pass", "warning_time": "fail"}},
    },
    {
        "name": "no_horn",
        "about": "Bell and train with no horn, as in a quiet zone",
        "bell": (ARRIVAL_S - 30.0, ARRIVAL_S + TRAIN_S),
        "blasts": [],
        "train": True,
        "expect": {"bell": True, "horn": False, "train": True, "bell_lead_s": 30.0,
                   "checks": {"horn_timing": "not_detected", "horn_pattern": "not_detected",
                              "warning_time": "pass"}},
    },
    {
        "name": "bell_only",
        "about": "Gates come down and go back up with no train",
        "bell": (ARRIVAL_S - 30.0, ARRIVAL_S + 10.0),
        "blasts": [],
        "train": False,
        "tail_s": 140.0,  # a crossing with no train closes after 2 minutes of quiet
        "expect": {"bell": True, "horn": False, "train": False,
                   "checks": {"horn_timing": "not_detected", "warning_time": "not_detected"}},
    },
]


def duration(s):
    end = ARRIVAL_S + TRAIN_S if s["train"] else max(s["bell"][1], ARRIVAL_S)
    return end + s.get("tail_s", TAIL_S)
