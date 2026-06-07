/*
 * drvFX4.cpp
 *
 * Asyn driver that inherits from the drvQuadEM class to control
 * the Pyramid FX4 4-channel picoammeter
 *
 * Author: Mark Rivers
 *
 * Created May 3, 2026
 *
 * Rewritten to use ixwebsocket instead of websocketpp.
 */

#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <errno.h>
#include <math.h>

#include <algorithm>
#include <iostream>
#include <set>
#include <string>

#include <epicsTypes.h>
#include <epicsTime.h>
#include <epicsThread.h>
#include <epicsString.h>
#include <epicsTimer.h>
#include <epicsMutex.h>
#include <epicsEvent.h>
#include <epicsMath.h>
#include <iocsh.h>

#include <epicsExport.h>
#include "drvFX4.h"

static const char *driverName = "drvFX4";
static std::atomic<bool> g_ixNetSystemInitialized(false);

static void pollThreadC(void *drvPvt)
{
    drvFX4 *pPvt = reinterpret_cast<drvFX4 *>(drvPvt);
    pPvt->pollThread();
}

void drvFX4::onOpen()
{
    FX4Connected_ = true;
}

void drvFX4::onMessage(const std::string& payload)
{
    try {
        json response = json::parse(payload);
        if (!response.contains("event")) return;
        json data = response.contains("data") ? response["data"] : json();
        onMessageEvent(response["event"], data);
    } catch (const std::exception& e) {
        std::cerr << driverName << ": JSON parse error: " << e.what() << std::endl;
    }
}

void drvFX4::onClose(int code, const std::string& reason)
{
    FX4Connected_ = false;
    if (!wsStopping_) {
        std::cerr << driverName
                  << ": WebSocket closed code=" << code
                  << " reason=" << reason << std::endl;
    }
}

void drvFX4::onError(const std::string& reason)
{
    FX4Connected_ = false;
    if (!wsStopping_) {
        std::cerr << driverName
                  << ": WebSocket error: " << reason << std::endl;
    }
}

bool drvFX4::waitForConnection(double timeoutSeconds)
{
    double waited = 0.0;
    const double sleepTime = 0.01;

    while (!FX4Connected_ && (waited < timeoutSeconds)) {
        epicsThreadSleep(sleepTime);
        waited += sleepTime;
    }
    return FX4Connected_;
}

void drvFX4::startWebSocket(const std::string& uri)
{
    ws_.setUrl(uri);
    ws_.disableAutomaticReconnection();
    wsStopping_ = false;

    ws_.setOnMessageCallback([this](const ix::WebSocketMessagePtr& msg) {
        switch (msg->type) {
            case ix::WebSocketMessageType::Open:
                this->onOpen();
                break;

            case ix::WebSocketMessageType::Message:
                this->onMessage(msg->str);
                break;

            case ix::WebSocketMessageType::Close:
                this->onClose(msg->closeInfo.code, msg->closeInfo.reason);
                break;

            case ix::WebSocketMessageType::Error:
                this->onError(msg->errorInfo.reason);
                break;

            default:
                break;
        }
    });

    ws_.start();
}

void drvFX4::stopWebSocket()
{
    std::lock_guard<std::mutex> guard(wsMutex_);
    wsStopping_ = true;
    try {
        ws_.stop();
    } catch (...) {
    }
    FX4Connected_ = false;
}

bool drvFX4::reconnectWebSocket(const std::string& uri)
{
    stopWebSocket();
    epicsThreadSleep(0.1);

    try {
        startWebSocket(uri);
    } catch (const std::exception& e) {
        std::cerr << driverName << ": reconnect setup error: " << e.what() << std::endl;
        return false;
    }

    if (!waitForConnection(5.0)) {
        std::cerr << driverName << ": reconnect timeout to " << uri << std::endl;
        return false;
    }

    if (acquiring_) {
        sendSubscribeEvent();
        sendGetEvent();
    }

    return true;
}

