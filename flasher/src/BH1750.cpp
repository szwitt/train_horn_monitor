#include "BH1750.h"

namespace {
constexpr uint8_t kPowerOn = 0x01;
constexpr uint8_t kContinuousLowRes = 0x13;
constexpr uint8_t kMtregHigh = 0x40;  // | top 3 bits of MTreg
constexpr uint8_t kMtregLow = 0x60;  // | low 5 bits of MTreg
constexpr uint8_t kDefaultMtreg = 69;
}  // namespace

bool BH1750::command(uint8_t cmd) {
    Wire.beginTransmission(addr_);
    Wire.write(cmd);
    return Wire.endTransmission() == 0;
}

bool BH1750::begin(uint8_t mtreg) {
    mtreg_ = mtreg;
    present_ = command(kPowerOn) && command(kMtregHigh | (mtreg >> 5)) && command(kMtregLow | (mtreg & 0x1F)) &&
               command(kContinuousLowRes);
    return present_;
}

bool BH1750::read(float &lux, bool &saturated) {
    if (!present_ || Wire.requestFrom(addr_, (uint8_t)2) != 2) {
        return false;
    }
    uint16_t raw = (uint16_t)(Wire.read() << 8);
    raw |= (uint16_t)Wire.read();
    saturated = raw == 0xFFFF;
    lux = raw / 1.2f * ((float)kDefaultMtreg / mtreg_);
    return true;
}
