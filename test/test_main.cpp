// Host tests for the node's detection pipeline. Build and run with `make -C test`.
// Signals are synthetic stand-ins for the bell, horn and train; real
// thresholds still need tuning from field recordings.

#include <cmath>
#include <cstdio>
#include <cstring>
#include <random>
#include <string>
#include <vector>

#include "Detectors.h"
#include "Episode.h"
#include "NodeProcessor.h"
#include "SoundLevel.h"
#include "Spectrum.h"
#include "config.h"

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

constexpr float kPi = 3.14159265358979f;
const float kFs = cfg::kMicSampleRateHz;
const size_t kN = cfg::kFrameSamples;

// ---- Synthetic sound sources ----

struct Scene {
    std::mt19937 rng{42};
    std::normal_distribution<float> gauss{0.0f, 1.0f};
    float rumbleState = 0.0f;
    uint64_t sampleIndex = 0;

    // Horn: three-note chord, on during [start, end) of each blast
    std::vector<std::pair<double, double>> blasts;  // seconds
    // Bell: decaying 2.1 kHz strikes every bellPeriod seconds in [bellOn, bellOff)
    double bellOn = -1, bellOff = -1, bellPeriod = 0.33;
    // Train rumble in [trainOn, trainOff)
    double trainOn = -1, trainOff = -1;

    bool hornAt(double t) const {
        for (auto &b : blasts) {
            if (t >= b.first && t < b.second) return true;
        }
        return false;
    }
    bool bellAt(double t) const { return t >= bellOn && t < bellOff; }
    bool trainAt(double t) const { return t >= trainOn && t < trainOff; }

    float sample(double t) {
        float s = 0.002f * gauss(rng);  // background hiss
        if (hornAt(t)) {
            s += 0.15f * (std::sin(2 * kPi * 311.0 * t) + std::sin(2 * kPi * 370.0 * t) +
                          std::sin(2 * kPi * 466.0 * t));
        }
        if (bellAt(t)) {
            double since = std::fmod(t - bellOn, bellPeriod);
            s += 0.3f * std::exp(-since * 12.0) * std::sin(2 * kPi * 2100.0 * t);
        }
        if (trainAt(t)) {
            rumbleState += 0.06f * (gauss(rng) - rumbleState);
            s += 1.2f * rumbleState;
        }
        return s;
    }

    // Meter level the Gravity sensor would report
    float meterDba(double t) const {
        double e = std::pow(10.0, 50.0 / 10);  // 50 dBA ambient
        if (bellAt(t)) e += std::pow(10.0, 72.0 / 10);
        if (trainAt(t)) e += std::pow(10.0, 85.0 / 10);
        if (hornAt(t)) e += std::pow(10.0, 98.0 / 10);
        return (float)(10.0 * std::log10(e));
    }

    void frame(double t0, float *out) {
        for (size_t i = 0; i < kN; i++) {
            out[i] = sample(t0 + i / kFs);
        }
    }
};

// Runs a scene through the node pipeline for `seconds`, returns reports.
std::vector<CrossingReport> runScene(Scene &scene, double seconds, uint64_t epochMs) {
    NodeProcessor node;
    std::vector<CrossingReport> reports;
    std::vector<float> buf(kN);
    double frameSec = kN / kFs;
    double nextMeter = 0.0;
    for (double t = 0.0; t < seconds; t += frameSec) {
        while (nextMeter <= t) {
            node.onMeterReading(epochMs + (uint64_t)(nextMeter * 1000), scene.meterDba(nextMeter));
            nextMeter += cfg::kMeterReadingMs / 1000.0;
        }
        scene.frame(t, buf.data());
        node.onAudioFrame(epochMs + (uint64_t)(t * 1000), buf.data());
        if (node.hasReport()) {
            reports.push_back(node.takeReport());
        }
    }
    return reports;
}

// ---- Tests ----

void testFft() {
    std::vector<float> re(64), im(64, 0.0f);
    for (size_t i = 0; i < 64; i++) re[i] = std::cos(2 * kPi * 5 * i / 64.0f);
    fftRadix2(re.data(), im.data(), 64);
    float peak = 0;
    size_t peakBin = 0;
    for (size_t k = 0; k < 32; k++) {
        float p = re[k] * re[k] + im[k] * im[k];
        if (p > peak) {
            peak = p;
            peakBin = k;
        }
    }
    CHECK(peakBin == 5);
    CHECK_NEAR(std::sqrt(peak), 32.0, 0.01);
}

void testSpectrumFeatures() {
    SpectrumAnalyzer sa(kN, kFs);
    Scene horn;
    horn.blasts = {{0.0, 10.0}};
    std::vector<float> buf(kN);
    horn.frame(1.0, buf.data());
    FrameFeatures fh = sa.analyze(buf.data());
    CHECK(fh.hornRatio > 0.9f);
    CHECK(fh.hornFlatness < 0.1f);

    Scene noise;  // hiss only: flat spectrum
    noise.frame(1.0, buf.data());
    FrameFeatures fn = sa.analyze(buf.data());
    CHECK(fn.hornFlatness > 0.3f);
    CHECK(fn.hornRatio < 0.2f);

    Scene bell;
    bell.bellOn = 0.0;
    bell.bellOff = 10.0;
    bell.frame(0.0, buf.data());
    FrameFeatures fb = sa.analyze(buf.data());
    CHECK(fb.bellRatio > 0.8f);
}

