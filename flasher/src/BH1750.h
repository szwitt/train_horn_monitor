#pragma once

#include "Particle.h"

// Minimal driver for the BH1750 ambient light sensor in continuous
// low-resolution mode (a new reading about every 16 ms).
class BH1750 {
public:
    explicit BH1750(uint8_t addr) : addr_(addr) {}

    // Powers the sensor on and starts continuous readings. Returns false if
    // it doesn't answer.
    bool begin(uint8_t mtreg);

    // Latest reading in lux. Returns false if the sensor didn't answer.
    bool read(float &lux, bool &saturated);

    bool present() const { return present_; }

private:
    bool command(uint8_t cmd);

    uint8_t addr_;
    uint8_t mtreg_ = 69;
    bool present_ = false;
};
