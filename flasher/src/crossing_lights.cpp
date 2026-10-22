// Crossing lights node: Particle Boron + two BH1750 light sensors on I2C
// (SDA D0, SCL D1), each looking through a hood at one lamp of a crossing
// flasher head.
//
// Detects the lights flashing and publishes one "crossing" event with a
// "flash" section per activation, plus a "heartbeat" every 6 hours. To save
// power the radio stays off except to publish. A bench unit in test mode
// publishes "test-crossing" and "test-heartbeat" instead, which the
// backend's webhooks don't match.

#include "Particle.h"

#include <deque>

#include "BH1750.h"
#include "FlashDetector.h"
#include "lights_config.h"

SYSTEM_MODE(SEMI_AUTOMATIC);
SYSTEM_THREAD(ENABLED);

SerialLogHandler logHandler(LOG_LEVEL_INFO);

namespace {

constexpr pin_t kDemoLedPin[lcfg::kMaxLamps] = {D6, D7};
constexpr uint32_t kDemoPeriodMs = 1200;  // 50 flashes per minute

// Node settings, kept in EEPROM and set with the "config" cloud function.
struct NodeConfig {
    uint32_t magic;
    int32_t nodeId;
    int32_t distanceFt;
    int32_t lamps;  // sensors in use, 1 or 2
    int32_t testMode;  // 1 = bench unit: publish under test- event names
    int32_t stayOnline;  // 1 = keep the radio on (bench use; costs battery)
};
constexpr uint32_t kConfigMagic = 0x54484c31;  // "THL1"
NodeConfig nodeConfig = {kConfigMagic, 5, 0, 2, 0, 0};

BH1750 sensor[lcfg::kMaxLamps] = {BH1750(lcfg::kSensorAddr[0]), BH1750(lcfg::kSensorAddr[1])};
FlashDetector *detector = nullptr;

// Readings taken on the timer thread, handed to loop()
struct Sample {
    uint64_t tMs;
    float lux[lcfg::kMaxLamps];
    bool saturated[lcfg::kMaxLamps];
};
Sample samples[lcfg::kSampleQueueMax];  // ring buffer, no allocation on the timer thread
size_t sampleHead = 0;  // next to read
size_t sampleCount = 0;
volatile uint32_t droppedSamples = 0;

// Epoch milliseconds: anchored to the second, extended with millis()
time32_t anchorSec = 0;
uint32_t anchorMillis = 0;

volatile uint32_t demoUntil = 0;

enum class Net { Off, Connecting, Online, Disconnecting };
Net net = Net::Off;
uint32_t netSince = 0;
uint32_t lastActivity = 0;
uint32_t holdMs = lcfg::kBootOnlineMs;
uint32_t retryAt = 0;

uint32_t lastHeartbeat = 0;
uint32_t lastPublish = 0;
uint32_t activations = 0;
std::deque<std::pair<String, String>> publishQueue;

double luxA = 0.0;
double luxB = 0.0;

uint64_t nowMs() {
    if (!Time.isValid()) {
        return 0;
    }
    time32_t sec = Time.now();
    if (sec != anchorSec) {
        anchorSec = sec;
        anchorMillis = millis();
    }
    uint32_t extra = millis() - anchorMillis;
    return (uint64_t)anchorSec * 1000 + (extra < 1000 ? extra : 999);
}

// Runs every kSampleMs on the timer thread, so readings keep their timing
// even while loop() waits on the radio.
void takeSample() {
    if (demoUntil && (int32_t)(millis() - demoUntil) < 0) {
        bool first = millis() % kDemoPeriodMs < kDemoPeriodMs / 2;
        digitalWrite(kDemoLedPin[0], first ? HIGH : LOW);
        digitalWrite(kDemoLedPin[1], first ? LOW : HIGH);
    } else if (demoUntil) {
        demoUntil = 0;
        digitalWrite(kDemoLedPin[0], LOW);
        digitalWrite(kDemoLedPin[1], LOW);
    }

    Sample s = {nowMs(), {0.0f, 0.0f}, {false, false}};
    if (s.tMs == 0) {
        return;  // wait for cloud time so events carry real timestamps
    }
    for (int i = 0; i < nodeConfig.lamps; i++) {
        if (!sensor[i].read(s.lux[i], s.saturated[i])) {
            s.lux[i] = 0.0f;  // a missing sensor reads flat, so it never confirms a flash
        }
    }
    ATOMIC_BLOCK() {
        if (sampleCount == lcfg::kSampleQueueMax) {
            sampleHead = (sampleHead + 1) % lcfg::kSampleQueueMax;  // drop the oldest
            sampleCount--;
            droppedSamples = droppedSamples + 1;
        }
        samples[(sampleHead + sampleCount) % lcfg::kSampleQueueMax] = s;
        sampleCount++;
    }
}
Timer sampleTimer(lcfg::kSampleMs, takeSample);

void loadConfig() {
    EEPROM.get(0, nodeConfig);
    if (nodeConfig.magic != kConfigMagic) {
        nodeConfig = {kConfigMagic, 5, 0, 2, 0, 0};
    }
    nodeConfig.lamps = constrain(nodeConfig.lamps, 1, (int)lcfg::kMaxLamps);
}

const char *eventName(const char *name) {
    static char buf[32];
    snprintf(buf, sizeof(buf), "%s%s", nodeConfig.testMode ? "test-" : "", name);
    return buf;
}

// "node=5,dist=120,lamps=2,test=0,online=0" (any subset). "demo=60" blinks
// LEDs on D6 and D7 like a flasher for 60 seconds, for bench testing.
int setConfig(String arg) {
    int v;
    const char *s = arg.c_str();
    if (const char *p = strstr(s, "node=")) {
        if (sscanf(p, "node=%d", &v) == 1) nodeConfig.nodeId = v;
    }
    if (const char *p = strstr(s, "dist=")) {
        if (sscanf(p, "dist=%d", &v) == 1) nodeConfig.distanceFt = v;
    }
    if (const char *p = strstr(s, "lamps=")) {
        if (sscanf(p, "lamps=%d", &v) == 1 && v >= 1 && v <= (int)lcfg::kMaxLamps && v != nodeConfig.lamps) {
            nodeConfig.lamps = v;
            delete detector;
            detector = new FlashDetector((uint8_t)v);
        }
    }
    if (const char *p = strstr(s, "test=")) {
        if (sscanf(p, "test=%d", &v) == 1) nodeConfig.testMode = v ? 1 : 0;
    }
    if (const char *p = strstr(s, "online=")) {
        if (sscanf(p, "online=%d", &v) == 1) nodeConfig.stayOnline = v ? 1 : 0;
    }
    EEPROM.put(0, nodeConfig);
    if (const char *p = strstr(s, "demo=")) {
        if (sscanf(p, "demo=%d", &v) == 1 && v > 0) demoUntil = millis() + (uint32_t)v * 1000;
    }
    Log.info("config node=%ld dist=%ld lamps=%ld test=%ld online=%ld", nodeConfig.nodeId, nodeConfig.distanceFt,
             nodeConfig.lamps, nodeConfig.testMode, nodeConfig.stayOnline);
    return 0;
}

void queuePublish(const char *name, const char *data) {
    if (publishQueue.size() >= lcfg::kPublishQueueMax) {
        publishQueue.pop_front();
    }
    publishQueue.emplace_back(String(name), String(data));
}

void servicePublishQueue() {
    if (publishQueue.empty() || millis() - lastPublish < lcfg::kPublishSpacingMs) {
        return;
    }
    auto &item = publishQueue.front();
    if (Particle.publish(item.first, item.second, PRIVATE)) {
        publishQueue.pop_front();
    }
    lastPublish = millis();
    lastActivity = millis();
}

// Battery percent. The Boron has a fuel gauge; the Argon bench unit doesn't, so it reports -1.
float batteryPercent() {
#if Wiring_Cellular
    return System.batteryCharge();
#else
    return -1.0f;
#endif
}

void radioOff() {
#if Wiring_Cellular
    Cellular.off();
#else
    WiFi.off();
#endif
}

void sendHeartbeat() {
    char buf[200];
    snprintf(buf, sizeof(buf),
             "{\"v\":1,\"kind\":\"lights\",\"node\":%ld,\"dist_ft\":%ld,\"soc\":%.0f,\"la\":%.0f,\"lb\":%.0f,"
             "\"lamps\":%ld,\"sensors\":%d,\"n\":%lu,\"drop\":%lu}",
             nodeConfig.nodeId, nodeConfig.distanceFt, batteryPercent(), luxA, luxB, nodeConfig.lamps,
             (int)sensor[0].present() + (int)sensor[1].present(), (unsigned long)activations,
             (unsigned long)droppedSamples);
    activations = 0;
    queuePublish(eventName("heartbeat"), buf);
}

// Turns the radio on when there is something to send and off again once
// it has been sent and the hold time has passed.
void serviceNetwork() {
    bool wanted = !publishQueue.empty() || nodeConfig.stayOnline;
    switch (net) {
        case Net::Off:
            if (wanted && (int32_t)(millis() - retryAt) >= 0) {
                Particle.connect();
                net = Net::Connecting;
                netSince = millis();
            }
            break;
        case Net::Connecting:
            if (Particle.connected()) {
                net = Net::Online;
                lastActivity = millis();
            } else if (millis() - netSince >= lcfg::kConnectTimeoutMs) {
                Log.warn("no connection, retrying later");
                Particle.disconnect();
                radioOff();
                net = Net::Off;
                retryAt = millis() + lcfg::kConnectRetryMs;
            }
            break;
        case Net::Online:
            if (!Particle.connected()) {
                net = Net::Connecting;  // the system reconnects on its own
                netSince = millis();
                break;
            }
            servicePublishQueue();
            if (publishQueue.empty() && !nodeConfig.stayOnline && millis() - lastActivity >= holdMs) {
                Particle.disconnect();
                net = Net::Disconnecting;
                holdMs = lcfg::kOnlineHoldMs;
            }
            break;
        case Net::Disconnecting:
            if (Particle.disconnected()) {
                radioOff();
                net = Net::Off;
            }
            break;
    }
}

}  // namespace

