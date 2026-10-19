#include "Detectors.h"

#include <algorithm>
#include <cmath>

// ---- Horn ----

bool hasHornPattern(const HornBlast *blasts, size_t count) {
    for (size_t i = 0; i + 3 < count; i++) {
        if (blasts[i].isLong && blasts[i + 1].isLong && !blasts[i + 2].isLong && blasts[i + 3].isLong) {
            return true;
        }
    }
    return false;
}

void HornDetector::onFrame(uint64_t tMs, uint32_t frameMs, const FrameFeatures &f, float dba,
                           float ambientDba) {
    bool isHorn = f.hornRatio >= cfg::kHornMinBandRatio && f.hornFlatness <= cfg::kHornMaxFlatness &&
                  dba >= ambientDba + cfg::kHornMinRiseDb;
    lastFrameHorn_ = isHorn;

    if (isHorn) {
        if (blastOpen_ && tMs <= blastLastEnd_ + cfg::kHornGapFillMs) {
            blastLastEnd_ = tMs + frameMs;
        } else {
            if (blastOpen_) {
                closeBlast();
            }
            blastOpen_ = true;
            blastStart_ = tMs;
            blastLastEnd_ = tMs + frameMs;
        }
        blastPeakDba_ = std::max(blastPeakDba_, dba);
        return;
    }

    if (blastOpen_ && tMs > blastLastEnd_ + cfg::kHornGapFillMs) {
        closeBlast();
    }
    if (seqOpen_ && !blastOpen_ && tMs > seq_.endMs + cfg::kHornSequenceEndMs) {
        seq_.pattern = hasHornPattern(seq_.blasts, seq_.count);
        done_ = seq_;
        ready_ = true;
        seqOpen_ = false;
    }
}

void HornDetector::closeBlast() {
    blastOpen_ = false;
    uint32_t dur = (uint32_t)(blastLastEnd_ - blastStart_);
    float peak = blastPeakDba_;
    blastPeakDba_ = 0.0f;
    if (dur < cfg::kHornMinBlastMs) {
        return;
    }
    if (!seqOpen_) {
        seq_ = HornSequence();
        seq_.startMs = blastStart_;
        seqOpen_ = true;
    }
    seq_.peakDba = std::max(seq_.peakDba, peak);
    if (seq_.count < cfg::kMaxBlasts) {
        HornBlast &b = seq_.blasts[seq_.count++];
        b.startMs = blastStart_;
        b.durMs = dur;
        b.isLong = dur >= cfg::kHornLongBlastMs;
    }
    seq_.total++;
    seq_.endMs = blastLastEnd_;
}

HornSequence HornDetector::takeSequence() {
    ready_ = false;
    return done_;
}

// ---- Bell ----

void BellDetector::onFrame(uint64_t tMs, const FrameFeatures &f, float dba, float ambientDba) {
    // Once the bell is established, keep counting strikes even when the horn
    // or train dominates the spectrum.
    bool loudEnough = dba >= ambientDba + cfg::kBellMinRiseDb;
    bool bellish = loudEnough && (active_ || f.bellRatio >= cfg::kBellMinBandRatio);
    bool jump = f.bellEnergyDb - prevEnergyDb_ >= cfg::kBellOnsetRiseDb;
    prevEnergyDb_ = f.bellEnergyDb;

    if (bellish && jump && (lastStrike_ == 0 || tMs >= lastStrike_ + cfg::kBellRefractoryMs)) {
        onStrike(tMs);
    }

    if (active_ && tMs > lastStrike_ + cfg::kBellEndMs) {
        current_.endMs = lastStrike_;
        current_.meanIntervalMs = current_.strikes > 1
                                      ? (uint32_t)(intervalSum_ / (current_.strikes - 1))
                                      : 0;
        done_ = current_;
        ready_ = true;
        active_ = false;
        intervalCount_ = 0;
    }
}

void BellDetector::onStrike(uint64_t tMs) {
    uint32_t interval = lastStrike_ ? (uint32_t)(tMs - lastStrike_) : 0;
    bool inRange = interval >= cfg::kBellMinIntervalMs && interval <= cfg::kBellMaxIntervalMs;
    lastStrike_ = tMs;

    if (active_) {
        current_.strikes++;
        intervalSum_ += interval;
        return;
    }

    if (!inRange) {
        runStart_ = tMs;
        intervalCount_ = 0;
        return;
    }

    // Keep the most recent intervals of the run
    if (intervalCount_ == kMaxRun) {
        std::copy(intervals_ + 1, intervals_ + kMaxRun, intervals_);
        intervalCount_--;
    }
    intervals_[intervalCount_++] = interval;

    if (intervalCount_ + 1 >= cfg::kBellMinStrikes && runIsSteady()) {
        active_ = true;
        current_ = BellEvent();
        current_.startMs = runStart_;
        current_.strikes = (uint32_t)intervalCount_ + 1;
        intervalSum_ = 0;
        for (size_t i = 0; i < intervalCount_; i++) {
            intervalSum_ += intervals_[i];
        }
    }
}

bool BellDetector::runIsSteady() const {
    float mean = 0.0f;
    for (size_t i = 0; i < intervalCount_; i++) {
        mean += intervals_[i];
    }
    mean /= intervalCount_;
    float var = 0.0f;
    for (size_t i = 0; i < intervalCount_; i++) {
        var += (intervals_[i] - mean) * (intervals_[i] - mean);
    }
    var /= intervalCount_;
    return std::sqrt(var) / mean <= cfg::kBellMaxIntervalJitter;
}

BellEvent BellDetector::takeEvent() {
    ready_ = false;
    return done_;
}

// ---- Train ----

void TrainDetector::onSecond(uint64_t tMs, float leqDba, float ambientDba, float otherFraction) {
    bool trainLike = otherFraction >= cfg::kTrainMinOtherFraction;
    if (!active_) {
        bool elevated = leqDba >= ambientDba + cfg::kTrainMinRiseDb && trainLike;
        if (elevated) {
            if (run_ == 0) {
                runStart_ = tMs;
                leq_.reset();
                maxLeq_ = 0.0f;
            }
            run_++;
            dips_ = 0;
            leq_.add(leqDba);
            maxLeq_ = std::max(maxLeq_, leqDba);
            if (run_ >= cfg::kTrainConfirmSec) {
                active_ = true;
                quietRun_ = 0;
            }
        } else if (run_ > 0) {
            // Tolerate a single one-second dip while confirming
            if (++dips_ > 1) {
                run_ = 0;
                dips_ = 0;
            } else {
                leq_.add(leqDba);
            }
        }
        return;
    }

    if (leqDba <= ambientDba + cfg::kTrainEndRiseDb || !trainLike) {
        if (quietRun_ == 0) {
            quietStart_ = tMs;
        }
        if (++quietRun_ >= cfg::kTrainEndSec) {
            done_.startMs = runStart_;
            done_.endMs = quietStart_;
            done_.leqDba = leq_.leq();
            done_.maxDba = maxLeq_;
            ready_ = true;
            active_ = false;
            run_ = 0;
            dips_ = 0;
        }
    } else {
        quietRun_ = 0;
        leq_.add(leqDba);
        maxLeq_ = std::max(maxLeq_, leqDba);
    }
}

TrainEvent TrainDetector::takeEvent() {
    ready_ = false;
    return done_;
}
