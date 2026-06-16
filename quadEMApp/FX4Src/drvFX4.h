/*
 * drvFX4.h
 *
 * Asyn driver that inherits from the drvQuadEM class to control the Pyramid FX4 4-channel picoammeter
 *
 * Author: Mark Rivers
 *
 * Created May 1, 2026
 */

#ifndef drvFX4_H
#define drvFX4_H

#include "drvQuadEM.h"

#include <array>
#include <atomic>
#include <list>
#include <mutex>
#include <string>

#include <ixwebsocket/IXNetSystem.h>
#include <ixwebsocket/IXWebSocket.h>
#include <json.hpp>

using json = nlohmann::json;

typedef enum {
    gateLevelLow = 0,
    gateLevelHigh = 1,
    gateLevelUnknown = 2
} gateLevel_t;

typedef enum {
    adcEvent,
    gateEvent,
} eventType_t;

typedef struct {
    double val;
    double time;
} ADCSample;

class sortedListElement {
public:
    sortedListElement(eventType_t et, const double vals[4], double ts)
        : eventType(et), timeStamp(ts)
    {
        for (int i = 0; i < 4; i++) values[i] = vals[i];
    }

    friend bool operator<(const sortedListElement& lhs, const sortedListElement& rhs)
    {
        return lhs.timeStamp < rhs.timeStamp;
    }

    eventType_t eventType;
    double values[4];
    double timeStamp;
};

/** Class to control the Pyramid FX4 4-Channel current meter */
class drvFX4 : public drvQuadEM {
public:
    drvFX4(const char *portName, const char *FX4_IP, int ringBufferSize);
    virtual ~drvFX4();

    void report(FILE *fp, int details);
    void pollThread(void);
    virtual void exitHandler();

protected:
    virtual asynStatus readStatus();
    virtual asynStatus reset();
    virtual asynStatus setAcquire(epicsInt32 value);
    virtual asynStatus setAcquireMode(epicsInt32 value);
    virtual asynStatus setAveragingTime(epicsFloat64 value);
    virtual asynStatus setNumAcquire(epicsInt32 value);
    virtual asynStatus setTriggerMode(epicsInt32 value);
    virtual asynStatus setTriggerPolarity(epicsInt32 value);
    virtual asynStatus setValuesPerRead(epicsInt32 value);

private:
    void onOpen();
    void onMessage(const std::string& payload);
    void onClose(int code, const std::string& reason);
    void onError(const std::string& reason);

    void sendEventData(const std::string& event, json data = nullptr);
    void sendSubscribeEvent();
    void sendUnsubscribeEvent();
    void sendGetEvent();
    void onMessageEvent(const std::string& event, const json& data);

    asynStatus setAcquireParams();
    bool waitForConnection(double timeoutSeconds);
    void startWebSocket(const std::string& uri);
    void stopWebSocket();
    bool reconnectWebSocket(const std::string& uri);

    ix::WebSocket ws_;
    std::mutex wsMutex_;
    std::atomic<bool> FX4Connected_;
    std::atomic<bool> wsStopping_;
    std::string wsUri_;

    static constexpr int FX4_NUM_CHANS = 4;
    static inline const std::array<std::string, FX4_NUM_CHANS>
        ADC_PATHS = {
            "/fx4/adc/channel_1/value",
            "/fx4/adc/channel_2/value",
            "/fx4/adc/channel_3/value",
            "/fx4/adc/channel_4/value"
        };
    static inline const std::string GATE_PATH = "/fx4/gpio_0/22/readback/value";

    std::array<std::list<ADCSample>, FX4_NUM_CHANS> adcCache_;
    epicsInt64 startTime_;
    gateLevel_t gateLevel_;
    bool synchronized_;
    bool timestampMismatch_;
    bool triggerActive_;
    int numTriggerValues_;

    int triggerMode_;
    int triggerPolarity_;
    int acquireMode_;
    int numAverage_;
};

#endif
