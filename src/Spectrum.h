#pragma once

#include <cstddef>
#include <vector>

// Per-frame audio features used to tell the bell, horn and train apart.
struct FrameFeatures {
    float rumbleRatio = 0.0f;  // share of energy in the rumble band
    float hornRatio = 0.0f;  // share of energy in the horn band
    float bellRatio = 0.0f;  // share of energy in the bell band
    float hornFlatness = 1.0f;  // spectral flatness in the horn band: ~0.56 noise, near 0 for tones
    float bellEnergyDb = -120.0f;  // absolute bell band energy, dB re full scale
    float frameDbfs = -120.0f;  // RMS level of the frame, dB re full scale
};

// Hann-windowed radix-2 FFT over one frame of samples, reduced to the
// band features above. Frame size must be a power of two.
class SpectrumAnalyzer {
public:
    SpectrumAnalyzer(size_t frameSamples, float sampleRateHz);
    FrameFeatures analyze(const float *samples);

private:
    void bandBins(float loHz, float hiHz, size_t *lo, size_t *hi) const;
    float bandEnergy(float loHz, float hiHz) const;
    float bandFlatness(float loHz, float hiHz) const;

    size_t n_;
    float sampleRate_;
    std::vector<float> window_;
    std::vector<float> re_;
    std::vector<float> im_;
    std::vector<float> power_;
};

// In-place iterative radix-2 complex FFT. n must be a power of two.
void fftRadix2(float *re, float *im, size_t n);
