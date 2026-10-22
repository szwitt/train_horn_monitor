// Host tests for the crossing lights node's flash detection. Build and run
// with `make -C test`. Light levels are synthetic stand-ins for the lamps,
// daylight and stray light; tune thresholds from readings at the crossing.

#include <cmath>
#include <cstdio>
#include <cstring>
#include <functional>
#include <random>
#include <vector>

#include "FlashDetector.h"
#include "lights_config.h"

static int failures = 0;
static int checks = 0;

#define CHECK(cond)                                                       \
    do {                                                                  \
        checks++;                                                         \
        if (!(cond)) {                                                    \
            failures++;                                                   \
            std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);   \
        }                                                                 \
    } while (0)

#define CHECK_NEAR(a, b, tol) CHECK(std::fabs((double)(a) - (double)(b)) <= (tol))

namespace {

constexpr uint64_t kEpochMs = 1760000000000ULL;

// What each sensor sees: background light plus its lamp when lit.
struct LightScene {
    std::mt19937 rng{7};
    std::normal_distribution<float> gauss{0.0f, 1.0f};
    double background = 300.0;  // lux through the hood, daytime
    double lampLux = 400.0;  // added by a lit lamp
    double fpm = 50.0;
    double on = -1, off = -1;  // seconds the lights are active
    bool alternate = true;  // false: both sensors lit together, like stray light
    std::function<double(double)> extra;  // more light on both sensors (clouds, headlights)
    std::function<bool(int, double)> blocked;  // lamp hidden from its sensor
    double saturateAbove = 1e9;

    bool lampLit(int lamp, double t) const {
        if (t < on || t >= off) return false;
        double period = 60.0 / fpm;
        double phase = std::fmod(t - on, period) / period;
        bool first = phase < 0.5;  // 50% duty
        return (lamp == 0 || !alternate) ? first : !first;
    }