void setup() {
    loadConfig();
    Particle.function("config", setConfig);
    Particle.variable("lux_a", luxA);
    Particle.variable("lux_b", luxB);

    // The status LED draws a few mA; keep it dark
    RGB.control(true);
    RGB.color(0, 0, 0);

    pinMode(kDemoLedPin[0], OUTPUT);
    pinMode(kDemoLedPin[1], OUTPUT);

    Wire.begin();
    for (int i = 0; i < (int)lcfg::kMaxLamps; i++) {
        if (!sensor[i].begin(lcfg::kSensorMtreg) && i < nodeConfig.lamps) {
            Log.error("light sensor %d (0x%02x) not found", i, lcfg::kSensorAddr[i]);
        }
    }
    detector = new FlashDetector((uint8_t)nodeConfig.lamps);

    // Connect once at power-up for the clock, config and firmware updates
    Particle.connect();
    net = Net::Connecting;
    netSince = millis();
    sampleTimer.start();
}

void loop() {
    for (;;) {
        Sample s;
        bool have = false;
        ATOMIC_BLOCK() {
            if (sampleCount) {
                s = samples[sampleHead];
                sampleHead = (sampleHead + 1) % lcfg::kSampleQueueMax;
                sampleCount--;
                have = true;
            }
        }
        if (!have) {
            break;
        }
        detector->onSample(s.tMs, s.lux, s.saturated);
        luxA = s.lux[0];
        luxB = s.lux[1];
    }

    if (detector->hasEvent()) {
        LightsEvent e = detector->takeEvent();
        activations++;
        char buf[256];
        if (lightsEventToJson(e, nodeConfig.nodeId, nodeConfig.distanceFt, buf, sizeof(buf))) {
            queuePublish(eventName("crossing"), buf);
            Log.info("lights %s", buf);
        }
    }

    if (millis() - lastHeartbeat >= lcfg::kHeartbeatMs) {
        lastHeartbeat = millis();
        sendHeartbeat();
    }
    serviceNetwork();
}
