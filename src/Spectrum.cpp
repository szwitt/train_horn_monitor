#include "Spectrum.h"

#include <algorithm>
#include <cmath>

#include "config.h"

namespace {
constexpr float kPi = 3.14159265358979f;
constexpr float kTiny = 1e-12f;
}  // namespace

void fftRadix2(float *re, float *im, size_t n) {
    // Bit-reversal permutation
    for (size_t i = 1, j = 0; i < n; i++) {
        size_t bit = n >> 1;
        for (; j & bit; bit >>= 1) {
            j ^= bit;
        }
        j ^= bit;
        if (i < j) {
            std::swap(re[i], re[j]);
            std::swap(im[i], im[j]);
        }
    }
    for (size_t len = 2; len <= n; len <<= 1) {
        float ang = -2.0f * kPi / (float)len;
        float wRe = std::cos(ang), wIm = std::sin(ang);
        for (size_t i = 0; i < n; i += len) {
            float curRe = 1.0f, curIm = 0.0f;
            for (size_t k = 0; k < len / 2; k++) {
                size_t a = i + k, b = i + k + len / 2;
                float tRe = re[b] * curRe - im[b] * curIm;
                float tIm = re[b] * curIm + im[b] * curRe;
                re[b] = re[a] - tRe;
                im[b] = im[a] - tIm;
                re[a] += tRe;
                im[a] += tIm;
                float nextRe = curRe * wRe - curIm * wIm;
                curIm = curRe * wIm + curIm * wRe;
                curRe = nextRe;
            }
        }
    }
}

SpectrumAnalyzer::SpectrumAnalyzer(size_t frameSamples, float sampleRateHz)
    : n_(frameSamples), sampleRate_(sampleRateHz), window_(frameSamples), re_(frameSamples),
      im_(frameSamples), power_(frameSamples / 2) {
    for (size_t i = 0; i < n_; i++) {
        window_[i] = 0.5f - 0.5f * std::cos(2.0f * kPi * i / (float)(n_ - 1));
    }
}

void SpectrumAnalyzer::bandBins(float loHz, float hiHz, size_t *lo, size_t *hi) const {
    float binHz = sampleRate_ / (float)n_;
    *lo = (size_t)std::ceil(loHz / binHz);
    *hi = std::min((size_t)std::floor(hiHz / binHz), power_.size() - 1);
}

float SpectrumAnalyzer::bandEnergy(float loHz, float hiHz) const {
    size_t lo, hi;
    bandBins(loHz, hiHz, &lo, &hi);
    float sum = 0.0f;
    for (size_t k = lo; k <= hi; k++) {
        sum += power_[k];
    }
    return sum;
}

// Geometric mean / arithmetic mean of the bin powers in a band.
float SpectrumAnalyzer::bandFlatness(float loHz, float hiHz) const {
    size_t lo, hi;
    bandBins(loHz, hiHz, &lo, &hi);
    if (hi < lo) {
        return 1.0f;
    }
    float logSum = 0.0f, sum = 0.0f;
    for (size_t k = lo; k <= hi; k++) {
        logSum += std::log(power_[k] + kTiny);
        sum += power_[k];
    }
    float count = (float)(hi - lo + 1);
    return std::exp(logSum / count) / (sum / count + kTiny);
}

FrameFeatures SpectrumAnalyzer::analyze(const float *samples) {
    float meanSq = 0.0f;
    for (size_t i = 0; i < n_; i++) {
        meanSq += samples[i] * samples[i];
        re_[i] = samples[i] * window_[i];
        im_[i] = 0.0f;
    }
    meanSq /= (float)n_;

    fftRadix2(re_.data(), im_.data(), n_);
    for (size_t k = 0; k < power_.size(); k++) {
        power_[k] = re_[k] * re_[k] + im_[k] * im_[k];
    }

    FrameFeatures f;
    float total = bandEnergy(cfg::kTotalLoHz, cfg::kTotalHiHz) + kTiny;
    float horn = bandEnergy(cfg::kHornLoHz, cfg::kHornHiHz);
    float bell = bandEnergy(cfg::kBellLoHz, cfg::kBellHiHz);
    float rumble = bandEnergy(cfg::kRumbleLoHz, cfg::kRumbleHiHz);

    f.rumbleRatio = rumble / total;
    f.hornRatio = horn / total;
    f.bellRatio = bell / total;
    f.hornFlatness = bandFlatness(cfg::kHornLoHz, cfg::kHornHiHz);
    f.bellEnergyDb = 10.0f * std::log10(bell / (float)(n_ * n_) + kTiny);
    f.frameDbfs = 10.0f * std::log10(meanSq + kTiny);
    return f;
}