/** Constructor for the drvFX4 class. */
drvFX4::drvFX4(const char *portName, const char *FX4_IP, int ringBufferSize)
    : drvQuadEM(portName, ringBufferSize),
      FX4Connected_(false),
      wsStopping_(false),
      wsUri_("ws://" + std::string(FX4_IP)),
      startTime_(0),
      gateLevel_(gateLevelUnknown),
      synchronized_(false),
      timestampMismatch_(false),
      triggerActive_(false),
      numTriggerValues_(0),
      triggerMode_(0),
      triggerPolarity_(0),
      acquireMode_(0),
      numAverage_(1)
{
    static const char *functionName = "drvFX4";

    if (!g_ixNetSystemInitialized.exchange(true)) {
        ix::initNetSystem();
    }

    try {
        startWebSocket(wsUri_);
    } catch (const std::exception& e) {
        std::cerr << driverName << ": connection setup error: " << e.what() << std::endl;
        return;
    }

    if (!waitForConnection(5.0)) {
        std::cerr << driverName << "::" << functionName
                  << ": timeout connecting to FX4 at " << wsUri_ << std::endl;
    }

    acquiring_ = 0;
    resolution_ = 24;
    setIntegerParam(P_Model, QE_ModelFX4);

    if (epicsThreadCreate("drvFX4Task",
                          epicsThreadPriorityMedium,
                          epicsThreadGetStackSize(epicsThreadStackMedium),
                          (EPICSTHREADFUNC)::pollThreadC,
                          this) == NULL) {
        printf("%s::%s: epicsThreadCreate failure\n", driverName, functionName);
        return;
    }

    callParamCallbacks();
}

drvFX4::~drvFX4()
{
    stopWebSocket();
}

void drvFX4::sendEventData(const std::string& event, json data)
{
    json msg;
    msg["event"] = event;
    msg["data"] = data;

    std::lock_guard<std::mutex> guard(wsMutex_);

    if (wsStopping_ || !FX4Connected_) return;

    ix::WebSocketSendInfo result = ws_.send(msg.dump());
    if (!result.success) {
//        std::cerr << driverName << ": send failed: " << result.errorStr << std::endl;
        std::cerr << driverName << ": send failed: " << std::endl;
    }
}

void drvFX4::sendSubscribeEvent()
{
    json data = {
        {ADC_PATHS[0], true},
        {ADC_PATHS[1], true},
        {ADC_PATHS[2], true},
        {ADC_PATHS[3], true},
        {GATE_PATH, true}
    };
    sendEventData("subscribe", data);
}

void drvFX4::sendUnsubscribeEvent()
{
    sendEventData("subscribe", json::object());
}

void drvFX4::sendGetEvent()
{
    sendEventData("get", nullptr);
}

