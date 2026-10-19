// Train crossing noise monitor node: Particle Boron + DFRobot Gravity sound
// level meter (A0) + MP34DT01-M PDM microphone (CLK D2, DAT D3).
//
// Detects the crossing bell, horn blasts and the train passing, then
// publishes one "crossing" event per train and a "heartbeat" every 15 min.
// A bench unit in test mode publishes "test-crossing" and "test-heartbeat"
// instead, which the backend's webhooks don't match.

#include "Particle.h"

#include <deque>

#include "Microphone_PDM.h"
#include "NodeProcessor.h"
#include "config.h"

SYSTEM_MODE(AUTOMATIC);
SYSTEM_THREAD(ENABLED);

SerialLogHandler logHandler(LOG_LEVEL_INFO);

namespace {

constexpr pin_t kMeterPin = A0;
constexpr pin_t kMicClkPin = D2;
constexpr pin_t kMicDatPin = D3;
constexpr uint32_t kMeterSampleMs = 25;  // 5 ADC samples per 125 ms reading

// Node settings, kept in EEPROM and set with the "config" cloud function.
struct NodeConfig {
    uint32_t magic;
    int32_t nodeId;
    int32_t distanceFt;
    float calOffsetDb;
    int32_t testMode;  // 1 = bench unit: publish under test- event names
};
struct NodeConfigV1 {
    uint32_t magic;
    int32_t nodeId;
    int32_t distanceFt;
    float calOffsetDb;
};
constexpr uint32_t kConfigMagic = 0x54484d32;  // "THM2"
constexpr uint32_t kConfigMagicV1 = 0x54484d31;  // "THM1", before test mode
NodeConfig nodeConfig = {kConfigMagic, 0, 0, 0.0f, 0};

NodeProcessor processor;
float frame[cfg::kFrameSamples];
volatile bool frameReady = false;
uint64_t frameTimeMs = 0;

// Epoch milliseconds: anchored to the cloud-synced second, extended with millis()
time32_t anchorSec = 0;
uint32_t anchorMillis = 0;

uint32_t lastMeterSample = 0;
float meterSum = 0.0f;
int meterCount = 0;
uint32_t lastHeartbeat = 0;
uint32_t lastPublish = 0;
std::deque<std::pair<String, String>> publishQueue;

double currentDba = 0.0;
double ambientDba = 0.0;

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

void loadConfig() {
    EEPROM.get(0, nodeConfig);
    if (nodeConfig.magic == kConfigMagic) {
        return;
    }
    NodeConfigV1 v1;
    EEPROM.get(0, v1);
    if (v1.magic == kConfigMagicV1) {
        nodeConfig = {kConfigMagic, v1.nodeId, v1.distanceFt, v1.calOffsetDb, 0};
    } else {
        nodeConfig = {kConfigMagic, 0, 0, 0.0f, 0};
    }
}

const char *eventName(const char *name) {
    static char buf[32];
    snprintf(buf, sizeof(buf), "%s%s", nodeConfig.testMode ? "test-" : "", name);
    return buf;
}

// "node=1,dist=50,cal=-1.5,test=0" (any subset)
int setConfig(String arg) {
    int node, dist, test;
    float cal;
    const char *s = arg.c_str();
    if (const char *p = strstr(s, "node=")) {
        if (sscanf(p, "node=%d", &node) == 1) nodeConfig.nodeId = node;
    }
    if (const char *p = strstr(s, "dist=")) {
        if (sscanf(p, "dist=%d", &dist) == 1) nodeConfig.distanceFt = dist;
    }
    if (const char *p = strstr(s, "cal=")) {
        if (sscanf(p, "cal=%f", &cal) == 1) nodeConfig.calOffsetDb = cal;
    }
    if (const char *p = strstr(s, "test=")) {
        if (sscanf(p, "test=%d", &test) == 1) nodeConfig.testMode = test ? 1 : 0;
    }
    EEPROM.put(0, nodeConfig);
    Log.info("config node=%ld dist=%ld cal=%.1f test=%ld", nodeConfig.nodeId, nodeConfig.distanceFt,
             nodeConfig.calOffsetDb, nodeConfig.testMode);
    return 0;
}

void queuePublish(const char *name, const char *data) {
    if (publishQueue.size() >= cfg::kPublishQueueMax) {
        publishQueue.pop_front();
    }
    publishQueue.emplace_back(String(name), String(data));
}

void servicePublishQueue() {
    if (publishQueue.empty() || !Particle.connected() || millis() - lastPublish < cfg::kPublishSpacingMs) {
        return;
    }
    auto &item = publishQueue.front();
    if (Particle.publish(item.first, item.second, PRIVATE)) {
        publishQueue.pop_front();
    }
    lastPublish = millis();
}

void sampleMeter(uint64_t tMs) {
    if (millis() - lastMeterSample < kMeterSampleMs) {
        return;
    }
    lastMeterSample = millis();
    meterSum += analogRead(kMeterPin);
    if (++meterCount < (int)(cfg::kMeterReadingMs / kMeterSampleMs)) {
        return;
    }
    float dba = adcCountsToDba(meterSum / meterCount, nodeConfig.calOffsetDb);
    meterSum = 0.0f;
    meterCount = 0;
    processor.onMeterReading(tMs, dba);
    currentDba = dba;
    ambientDba = processor.ambientDba();
}

// Battery percent. The Boron has a fuel gauge; the Argon bench unit doesn't, so it reports -1.
float batteryPercent() {
#if Wiring_Cellular
    return System.batteryCharge();
#else
    return -1.0f;
#endif
}

void sendHeartbeat() {
    char buf[200];
    snprintf(buf, sizeof(buf),
             "{\"v\":1,\"node\":%ld,\"dist_ft\":%ld,\"amb\":%.1f,\"leq\":%.1f,\"soc\":%.0f,\"busy\":%s}",
             nodeConfig.nodeId, nodeConfig.distanceFt, processor.ambientDba(), processor.takeIntervalLeq(),
             batteryPercent(), processor.busy() ? "true" : "false");
    queuePublish(eventName("heartbeat"), buf);
}

}  // namespace

