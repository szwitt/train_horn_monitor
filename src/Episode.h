#pragma once

#include <cstddef>
#include <cstdint>

#include "Detectors.h"

// Everything one node heard around one train: bell, horn and passage.
struct CrossingReport {
    uint64_t t0Ms = 0;  // when the first activity began
    float ambientDba = 0.0f;  // background level just before t0
    float peakDba = 0.0f;  // loudest 125 ms meter reading in the episode
    bool hasBell = false;
    BellEvent bell;
    bool hasHorn = false;
    HornSequence horn;
    uint32_t extraHornSequences = 0;
    bool hasTrain = false;
    TrainEvent train;
};

// Collects detector outputs into one report per crossing and decides when
// the crossing is over.
class EpisodeBuilder {
public:
    // Call on every meter reading with whether any detector is mid-event.
    void tick(uint64_t nowMs, bool anyActive, float dba, float ambientDba);
    void addBell(const BellEvent &e);
    void addHorn(const HornSequence &s);
    void addTrain(const TrainEvent &t);
    bool open() const { return open_; }
    bool hasReport() const { return ready_; }
    CrossingReport takeReport();

private:
    void ensureOpen(uint64_t nowMs, float ambientDba);

    bool open_ = false;
    uint64_t lastActivityMs_ = 0;
    CrossingReport cur_;
    bool ready_ = false;
    CrossingReport done_;
};

// Writes the report as compact JSON for Particle.publish (kept under 1 KB).
// Times inside are milliseconds relative to t0. Returns the length written,
// or 0 if the buffer was too small.
size_t reportToJson(const CrossingReport &r, int nodeId, int distanceFt, char *buf, size_t bufSize);