void drvFX4::onMessageEvent(const std::string& event, const json& data)
{
    static const char *functionName = "drvFX4::onMessageEvent";
    std::multiset<sortedListElement> eventList;
    double values[4] = {0, 0, 0, 0};
    double times[4]  = {0, 0, 0, 0};
    size_t minSize, maxSize;

    if (!acquiring_) return;
    if (event != "update") goto done;
    if (!data.is_object()) goto done;

    for (auto& [path, vals] : data.items()) {
        int chan = -1;
        bool isGate = (path == GATE_PATH);

        if (!isGate) {
            for (int i = 0; i < FX4_NUM_CHANS; i++) {
                if (path == ADC_PATHS[i]) {
                    chan = i;
                    break;
                }
            }
            if (chan < 0) continue;
        }

        if (!vals.is_array()) continue;

        for (auto& v : vals) {
            if (!v.is_array() || v.size() < 2) continue;

            epicsInt64 time = 0;
            try {
                time = v[1].get<epicsInt64>();
            } catch (...) {
                continue;
            }

            if (startTime_ == 0) startTime_ = time;
            double timestamp = (time - startTime_) / 1e9;

            if (isGate) {
                values[0] = v[0].get<bool>() ? 1.0 : 0.0;
                eventList.insert(sortedListElement(gateEvent, values, timestamp));

                if (!adcCache_[0].empty()) {
                    asynPrint(pasynUserSelf, ASYN_TRACEIO_DRIVER,
                              "Gate event, value=%f, time=%f, ADC1 last time=%f\n",
                              values[0], timestamp, adcCache_[0].back().time);
                }
            } else {
                double adcValue = 0.0;
                try {
                    adcValue = v[0].get<double>();
                } catch (...) {
                    continue;
                }
                adcCache_[chan].push_back({adcValue, timestamp});
            }
        }
    }

    if (adcCache_[0].empty()) goto done;

    asynPrint(pasynUserSelf, ASYN_TRACEIO_DRIVER, "%s: Samples=%lu %lu %lu %lu\n"
                                                  "    ADCs oldest=%f %f %f %f\n"
                                                  "   Times oldest=%f %f %f %f\n"
                                                  "    ADCs newest=%f %f %f %f\n"
                                                  "   Times newest=%f %f %f %f\n", functionName,
              (unsigned long)adcCache_[0].size(), (unsigned long)adcCache_[1].size(),
              (unsigned long)adcCache_[2].size(), (unsigned long)adcCache_[3].size(),
              adcCache_[0].front().val,  adcCache_[1].front().val,  adcCache_[2].front().val,  adcCache_[3].front().val,
              adcCache_[0].front().time, adcCache_[1].front().time, adcCache_[2].front().time, adcCache_[3].front().time,
              adcCache_[0].back().val,   adcCache_[1].back().val,   adcCache_[2].back().val,   adcCache_[3].back().val,
              adcCache_[0].back().time,  adcCache_[1].back().time,  adcCache_[2].back().time,  adcCache_[3].back().time);

    minSize = std::min({adcCache_[0].size(), adcCache_[1].size(), adcCache_[2].size(), adcCache_[3].size()});
    maxSize = std::max({adcCache_[0].size(), adcCache_[1].size(), adcCache_[2].size(), adcCache_[3].size()});

    asynPrint(pasynUserSelf, ASYN_TRACEIO_DRIVER, "%s minimum size=%lu, size=%lu %lu %lu %lu\n",
              functionName, (unsigned long)minSize,
              (unsigned long)adcCache_[0].size(), (unsigned long)adcCache_[1].size(),
              (unsigned long)adcCache_[2].size(), (unsigned long)adcCache_[3].size());

    if (minSize != maxSize) {
        if (!synchronized_) {
            asynPrint(pasynUserSelf, ASYN_TRACE_ERROR,
                      "%s not synchronized and different number of samples per channel=%lu %lu %lu %lu\n",
                      functionName,
                      (unsigned long)adcCache_[0].size(), (unsigned long)adcCache_[1].size(),
                      (unsigned long)adcCache_[2].size(), (unsigned long)adcCache_[3].size());
            for (auto& adc : adcCache_) adc.clear();
            goto done;
        }
    } else {
        synchronized_ = true;
    }

    for (size_t i = 0; i < minSize; i++) {
        for (size_t j = 0; j < 4; j++) {
            times[j] = adcCache_[j].front().time;
            values[j] = adcCache_[j].front().val;
        }

        if ((times[1] != times[0]) || (times[2] != times[0]) || (times[3] != times[0])) {
            if (!timestampMismatch_) {
                asynPrint(pasynUserSelf, ASYN_TRACE_ERROR,
                          "%s timestamps are not the same for sample %lu %f %f %f %f\n",
                          functionName, (unsigned long)i, times[0], times[1], times[2], times[3]);
                timestampMismatch_ = true;
            }
        } else {
            if (timestampMismatch_) {
                asynPrint(pasynUserSelf, ASYN_TRACE_ERROR, "%s timestamps back to normal\n", functionName);
                timestampMismatch_ = false;
            }
        }

        eventList.insert(sortedListElement(adcEvent, values, times[0]));
        for (size_t j = 0; j < 4; j++) adcCache_[j].pop_front();
    }

    for (const sortedListElement& element : eventList) {
        if (element.eventType == gateEvent) {
            gateLevel_ = (gateLevel_t)element.values[0];
            if (triggerMode_ == QETriggerModeExtTrigger) {
                if (((triggerPolarity_ == QETriggerPolarityPositive) && (gateLevel_ == gateLevelHigh)) ||
                    ((triggerPolarity_ == QETriggerPolarityNegative) && (gateLevel_ == gateLevelLow))) {
                    asynPrint(pasynUserSelf, ASYN_TRACEIO_DRIVER,
                              "trigger event: gateLevel=%d, triggerActive=%d, numTriggerValues=%d\n",
                              gateLevel_, triggerActive_, numTriggerValues_);
                    triggerActive_ = true;
                    numTriggerValues_ = 0;
                }
            } else if (triggerMode_ == QETriggerModeExtBulb) {
                if (((triggerPolarity_ == QETriggerPolarityPositive) && (gateLevel_ == gateLevelLow)) ||
                    ((triggerPolarity_ == QETriggerPolarityNegative) && (gateLevel_ == gateLevelHigh))) {
                    asynPrint(pasynUserSelf, ASYN_TRACEIO_DRIVER,
                              "bulb event: gateLevel=%d\n", gateLevel_);
                    triggerCallbacks();
                }
            }
            continue;
        }

        if (triggerMode_ == QETriggerModeExtTrigger) {
            if (!triggerActive_) continue;
            numTriggerValues_++;
            if (numTriggerValues_ > numAverage_) {
                triggerActive_ = false;
                continue;
            }
        }

        if ((triggerMode_ == QETriggerModeExtGate) || (triggerMode_ == QETriggerModeExtBulb)) {
            if ((triggerPolarity_ == QETriggerPolarityPositive) && (gateLevel_ == gateLevelLow)) continue;
            if ((triggerPolarity_ == QETriggerPolarityNegative) && (gateLevel_ == gateLevelHigh)) continue;
        }

        lock();
        computePositions((double*)element.values);
        unlock();
    }

done:
    epicsThreadSleep(0.01);
    if (acquiring_) sendGetEvent();
}

