#include "FlashDetector.h"

#include <algorithm>
#include <cstdio>

void LampTracker::onSample(uint64_t tMs, float lux, bool saturated) {
    window_[next_] = lux;
    next_ = (next_ + 1) % kWindow;
    count_ = std::min(count_ + 1, kWindow);

    float lo = window_[0], hi = window_[0];
    for (size_t i = 1; i < count_; i++) {
        lo = std::min(lo, window_[i]);
        hi = std::max(hi, window_[i]);
    }
    float swing = hi - lo;
    usable_ = !saturated && swing >= lcfg::kMinSwingLux && swing >= lcfg::kMinContrast * hi;
    if (!usable_) {
        lit_ = false;  // so the first flash after a flat stretch counts as a rise
    } else if (!lit_ && lux >= lo + lcfg::kOnThreshold * swing) {
        lit_ = true;
        if (lastRiseMs_ != 0) {
            uint64_t period = tMs - lastRiseMs_;
            if (period >= lcfg::kMinPeriodMs && period <= lcfg::kMaxPeriodMs) {
                steady_++;
            } else {
                steady_ = 0;
            }
        }
        if (steady_ == 0) {
            runStartMs_ = tMs;
            runFlashes_ = 0;
        }
        runFlashes_++;
        lastRiseMs_ = tMs;
        rises_++;
    } else if (lit_ && lux <= lo + lcfg::kOffThreshold * swing) {
        lit_ = false;
    }
    if (lit_) {
        lastLitMs_ = tMs;
    }
    if (lastRiseMs_ != 0 && tMs - lastRiseMs_ > lcfg::kLampLostMs) {
        steady_ = 0;
    }
}

bool LampTracker::flashing(uint64_t tMs) const {
    return steady_ >= lcfg::kConfirmCycles && tMs - lastRiseMs_ <= lcfg::kLampLostMs;
}

FlashDetector::FlashDetector(uint8_t lamps) : lamps_(std::max<uint8_t>(1, std::min<uint8_t>(lamps, lcfg::kMaxLamps))) {}

float FlashDetector::alternation() const {
    size_t comparable = 0, oneLit = 0;
    for (uint8_t a : alt_) {
        comparable += a != 0;
        oneLit += a == 1;
    }
    return comparable ? (float)oneLit / comparable : 0.0f;
}

void FlashDetector::onSample(uint64_t tMs, const float *lux, const bool *saturated) {
    for (uint8_t i = 0; i < lamps_; i++) {
        lamp_[i].onSample(tMs, lux[i], saturated[i]);
    }
    if (lamps_ == 2) {
        bool both = lamp_[0].usable() && lamp_[1].usable();
        alt_[altNext_] = !both ? 0 : (lamp_[0].lit() != lamp_[1].lit() ? 1 : 2);
        altNext_ = (altNext_ + 1) % kAltWindow;
    }

    bool any = false, all = true;
    uint64_t start = UINT64_MAX;
    for (uint8_t i = 0; i < lamps_; i++) {
        bool f = lamp_[i].flashing(tMs);
        any = any || f;
        all = all && f;
        if (f) {
            start = std::min(start, lamp_[i].runStartMs());
        }
    }

    if (!active_) {
        bool confirmed = lamps_ == 1 ? any : (all && alternation() >= lcfg::kMinAlternation);
        if (!confirmed) {
            return;
        }
        active_ = true;
        alternating_ = lamps_ == 2;
        lastFlashingMs_ = tMs;
        cur_ = LightsEvent();
        cur_.startMs = start;
        cur_.lamps = lamps_;
        for (uint8_t i = 0; i < lamps_; i++) {
            bool f = lamp_[i].flashing(tMs);
            count_[i] = f ? lamp_[i].runFlashes() : 0;
            firstRiseMs_[i] = f ? lamp_[i].runStartMs() : 0;
            mark_[i] = lamp_[i].rises();
        }
        return;
    }

    for (uint8_t i = 0; i < lamps_; i++) {
        uint32_t fresh = lamp_[i].rises() - mark_[i];
        if (fresh) {
            if (count_[i] == 0) {
                firstRiseMs_[i] = lamp_[i].lastRiseMs();
            }
            count_[i] += fresh;
            mark_[i] = lamp_[i].rises();
        }
    }
    if (any) {
        lastFlashingMs_ = tMs;
    } else if (tMs - lastFlashingMs_ >= lcfg::kEndMs) {
        finish();
    }
}

void FlashDetector::finish() {
    uint64_t end = 0;
    uint8_t best = 0;
    for (uint8_t i = 0; i < lamps_; i++) {
        end = std::max(end, lamp_[i].lastLitMs());
        if (count_[i] > count_[best]) {
            best = i;
        }
    }
    cur_.endMs = std::max(end, cur_.startMs);
    cur_.flashes = count_[best];
    uint64_t span = lamp_[best].lastRiseMs() - firstRiseMs_[best];
    cur_.flashesPerMin = count_[best] > 1 && span > 0 ? 60000.0f * (count_[best] - 1) / (float)span : 0.0f;
    cur_.alternating = alternating_;
    done_ = cur_;
    ready_ = true;
    active_ = false;
}

LightsEvent FlashDetector::takeEvent() {
    ready_ = false;
    return done_;
}

size_t lightsEventToJson(const LightsEvent &e, int nodeId, int distanceFt, char *buf, size_t bufSize) {
    int n = snprintf(buf, bufSize,
                     "{\"v\":1,\"kind\":\"lights\",\"node\":%d,\"dist_ft\":%d,\"t0\":%lu,\"t0_ms\":%u,"
                     "\"flash\":{\"s\":0,\"e\":%lu,\"n\":%lu,\"fpm\":%.1f,\"alt\":%s,\"lamps\":%u}}",
                     nodeId, distanceFt, (unsigned long)(e.startMs / 1000), (unsigned)(e.startMs % 1000),
                     (unsigned long)(e.endMs - e.startMs), (unsigned long)e.flashes, e.flashesPerMin,
                     e.alternating ? "true" : "false", (unsigned)e.lamps);
    return (n < 0 || (size_t)n >= bufSize) ? 0 : (size_t)n;
}
