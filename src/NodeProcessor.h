#pragma once

#include <cstddef>
#include <cstdint>

#include "Detectors.h"
#include "Episode.h"
#include "SoundLevel.h"
#include "Spectrum.h"

// Ties the meter readings and microphone frames to the detectors. Has no
// Particle dependencies so the whole pipeline can be tested on a PC.
class NodeProcessor {
public:
    NodeProcessor();

    // Call every cfg::kMeterReadingMs with the calibrated meter level.
    void onMeterReading(uint64_t tMs, float dba);

    // Call with each microphone frame (cfg::kFrameSamples samples, -1..1).
    void onAudioFrame(uint64_t tMs, const float *samples);

    bool hasReport() const { return episode_.hasReport(); }
    CrossingReport takeReport() { return episode_.takeReport(); }

    float currentDba() const { return lastDba_; }
    float ambientDba() const { return ambient_.ambient(); }
    bool busy() const;

    // Energy average since the last call (for heartbeats)
    float takeIntervalLeq();

private:
    void onSecond(uint64_t secStartMs);

    SpectrumAnalyzer spectrum_;
    AmbientTracker ambient_;
    HornDetector horn_;
    BellDetector bell_;
    TrainDetector train_;
    EpisodeBuilder episode_;

    float lastDba_ = 0.0f;
    uint64_t secStartMs_ = 0;
    LeqAccumulator secLeq_;
    LeqAccumulator intervalLeq_;
    uint32_t secFrames_ = 0;
    uint32_t secSignalFrames_ = 0;  // horn- or bell-dominated frames
};