void drvFX4::pollThread()
{
    while (1) {
        if (!FX4Connected_ && !wsStopping_) {
            reconnectWebSocket(wsUri_);
        }

        if (FX4Connected_ && !acquiring_) {
            sendGetEvent();
        }

        epicsThreadSleep(5.0);
    }
}

asynStatus drvFX4::setAcquireParams()
{
    if (!FX4Connected_ && !wsStopping_) {
        reconnectWebSocket(wsUri_);
    }

    if (!FX4Connected_) {
        return asynError;
    }

    int numAverage;
    int valuesPerRead;
    double sampleTime;
    double averagingTime;
    int numAcquire;

    getIntegerParam(P_TriggerMode,      &triggerMode_);
    getIntegerParam(P_TriggerPolarity,  &triggerPolarity_);
    getIntegerParam(P_AcquireMode,      &acquireMode_);
    getIntegerParam(P_ValuesPerRead,    &valuesPerRead);
    getDoubleParam (P_AveragingTime,    &averagingTime);
    getIntegerParam(P_NumAcquire,       &numAcquire);

    sampleTime = 10e-6 * valuesPerRead;
    setDoubleParam(P_SampleTime, sampleTime);

    if (triggerMode_ == QETriggerModeExtBulb) {
        numAverage = 0;
    } else {
        numAverage = (int)((averagingTime / sampleTime) + 0.5);
    }

    setIntegerParam(P_NumAverage, numAverage);
    numAverage_ = numAverage;

    return asynSuccess;
}

asynStatus drvFX4::setAcquire(epicsInt32 value)
{
    if (value == acquiring_) return asynSuccess;

    if (value && !FX4Connected_ && !wsStopping_) {
        reconnectWebSocket(wsUri_);
    }
    if (value && !FX4Connected_) return asynError;

    if (value) {
        startTime_ = 0;
        for (auto& adc : adcCache_) adc.clear();
        acquiring_ = 1;
        synchronized_ = false;
        timestampMismatch_ = false;
        numTriggerValues_ = 0;
        triggerActive_ = false;
        gateLevel_ = gateLevelUnknown;
        sendSubscribeEvent();
        sendGetEvent();
    } else {
        sendUnsubscribeEvent();
        acquiring_ = 0;
    }

    return drvQuadEM::setAcquire(value);
}

asynStatus drvFX4::setAcquireMode(epicsInt32 value)
{
    return setAcquireParams();
}

asynStatus drvFX4::setAveragingTime(epicsFloat64 value)
{
    return setAcquireParams();
}

asynStatus drvFX4::setNumAcquire(epicsInt32 value)
{
    return setAcquireParams();
}

asynStatus drvFX4::setTriggerMode(epicsInt32 value)
{
    return setAcquireParams();
}

asynStatus drvFX4::setTriggerPolarity(epicsInt32 value)
{
    return setAcquireParams();
}

asynStatus drvFX4::setValuesPerRead(epicsInt32 value)
{
    return setAcquireParams();
}

asynStatus drvFX4::readStatus()
{
    return asynSuccess;
}

asynStatus drvFX4::reset()
{
    return asynSuccess;
}

void drvFX4::exitHandler()
{
    lock();
    setAcquire(0);
    unlock();
}

void drvFX4::report(FILE *fp, int details)
{
    fprintf(fp, "%s: port=%s connected=%d\n",
            driverName, portName, FX4Connected_ ? 1 : 0);
    drvQuadEM::report(fp, details);
}

extern "C" {

int drvFX4Configure(const char *portName, const char *FX4_IP, int ringBufferSize)
{
    new drvFX4(portName, FX4_IP, ringBufferSize);
    return asynSuccess;
}

static const iocshArg initArg0 = { "portName", iocshArgString };
static const iocshArg initArg1 = { "FX4 IP address", iocshArgString };
static const iocshArg initArg2 = { "ring buffer size", iocshArgInt };
static const iocshArg * const initArgs[] = { &initArg0, &initArg1, &initArg2 };
static const iocshFuncDef initFuncDef = { "drvFX4Configure", 3, initArgs };

static void initCallFunc(const iocshArgBuf *args)
{
    drvFX4Configure(args[0].sval, args[1].sval, args[2].ival);
}

void drvFX4Register(void)
{
    if (!g_ixNetSystemInitialized.exchange(true)) {
        ix::initNetSystem();
    }
    iocshRegister(&initFuncDef, initCallFunc);
}

epicsExportRegistrar(drvFX4Register);

}