    void read(double t, float *lux, bool *sat) {
        for (int i = 0; i < 2; i++) {
            double v = background + (extra ? extra(t) : 0.0);
            if (lampLit(i, t) && !(blocked && blocked(i, t))) v += lampLux;
            v *= 1.0 + 0.02 * gauss(rng);
            sat[i] = v >= saturateAbove;
            lux[i] = (float)std::min(v, saturateAbove);
        }
    }
};

std::vector<LightsEvent> run(LightScene &s, double seconds, uint8_t lamps = 2) {
    FlashDetector d(lamps);
    std::vector<LightsEvent> events;
    for (uint64_t ms = 0; ms < seconds * 1000; ms += lcfg::kSampleMs) {
        float lux[2];
        bool sat[2];
        s.read(ms / 1000.0, lux, sat);
        d.onSample(kEpochMs + ms, lux, sat);
        if (d.hasEvent()) events.push_back(d.takeEvent());
    }
    return events;
}

double relS(uint64_t ms) { return (double)(ms - kEpochMs) / 1000.0; }

// ---- Tests ----

void testTypicalActivation() {
    LightScene s;
    s.on = 60.0;  // 30 s warning, then a 90 s train
    s.off = 180.0;
    auto ev = run(s, 240.0);
    CHECK(ev.size() == 1);
    if (ev.empty()) return;
    CHECK_NEAR(relS(ev[0].startMs), 60.0, 0.1);  // the very first flash, not when it was confirmed
    CHECK_NEAR(relS(ev[0].endMs), 180.0, 0.7);
    CHECK_NEAR(ev[0].flashesPerMin, 50.0, 1.0);
    CHECK_NEAR(ev[0].flashes, 100, 1);  // 120 s at 50 per minute
    CHECK(ev[0].alternating);
    CHECK(ev[0].lamps == 2);
}

void testNightAndFlashRates() {
    for (double fpm : {35.0, 65.0}) {
        LightScene s;
        s.background = 0.5;  // dark, lamp seen against the night
        s.lampLux = 40.0;
        s.fpm = fpm;
        s.on = 20.0;
        s.off = 80.0;
        auto ev = run(s, 100.0);
        CHECK(ev.size() == 1);
        if (!ev.empty()) CHECK_NEAR(ev[0].flashesPerMin, fpm, 1.5);
    }
}

void testNotFooledByStrayLight() {
    // Clouds drifting across the sun
    LightScene clouds;
    clouds.extra = [](double t) { return 2000.0 + 1500.0 * std::sin(t / 7.0) + 600.0 * std::sin(t * 1.3); };
    CHECK(run(clouds, 180.0).empty());

    // Something blinking at a crossing-light rate that lights both sensors at
    // once, like a reflection or a turn signal
    LightScene together;
    together.alternate = false;
    together.on = 10.0;
    together.off = 100.0;
    CHECK(run(together, 120.0).empty());
    CHECK(run(together, 120.0, 1).size() == 1);  // what the second lamp guards against

    // Hazard lights, too fast for a crossing
    LightScene hazards;
    hazards.fpm = 120.0;
    hazards.on = 10.0;
    hazards.off = 100.0;
    CHECK(run(hazards, 120.0).empty());

    // Headlights sweeping past
    LightScene cars;
    cars.extra = [](double t) {
        double p = std::fmod(t, 25.0);
        return (p > 5.0 && p < 8.0) ? 900.0 : 0.0;
    };
    CHECK(run(cars, 180.0).empty());
}

void testSaturatedSensor() {
    // Low sun straight into one hood: that sensor reads flat at its limit, so
    // two-lamp mode can't confirm the lamps alternating
    LightScene s;
    s.on = 20.0;
    s.off = 80.0;
    FlashDetector d(2);
    bool fired = false;
    for (uint64_t ms = 0; ms < 100000; ms += lcfg::kSampleMs) {
        float lux[2];
        bool sat[2];
        s.read(ms / 1000.0, lux, sat);
        lux[0] = 65535.0f;
        sat[0] = true;
        d.onSample(kEpochMs + ms, lux, sat);
        fired = fired || d.active() || d.hasEvent();
    }
    CHECK(!fired);
}

void testOneLampBlockedMidway() {
    // A truck hides the left lamp for 15 s after the event has started
    LightScene s;
    s.on = 30.0;
    s.off = 150.0;
    s.blocked = [](int lamp, double t) { return lamp == 0 && t >= 60.0 && t < 75.0; };
    auto ev = run(s, 200.0);
    CHECK(ev.size() == 1);
    if (!ev.empty()) {
        CHECK_NEAR(relS(ev[0].startMs), 30.0, 0.1);
        CHECK_NEAR(relS(ev[0].endMs), 150.0, 0.7);
    }
}

void testSingleLamp() {
    LightScene s;
    s.on = 30.0;
    s.off = 90.0;
    auto ev = run(s, 120.0, 1);
    CHECK(ev.size() == 1);
    if (!ev.empty()) {
        CHECK_NEAR(relS(ev[0].startMs), 30.0, 0.1);
        CHECK(!ev[0].alternating);
        CHECK(ev[0].lamps == 1);
    }
}

void testBackToBack() {
    // Two activations 40 s apart give two events
    LightScene s;
    s.on = 20.0;
    s.off = 80.0;
    FlashDetector d(2);
    std::vector<LightsEvent> ev;
    for (uint64_t ms = 0; ms < 220000; ms += lcfg::kSampleMs) {
        double t = ms / 1000.0;
        if (t >= 120.0 && s.on < 100.0) {
            s.on = 120.0;
            s.off = 180.0;
        }
        float lux[2];
        bool sat[2];
        s.read(t, lux, sat);
        d.onSample(kEpochMs + ms, lux, sat);
        if (d.hasEvent()) ev.push_back(d.takeEvent());
    }
    CHECK(ev.size() == 2);
    if (ev.size() == 2) {
        CHECK_NEAR(relS(ev[0].startMs), 20.0, 0.1);
        CHECK_NEAR(relS(ev[1].startMs), 120.0, 0.1);
    }
}

void testJson() {
    LightsEvent e;
    e.startMs = kEpochMs + 1234;
    e.endMs = e.startMs + 95000;
    e.flashes = 79;
    e.flashesPerMin = 50.04f;
    e.alternating = true;
    e.lamps = 2;
    char buf[256];
    CHECK(lightsEventToJson(e, 5, 120, buf, sizeof(buf)) > 0);
    CHECK(std::strcmp(buf, "{\"v\":1,\"kind\":\"lights\",\"node\":5,\"dist_ft\":120,\"t0\":1760000001,\"t0_ms\":234,"
                           "\"flash\":{\"s\":0,\"e\":95000,\"n\":79,\"fpm\":50.0,\"alt\":true,\"lamps\":2}}") == 0);
    char small[20];
    CHECK(lightsEventToJson(e, 5, 120, small, sizeof(small)) == 0);
}

}  // namespace

int main() {
    testTypicalActivation();
    testNightAndFlashRates();
    testNotFooledByStrayLight();
    testSaturatedSensor();
    testOneLampBlockedMidway();
    testSingleLamp();
    testBackToBack();
    testJson();
    std::printf("lights: %d checks, %d failures\n", checks, failures);
    return failures ? 1 : 0;
}
