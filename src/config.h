#pragma once

// Tunable constants for the train crossing node.
// Detection thresholds are starting estimates; tune them against field
// recordings from the actual crossing.

#include <cstddef>
#include <cstdint>

namespace cfg {

// ---- Microphone (MP34DT01-M PDM breakout) ----
// nRF52840 PDM clock 1.032 MHz with ratio 64 gives ~16.125 kHz.
constexpr float kMicSampleRateHz = 16125.0f;
constexpr size_t kFrameSamples = 512;  // ~32 ms per frame, one PDM buffer
constexpr float kMicFullScale = 2048.0f;  // RANGE_2048 (12-bit)

// Frequency bands (Hz) used to classify each frame.
constexpr float kRumbleLoHz = 60.0f, kRumbleHiHz = 200.0f;
constexpr float kHornLoHz = 250.0f, kHornHiHz = 800.0f;
constexpr float kBellLoHz = 1000.0f, kBellHiHz = 4000.0f;
constexpr float kTotalLoHz = 60.0f, kTotalHiHz = 8000.0f;

// ---- Sound level meter (DFRobot Gravity SEN0232) ----
constexpr float kAdcRefVolts = 3.3f;
constexpr float kAdcMaxCounts = 4095.0f;
constexpr float kDbPerVolt = 50.0f;  // DFRobot sample code: dBA = V * 50
constexpr uint32_t kMeterReadingMs = 125;  // matches sensor's 125 ms response

// ---- Ambient baseline ----
constexpr size_t kAmbientWindowSec = 300;  // 5 minutes of 1 s Leq values
constexpr float kAmbientPercentile = 0.10f;  // L90 = 10th percentile of levels
constexpr float kDefaultAmbientDba = 45.0f;  // used until the window has data

// ---- Horn ----
constexpr float kHornMinBandRatio = 0.45f;  // horn band share of total energy
constexpr float kHornMaxFlatness = 0.25f;  // horn band must be tonal, not noise-like
constexpr float kHornMinRiseDb = 10.0f;  // meter level above ambient
constexpr uint32_t kHornGapFillMs = 160;  // merge blasts split by short gaps
constexpr uint32_t kHornMinBlastMs = 300;
constexpr uint32_t kHornLongBlastMs = 1500;  // >= this is a "long" blast
constexpr uint32_t kHornSequenceEndMs = 8000;  // silence that ends a sequence
constexpr size_t kMaxBlasts = 12;

// ---- Bell ----
constexpr float kBellMinBandRatio = 0.35f;
constexpr float kBellMinRiseDb = 6.0f;
constexpr float kBellOnsetRiseDb = 6.0f;  // band energy jump that marks a strike
constexpr uint32_t kBellRefractoryMs = 120;
constexpr uint32_t kBellMinIntervalMs = 150;  // strike interval range
constexpr uint32_t kBellMaxIntervalMs = 1000;
constexpr size_t kBellMinStrikes = 4;  // strikes in a row to call it a bell
constexpr float kBellMaxIntervalJitter = 0.35f;  // coefficient of variation
constexpr uint32_t kBellEndMs = 3000;  // no strikes for this long ends the bell

// ---- Train passage ----
constexpr float kTrainMinRiseDb = 10.0f;
constexpr float kTrainEndRiseDb = 5.0f;
constexpr uint32_t kTrainConfirmSec = 5;  // elevated seconds to confirm a train
constexpr uint32_t kTrainEndSec = 5;  // quiet seconds to end it
constexpr float kTrainMinOtherFraction = 0.5f;  // seconds dominated by horn or bell don't count
constexpr float kBellDominantRatio = 0.5f;  // frame counts as bell-dominated above this

// ---- Crossing episode ----
constexpr uint32_t kEpisodeIdleCloseMs = 30000;  // all quiet this long closes it
constexpr uint32_t kEpisodeNoTrainCloseMs = 120000;  // bell/horn without a train

// ---- Reporting ----
constexpr uint32_t kHeartbeatMs = 15 * 60 * 1000;
constexpr uint32_t kPublishSpacingMs = 1100;  // Particle limit is ~1 per second
constexpr size_t kPublishQueueMax = 20;

}  // namespace cfg
