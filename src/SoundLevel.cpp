#include "SoundLevel.h"

#include <algorithm>
#include <cmath>

#include "config.h"

float adcCountsToDba(float counts, float calOffsetDb) {
    float volts = counts * cfg::kAdcRefVolts / cfg::kAdcMaxCounts;
    return volts * cfg::kDbPerVolt + calOffsetDb;
}

void LeqAccumulator::add(float db) {
    energySum_ += std::pow(10.0, db / 10.0);
    count_++;
    max_ = std::max(max_, db);
}

float LeqAccumulator::leq() const {
    if (count_ == 0) {
        return 0.0f;
    }
    return (float)(10.0 * std::log10(energySum_ / count_));
}

void LeqAccumulator::reset() {
    energySum_ = 0.0;
    count_ = 0;
    max_ = -1000.0f;
}

AmbientTracker::AmbientTracker(size_t windowSec, float percentile, float initialDba)
    : window_(windowSec), percentile_(percentile), ambient_(initialDba) {
    values_.reserve(windowSec);
}

void AmbientTracker::addSecond(float leqDba) {
    if (values_.size() < window_) {
        values_.push_back(leqDba);
    } else {
        values_[next_] = leqDba;
        next_ = (next_ + 1) % window_;
    }
    recompute();
}

void AmbientTracker::recompute() {
    if (values_.empty()) {
        return;
    }
    std::vector<float> sorted(values_);
    size_t idx = (size_t)(percentile_ * (sorted.size() - 1));
    std::nth_element(sorted.begin(), sorted.begin() + idx, sorted.end());
    ambient_ = sorted[idx];
}
