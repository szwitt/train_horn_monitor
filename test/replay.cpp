// Replays a recording through the node's detection pipeline on a PC and
// prints each crossing report as the JSON the node would publish.
//
//   replay <audio.wav> <meter.csv> [node] [dist_ft]
//
// audio.wav: mono or stereo 16-bit PCM at any rate (resampled to the mic rate).
// meter.csv: "t_ms,dba" rows, the Gravity meter level every 125 ms.

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <utility>
#include <vector>

#include "Episode.h"
#include "NodeProcessor.h"
#include "config.h"

namespace {

constexpr uint64_t kEpochMs = 1760000000000ULL;  // fixed start so output is repeatable

uint32_t le32(const unsigned char *p) { return p[0] | (p[1] << 8) | (p[2] << 16) | ((uint32_t)p[3] << 24); }
uint16_t le16(const unsigned char *p) { return (uint16_t)(p[0] | (p[1] << 8)); }

bool readWav(const char *path, std::vector<float> &out, uint32_t &rate) {
    FILE *f = std::fopen(path, "rb");
    if (!f) return false;
    std::vector<unsigned char> d;
    unsigned char buf[65536];
    size_t n;
    while ((n = std::fread(buf, 1, sizeof(buf), f)) > 0) d.insert(d.end(), buf, buf + n);
    std::fclose(f);
    if (d.size() < 12 || std::memcmp(d.data(), "RIFF", 4) || std::memcmp(d.data() + 8, "WAVE", 4)) return false;

    uint16_t channels = 0, bits = 0, format = 0;
    for (size_t pos = 12; pos + 8 <= d.size();) {
        uint32_t len = le32(&d[pos + 4]);
        const unsigned char *body = &d[pos + 8];
        if (pos + 8 + len > d.size()) len = (uint32_t)(d.size() - pos - 8);
        if (!std::memcmp(&d[pos], "fmt ", 4) && len >= 16) {
            format = le16(body);
            channels = le16(body + 2);
            rate = le32(body + 4);
            bits = le16(body + 14);
        } else if (!std::memcmp(&d[pos], "data", 4)) {
            if (format != 1 || bits != 16 || channels == 0) return false;
            size_t frames = len / (2 * channels);
            out.resize(frames);
            for (size_t i = 0; i < frames; i++) {
                int16_t s = (int16_t)le16(body + 2 * channels * i);  // first channel
                out[i] = s / 32768.0f;
            }
            return true;
        }
        pos += 8 + len + (len & 1);
    }
    return false;
}

bool readMeter(const char *path, std::vector<std::pair<uint32_t, float>> &rows) {
    FILE *f = std::fopen(path, "r");
    if (!f) return false;
    char line[128];
    while (std::fgets(line, sizeof(line), f)) {
        unsigned t;
        float db;
        if (std::sscanf(line, "%u,%f", &t, &db) == 2) rows.emplace_back(t, db);
    }
    std::fclose(f);
    return !rows.empty();
}

}  // namespace

int main(int argc, char **argv) {
    if (argc < 3) {
        std::fprintf(stderr, "usage: %s audio.wav meter.csv [node] [dist_ft]\n", argv[0]);
        return 2;
    }
    int node = argc > 3 ? std::atoi(argv[3]) : 1;
    int dist = argc > 4 ? std::atoi(argv[4]) : 50;

    std::vector<float> audio;
    uint32_t rate = 0;
    if (!readWav(argv[1], audio, rate)) {
        std::fprintf(stderr, "can't read %s (need 16-bit PCM WAV)\n", argv[1]);
        return 1;
    }
    std::vector<std::pair<uint32_t, float>> meter;
    if (!readMeter(argv[2], meter)) {
        std::fprintf(stderr, "can't read meter levels from %s\n", argv[2]);
        return 1;
    }

    NodeProcessor proc;
    std::vector<float> frame(cfg::kFrameSamples);
    const double step = (double)rate / cfg::kMicSampleRateHz;  // input samples per mic sample
    const double frameSec = cfg::kFrameSamples / cfg::kMicSampleRateHz;
    const double seconds = audio.size() / (double)rate;
    size_t nextMeter = 0;
    int reports = 0;
    char json[1024];

    auto emit = [&]() {
        while (proc.hasReport()) {
            CrossingReport r = proc.takeReport();
            if (reportToJson(r, node, dist, json, sizeof(json))) {
                std::printf("%s\n", json);
                reports++;
            }
        }
    };

    double pos = 0.0;  // position in input samples
    for (double t = 0.0; t + frameSec <= seconds; t += frameSec) {
        uint64_t tMs = kEpochMs + (uint64_t)(t * 1000);
        while (nextMeter < meter.size() && meter[nextMeter].first <= t * 1000) {
            proc.onMeterReading(kEpochMs + meter[nextMeter].first, meter[nextMeter].second);
            nextMeter++;
            emit();
        }
        for (size_t i = 0; i < cfg::kFrameSamples; i++, pos += step) {
            size_t k = (size_t)pos;
            float frac = (float)(pos - k);
            float a = k < audio.size() ? audio[k] : 0.0f;
            float b = k + 1 < audio.size() ? audio[k + 1] : a;
            frame[i] = a + (b - a) * frac;
        }
        proc.onAudioFrame(tMs, frame.data());
        emit();
    }
    std::fprintf(stderr, "%s: %.0f s at %u Hz, %d crossing report(s)\n", argv[1], seconds, rate, reports);
    return 0;
}
