#include "NodeProcessor.h"

#include "config.h"

namespace {
constexpr uint32_t kFrameMs = (uint32_t)(cfg::kFrameSamples * 1000.0f / cfg::kMicSampleRateHz);
}

NodeProcessor::NodeProcessor()
    : spectrum_(cfg::kFrameSamples, cfg::kMicSampleRateHz),
      ambient_(cfg::kAmbientWindowSec, cfg::kAmbientPercentile, cfg::kDefaultAmbientDba) {}

bool NodeProcessor::busy() const {
    return horn_.active() || bell_.active() || train_.active();
}

void NodeProcessor::onMeterReading(uint64_t tMs, float dba) {
    lastDba_ = dba;
    if (secStartMs_ == 0) {
        secStartMs_ = tMs;
    }
    if (tMs >= secStartMs_ + 1000) {
        onSecond(secStartMs_);
        secStartMs_ += 1000;
        if (tMs >= secStartMs_ + 1000) {
            secStartMs_ = tMs;  // clock jumped; resync
        }
    }
    secLeq_.add(dba);
    intervalLeq_.add(dba);

    episode_.tick(tMs, busy(), dba, ambient_.ambient());
}

void NodeProcessor::onAudioFrame(uint64_t tMs, const float *samples) {
    FrameFeatures f = spectrum_.analyze(samples);
    float ambient = ambient_.ambient();

    horn_.onFrame(tMs, kFrameMs, f, lastDba_, ambient);
    bell_.onFrame(tMs, f, lastDba_, ambient);
    secFrames_++;
    if (horn_.lastFrameWasHorn() || f.bellRatio >= cfg::kBellDominantRatio) {
        secSignalFrames_++;
    }

    if (horn_.hasSequence()) {
        episode_.addHorn(horn_.takeSequence());
    }
    if (bell_.hasEvent()) {
        episode_.addBell(bell_.takeEvent());
    }
}

void NodeProcessor::onSecond(uint64_t secStartMs) {
    if (secLeq_.empty()) {
        return;
    }
    float leq = secLeq_.leq();
    float otherFraction = secFrames_ ? 1.0f - (float)secSignalFrames_ / secFrames_ : 1.0f;
    secLeq_.reset();
    secFrames_ = 0;
    secSignalFrames_ = 0;

    train_.onSecond(secStartMs, leq, ambient_.ambient(), otherFraction);
    if (train_.hasEvent()) {
        episode_.addTrain(train_.takeEvent());
    }

    // Freeze the baseline while anything is happening
    if (!busy() && !episode_.open()) {
        ambient_.addSecond(leq);
    }
}

float NodeProcessor::takeIntervalLeq() {
    float leq = intervalLeq_.leq();
    intervalLeq_.reset();
    return leq;
}