void setup() {
    loadConfig();
    Particle.function("config", setConfig);
    Particle.variable("dba", currentDba);
    Particle.variable("ambient", ambientDba);

    int err = Microphone_PDM::instance()
                  .withPinCLK(kMicClkPin)
                  .withPinDAT(kMicDatPin)
                  .withOutputSize(Microphone_PDM::OutputSize::SIGNED_16)
                  .withRange(Microphone_PDM::Range::RANGE_2048)
                  .init();
    if (err) {
        Log.error("PDM init err=%d", err);
    }
    err = Microphone_PDM::instance().start();
    if (err) {
        Log.error("PDM start err=%d", err);
    }
}

void loop() {
    uint64_t tMs = nowMs();

    Microphone_PDM::instance().loop();
    Microphone_PDM::instance().noCopySamples([](void *pSamples, size_t numSamples) {
        if (frameReady || numSamples != cfg::kFrameSamples) {
            return;  // previous frame still being processed
        }
        const int16_t *src = (const int16_t *)pSamples;
        for (size_t i = 0; i < numSamples; i++) {
            frame[i] = (float)src[i] / cfg::kMicFullScale;
        }
        frameReady = true;
    });

    // Wait for cloud time before detecting, so events carry real timestamps
    if (tMs == 0) {
        frameReady = false;
        return;
    }

    if (frameReady) {
        processor.onAudioFrame(tMs, frame);
        frameReady = false;
    }
    sampleMeter(tMs);

    if (processor.hasReport()) {
        CrossingReport r = processor.takeReport();
        char buf[1024];
        if (reportToJson(r, nodeConfig.nodeId, nodeConfig.distanceFt, buf, sizeof(buf))) {
            queuePublish(eventName("crossing"), buf);
            Log.info("crossing %s", buf);
        } else {
            Log.error("crossing report too large");
        }
    }

    if (millis() - lastHeartbeat >= cfg::kHeartbeatMs) {
        lastHeartbeat = millis();
        sendHeartbeat();
    }
    servicePublishQueue();
}
