#pragma once

#include <cstddef>
#include <cstdint>

#include "lights_config.h"

// One activation of the crossing lights: from the first flash to the last.
struct LightsEvent {
    uint64_t startMs = 0;  // first flash of the run that confirmed it
    uint64_t endMs = 0;  // last moment any lamp was lit
    uint32_t flashes = 0;  // flashes counted on the lamp seen most clearly
    float flashesPerMin = 0.0f;
    bool alternating = false;  // two lamps confirmed lighting in turn
    uint8_t lamps = 1;
};

// Tracks one lamp: finds its flashes in the light level and says whether
// they come at a steady crossing-light rhythm.
class LampTracker {
public:
    void onSample(uint64_t tMs, float lux, bool saturated);
    bool lit() const { return lit_; }
    bool usable() const { return usable_; }  // enough contrast to tell on from off
    bool flashing(uint64_t tMs) const;
    uint64_t runStartMs() const { return runStartMs_; }
    uint32_t runFlashes() const { return runFlashes_; }  // flashes since runStartMs
    uint64_t lastRiseMs() const { return lastRiseMs_; }
    uint64_t lastLitMs() const { return lastLitMs_; }
    uint32_t rises() const { return rises_; }  // every flash seen, for counting

private:
    static constexpr size_t kWindow = lcfg::kWindowMs / lcfg::kSampleMs;
    float window_[kWindow] = {};
    size_t count_ = 0;
    size_t next_ = 0;

    bool lit_ = false;
    bool usable_ = false;
    uint64_t lastRiseMs_ = 0;
    uint64_t runStartMs_ = 0;  // first rise of the current run of steady flashes
    uint32_t steady_ = 0;  // steady periods in a row
    uint32_t runFlashes_ = 0;
    uint64_t lastLitMs_ = 0;
    uint32_t rises_ = 0;
};

// Combines one or two lamps into lights-on and lights-off events. With two
// lamps (the left and right lamps of one head) both must flash in turn to
// start an event, which rules out sun glare and flicker that light both
// sensors at once. Once started, either lamp keeps it going.
class FlashDetector {
public:
    explicit FlashDetector(uint8_t lamps = 2);

    // Call every lcfg::kSampleMs with each lamp's reading.
    void onSample(uint64_t tMs, const float *lux, const bool *saturated);

    bool active() const { return active_; }
    bool hasEvent() const { return ready_; }
    LightsEvent takeEvent();
    uint8_t lamps() const { return lamps_; }

private:
    float alternation() const;
    void finish();

    uint8_t lamps_;
    LampTracker lamp_[lcfg::kMaxLamps];

    static constexpr size_t kAltWindow = lcfg::kWindowMs / lcfg::kSampleMs;
    uint8_t alt_[kAltWindow] = {};  // 0 = not comparable, 1 = one lamp lit, 2 = both or neither
    size_t altNext_ = 0;

    bool active_ = false;
    bool alternating_ = false;
    uint64_t lastFlashingMs_ = 0;
    // Per-lamp flash counts during the event
    uint32_t count_[lcfg::kMaxLamps] = {};
    uint32_t mark_[lcfg::kMaxLamps] = {};
    uint64_t firstRiseMs_[lcfg::kMaxLamps] = {};
    LightsEvent cur_;
    bool ready_ = false;
    LightsEvent done_;
};

// Writes the event as compact JSON for Particle.publish. Times inside are
// milliseconds relative to the start. Returns the length written, or 0 if
// the buffer was too small.
size_t lightsEventToJson(const LightsEvent &e, int nodeId, int distanceFt, char *buf, size_t bufSize);
