#pragma once

#include <cstddef>
#include <cstdint>

#include "SoundLevel.h"
#include "Spectrum.h"
#include "config.h"

// All times are epoch milliseconds.

struct HornBlast {
    uint64_t startMs = 0;
    uint32_t durMs = 0;
    bool isLong = false;
};

struct HornSequence {
    uint64_t startMs = 0;  // start of the first blast
    uint64_t endMs = 0;  // end of the last blast
    HornBlast blasts[cfg::kMaxBlasts];
    size_t count = 0;  // blasts stored (capped at kMaxBlasts)
    size_t total = 0;  // blasts heard
    bool pattern = false;  // long-long-short-long found
    float peakDba = 0.0f;
};

// Finds horn blasts frame by frame, groups them into a sequence and checks
// for the FRA long-long-short-long pattern.
class HornDetector {
public:
    void onFrame(uint64_t tMs, uint32_t frameMs, const FrameFeatures &f, float dba, float ambientDba);
    bool active() const { return blastOpen_ || seqOpen_; }
    bool lastFrameWasHorn() const { return lastFrameHorn_; }
    bool hasSequence() const { return ready_; }
    HornSequence takeSequence();

private:
    void closeBlast();

    bool lastFrameHorn_ = false;
    bool blastOpen_ = false;
    uint64_t blastStart_ = 0;
    uint64_t blastLastEnd_ = 0;
    float blastPeakDba_ = 0.0f;
    bool seqOpen_ = false;
    HornSequence seq_;
    bool ready_ = false;
    HornSequence done_;
};

bool hasHornPattern(const HornBlast *blasts, size_t count);

struct BellEvent {
    uint64_t startMs = 0;  // first strike of the run
    uint64_t endMs = 0;  // last strike
    uint32_t strikes = 0;
    uint32_t meanIntervalMs = 0;
};

// Finds the crossing bell by its steady strike rhythm in the bell band.
class BellDetector {
public:
    void onFrame(uint64_t tMs, const FrameFeatures &f, float dba, float ambientDba);
    bool active() const { return active_; }
    bool hasEvent() const { return ready_; }
    BellEvent takeEvent();

private:
    void onStrike(uint64_t tMs);
    bool runIsSteady() const;

    static constexpr size_t kMaxRun = 8;
    float prevEnergyDb_ = -120.0f;
    uint64_t lastStrike_ = 0;
    uint64_t runStart_ = 0;
    uint32_t intervals_[kMaxRun] = {};
    size_t intervalCount_ = 0;  // consecutive in-range intervals in the run
    bool active_ = false;
    BellEvent current_;
    uint64_t intervalSum_ = 0;
    bool ready_ = false;
    BellEvent done_;
};

struct TrainEvent {
    uint64_t startMs = 0;
    uint64_t endMs = 0;
    float leqDba = 0.0f;  // energy average over the passage
    float maxDba = 0.0f;  // loudest 1 s Leq
};

// Finds a train passing from one-second Leq values: a sustained rise over
// ambient that is not just the horn or the bell.
class TrainDetector {
public:
    // otherFraction: share of the second's audio frames that were neither
    // horn- nor bell-dominated.
    void onSecond(uint64_t tMs, float leqDba, float ambientDba, float otherFraction);
    bool active() const { return active_ || run_ > 0; }
    bool confirmed() const { return active_; }
    bool hasEvent() const { return ready_; }
    TrainEvent takeEvent();

private:
    uint32_t run_ = 0;
    uint32_t dips_ = 0;
    uint64_t runStart_ = 0;
    bool active_ = false;
    uint32_t quietRun_ = 0;
    uint64_t quietStart_ = 0;
    LeqAccumulator leq_;
    float maxLeq_ = 0.0f;
    bool ready_ = false;
    TrainEvent done_;
};
