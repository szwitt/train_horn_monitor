#pragma once

// Tunable constants for the crossing lights node.
// Thresholds are starting estimates; tune them against readings taken from
// the chosen spot at the crossing.

#include <cstddef>
#include <cstdint>

namespace lcfg {

// ---- Sampling ----
constexpr uint32_t kSampleMs = 50;  // 20 readings per second from each sensor
constexpr size_t kMaxLamps = 2;

// ---- Light sensors (BH1750) ----
constexpr uint8_t kSensorAddr[kMaxLamps] = {0x23, 0x5C};  // ADDR pin low, high
constexpr uint8_t kSensorMtreg = 31;  // lowest sensitivity: reads up to ~120k lux before saturating

// ---- Flash detection ----
// Crossing flashers run at 35 to 65 flashes per minute (0.92 to 1.71 s per
// flash). The period limits are a little wider to allow for timing jitter.
constexpr uint32_t kWindowMs = 4000;  // min/max window, covers at least two flashes
constexpr float kMinSwingLux = 5.0f;  // on/off difference needed to count as a flash
constexpr float kMinContrast = 0.25f;  // swing as a share of the brightest reading
constexpr float kOnThreshold = 0.6f;  // hysteresis, as a share of the swing above the minimum
constexpr float kOffThreshold = 0.4f;
constexpr uint32_t kMinPeriodMs = 700;
constexpr uint32_t kMaxPeriodMs = 2200;
constexpr uint32_t kConfirmCycles = 3;  // evenly spaced flashes in a row before a lamp counts as flashing
constexpr uint32_t kLampLostMs = kMaxPeriodMs + 1000;  // no flash this long and the lamp has stopped
constexpr float kMinAlternation = 0.6f;  // two lamps: share of readings where exactly one is lit
constexpr uint32_t kEndMs = 4000;  // no lamp flashing this long ends the activation

// ---- Reporting and power ----
constexpr uint32_t kHeartbeatMs = 6 * 60 * 60 * 1000;  // each one costs a cellular connection
constexpr uint32_t kOnlineHoldMs = 60 * 1000;  // stay connected after publishing, for config and updates
constexpr uint32_t kBootOnlineMs = 5 * 60 * 1000;  // longer window after power-up
constexpr uint32_t kConnectTimeoutMs = 5 * 60 * 1000;  // give up and turn the radio off
constexpr uint32_t kConnectRetryMs = 15 * 60 * 1000;
constexpr uint32_t kPublishSpacingMs = 1100;  // Particle limit is ~1 per second
constexpr size_t kPublishQueueMax = 20;
constexpr size_t kSampleQueueMax = 600;  // 30 s of readings, covers a slow radio shutdown

}  // namespace lcfg
