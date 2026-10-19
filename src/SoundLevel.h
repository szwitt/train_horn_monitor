#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

// Converts Gravity SEN0232 ADC counts to dB(A).
float adcCountsToDba(float counts, float calOffsetDb);

// Energy-averages dB values: Leq = 10*log10(mean(10^(L/10))).
class LeqAccumulator {
public:
    void add(float db);
    bool empty() const { return count_ == 0; }
    size_t count() const { return count_; }
    float leq() const;
    float max() const { return max_; }
    void reset();

private:
    double energySum_ = 0.0;
    size_t count_ = 0;
    float max_ = -1000.0f;
};

// Rolling background level: a low percentile (L90 by default) of the last
// N one-second Leq values. Updates can be frozen while an event is active
// so trains don't raise the baseline.
class AmbientTracker {
public:
    explicit AmbientTracker(size_t windowSec, float percentile, float initialDba);
    void addSecond(float leqDba);
    float ambient() const { return ambient_; }
    size_t samples() const { return values_.size(); }

private:
    void recompute();

    std::vector<float> values_;
    size_t next_ = 0;
    size_t window_;
    float percentile_;
    float ambient_;
};
