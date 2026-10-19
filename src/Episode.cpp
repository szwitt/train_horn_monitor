#include "Episode.h"

#include <algorithm>
#include <cmath>
#include <cstdarg>
#include <cstdio>

void EpisodeBuilder::ensureOpen(uint64_t nowMs, float ambientDba) {
    if (open_) {
        return;
    }
    open_ = true;
    cur_ = CrossingReport();
    cur_.t0Ms = nowMs;
    cur_.ambientDba = ambientDba;
    energySec_ = 0.0;
    lastActivityMs_ = nowMs;
}

void EpisodeBuilder::tick(uint64_t nowMs, bool anyActive, float dba, float ambientDba) {
    if (anyActive) {
        ensureOpen(nowMs, ambientDba);
        lastActivityMs_ = nowMs;
    }
    if (!open_) {
        return;
    }
    cur_.peakDba = std::max(cur_.peakDba, dba);
    if (anyActive) {
        energySec_ += std::pow(10.0, dba / 10.0) * (cfg::kMeterReadingMs / 1000.0);
        cur_.activeMs += cfg::kMeterReadingMs;
    }

    uint32_t closeAfter = cur_.hasTrain ? cfg::kEpisodeIdleCloseMs : cfg::kEpisodeNoTrainCloseMs;
    if (!anyActive && nowMs >= lastActivityMs_ + closeAfter) {
        cur_.selDba = energySec_ > 0.0 ? (float)(10.0 * std::log10(energySec_)) : 0.0f;
        done_ = cur_;
        ready_ = true;
        open_ = false;
    }
}

void EpisodeBuilder::addBell(const BellEvent &e) {
    ensureOpen(e.startMs, cur_.ambientDba);
    cur_.t0Ms = std::min(cur_.t0Ms, e.startMs);
    if (!cur_.hasBell) {
        cur_.hasBell = true;
        cur_.bell = e;
    }
}

void EpisodeBuilder::addHorn(const HornSequence &s) {
    ensureOpen(s.startMs, cur_.ambientDba);
    cur_.t0Ms = std::min(cur_.t0Ms, s.startMs);
    if (!cur_.hasHorn) {
        cur_.hasHorn = true;
        cur_.horn = s;
    } else {
        cur_.extraHornSequences++;
    }
}

void EpisodeBuilder::addTrain(const TrainEvent &t) {
    ensureOpen(t.startMs, cur_.ambientDba);
    cur_.t0Ms = std::min(cur_.t0Ms, t.startMs);
    if (!cur_.hasTrain) {
        cur_.hasTrain = true;
        cur_.train = t;
    }
}

CrossingReport EpisodeBuilder::takeReport() {
    ready_ = false;
    return done_;
}

namespace {

// Appends formatted text, tracking overflow.
struct JsonWriter {
    char *buf;
    size_t size;
    size_t len = 0;
    bool overflow = false;

    void add(const char *fmt, ...) {
        if (overflow) {
            return;
        }
        va_list ap;
        va_start(ap, fmt);
        int n = vsnprintf(buf + len, size - len, fmt, ap);
        va_end(ap);
        if (n < 0 || (size_t)n >= size - len) {
            overflow = true;
            return;
        }
        len += (size_t)n;
    }
};

long rel(uint64_t t, uint64_t t0) {
    return (long)((int64_t)t - (int64_t)t0);
}

}  // namespace

size_t reportToJson(const CrossingReport &r, int nodeId, int distanceFt, char *buf, size_t bufSize) {
    JsonWriter w{buf, bufSize};
    w.add("{\"v\":1,\"node\":%d,\"dist_ft\":%d,\"t0\":%lu,\"t0_ms\":%u,\"amb\":%.1f,\"peak\":%.1f,\"sel\":%.1f,\"act\":%lu",
          nodeId, distanceFt, (unsigned long)(r.t0Ms / 1000), (unsigned)(r.t0Ms % 1000), r.ambientDba, r.peakDba,
          r.selDba, (unsigned long)r.activeMs);

    if (r.hasBell) {
        w.add(",\"bell\":{\"s\":%ld,\"e\":%ld,\"n\":%u,\"iv\":%u}", rel(r.bell.startMs, r.t0Ms),
              rel(r.bell.endMs, r.t0Ms), (unsigned)r.bell.strikes, (unsigned)r.bell.meanIntervalMs);
    }
    if (r.hasHorn) {
        w.add(",\"horn\":{\"s\":%ld,\"e\":%ld,\"pat\":%s,\"peak\":%.1f,\"n\":%u,\"more\":%u,\"b\":[",
              rel(r.horn.startMs, r.t0Ms), rel(r.horn.endMs, r.t0Ms), r.horn.pattern ? "true" : "false",
              r.horn.peakDba, (unsigned)r.horn.total, (unsigned)r.extraHornSequences);
        for (size_t i = 0; i < r.horn.count; i++) {
            const HornBlast &b = r.horn.blasts[i];
            w.add("%s[%ld,%u]", i ? "," : "", rel(b.startMs, r.t0Ms), (unsigned)b.durMs);
        }
        w.add("]}");
    }
    if (r.hasTrain) {
        w.add(",\"train\":{\"s\":%ld,\"e\":%ld,\"leq\":%.1f,\"max\":%.1f}", rel(r.train.startMs, r.t0Ms),
              rel(r.train.endMs, r.t0Ms), r.train.leqDba, r.train.maxDba);
    }
    w.add("}");
    return w.overflow ? 0 : w.len;
}