void testHornPattern() {
    HornBlast b[5];
    bool longs[5] = {true, true, false, true, true};
    for (int i = 0; i < 5; i++) b[i].isLong = longs[i];
    CHECK(hasHornPattern(b, 4));
    CHECK(hasHornPattern(b, 5));
    bool other[4] = {true, false, true, true};
    for (int i = 0; i < 4; i++) b[i].isLong = other[i];
    CHECK(!hasHornPattern(b, 4));
}

void testLeqAndAmbient() {
    LeqAccumulator acc;
    acc.add(60.0f);
    acc.add(60.0f);
    CHECK_NEAR(acc.leq(), 60.0, 0.01);
    acc.add(70.0f);  // energy average, dominated by the louder value
    CHECK_NEAR(acc.leq(), 10 * std::log10((2 * 1e6 + 1e7) / 3.0), 0.01);

    AmbientTracker amb(10, 0.1f, 45.0f);
    CHECK_NEAR(amb.ambient(), 45.0, 0.01);
    for (int i = 0; i < 9; i++) amb.addSecond(50.0f);
    amb.addSecond(90.0f);  // one loud second doesn't move L90
    CHECK_NEAR(amb.ambient(), 50.0, 0.01);

    CHECK_NEAR(adcCountsToDba(4095.0f * 1.0f / 3.3f, 0.0f), 50.0, 0.01);
    CHECK_NEAR(adcCountsToDba(4095.0f * 1.0f / 3.3f, -2.0f), 48.0, 0.01);
}

// Bell, then a compliant horn sequence, then the train: one report.
void testFullCrossing() {
    Scene s;
    s.bellOn = 100.0;
    s.bellOff = 160.0;
    // long-long-short-long, last blast held until the train arrives
    s.blasts = {{117.0, 119.5}, {120.5, 123.0}, {124.0, 125.0}, {126.0, 133.0}};
    s.trainOn = 133.0;
    s.trainOff = 158.0;

    const uint64_t epoch = 1760000000000ULL;
    auto reports = runScene(s, 260.0, epoch);
    CHECK(reports.size() == 1);
    if (reports.empty()) return;
    const CrossingReport &r = reports[0];

    CHECK(r.hasBell);
    CHECK_NEAR((r.bell.startMs - epoch) / 1000.0, 100.0, 0.5);
    CHECK_NEAR((r.bell.endMs - epoch) / 1000.0, 160.0, 0.6);
    CHECK_NEAR(r.bell.meanIntervalMs, 330, 40);

    CHECK(r.hasHorn);
    CHECK(r.horn.total == 4);
    CHECK(r.horn.pattern);
    CHECK_NEAR((r.horn.startMs - epoch) / 1000.0, 117.0, 0.2);
    CHECK_NEAR(r.horn.peakDba, 98.0, 0.5);

    CHECK(r.hasTrain);
    CHECK_NEAR((r.train.startMs - epoch) / 1000.0, 133.0, 1.5);
    CHECK_NEAR((r.train.endMs - epoch) / 1000.0, 158.0, 1.5);
    CHECK_NEAR(r.ambientDba, 50.0, 1.0);

    // Horn lead time: 16 s, inside the FRA 15 to 20 s window
    double lead = ((double)r.train.startMs - (double)r.horn.startMs) / 1000.0;
    CHECK(lead >= 15.0 && lead <= 20.0);

    char buf[1024];
    size_t n = reportToJson(r, 2, 100, buf, sizeof(buf));
    CHECK(n > 0);
    std::string json(buf);
    CHECK(json.find("\"node\":2") != std::string::npos);
    CHECK(json.find("\"pat\":true") != std::string::npos);
    CHECK(json.find("\"train\":{") != std::string::npos);
    std::printf("sample report (%zu bytes): %s\n", n, buf);
}

// Short-short-short horn (not the FRA pattern), no bell, train passes.
void testNonCompliantHorn() {
    Scene s;
    s.blasts = {{60.0, 60.8}, {61.5, 62.3}, {63.0, 63.8}};
    s.trainOn = 66.0;
    s.trainOff = 90.0;
    auto reports = runScene(s, 200.0, 1760000000000ULL);
    CHECK(reports.size() == 1);
    if (reports.empty()) return;
    CHECK(!reports[0].hasBell);
    CHECK(reports[0].hasHorn);
    CHECK(!reports[0].horn.pattern);
    CHECK(reports[0].hasTrain);
}

// Quiet day: nothing reported.
void testQuiet() {
    Scene s;
    auto reports = runScene(s, 120.0, 1760000000000ULL);
    CHECK(reports.empty());
}

void testJsonOverflow() {
    CrossingReport r;
    r.hasHorn = true;
    r.horn.count = cfg::kMaxBlasts;
    char small[40];
    CHECK(reportToJson(r, 1, 50, small, sizeof(small)) == 0);
    char big[1024];
    CHECK(reportToJson(r, 1, 50, big, sizeof(big)) > 0);
}

}  // namespace

int main() {
    testFft();
    testSpectrumFeatures();
    testHornPattern();
    testLeqAndAmbient();
    testFullCrossing();
    testNonCompliantHorn();
    testQuiet();
    testJsonOverflow();
    std::printf("%d checks, %d failures\n", checks, failures);
    return failures ? 1 : 0;
}
