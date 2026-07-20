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

/**
 * onMessage - handles incoming binary MessagePack data from the WebSocket.
 *
 * The FX4 device sends MessagePack-encoded messages with the structure:
 *   { "event": <string>, "data": <map or nil> }
 *
 * The ixwebsocket callback passes binary frames as std::string containing
 * raw bytes, so payload.data() / payload.size() give us the buffer.
 */
void drvFX4::onMessage(const std::string& payload)
{
    static const char *functionName = "onMessage";
    try {
        /* Unpack the top-level MessagePack object from the raw byte buffer */
        msgpack::object_handle oh = msgpack::unpack(payload.data(), payload.size());
        msgpack::object obj = oh.get();

        /* The top-level object must be a map */
        if (obj.type != msgpack::type::MAP) return;

        /* Convert to a std::map for easy key lookup */
        std::map<std::string, msgpack::object> root;
        obj.convert(root);

        auto eventIt = root.find("event");
        if (eventIt == root.end()) return;

        std::string event;
        eventIt->second.convert(event);

        /* "data" is optional; use a nil object if absent */
        msgpack::object dataObj;
        auto dataIt = root.find("data");
        if (dataIt != root.end()) {
            dataObj = dataIt->second;
        }
        /* dataObj defaults to msgpack::type::NIL if not assigned */

        onMessageEvent(event, dataObj);

    } catch (const std::exception& e) {
        asynPrint(pasynUserSelf, ASYN_TRACE_ERROR,
            "%s::%s MessagePack parse error: %s\n",
            driverName, functionName, e.what());
    }
}

void drvFX4::onClose(int code, const std::string& reason)
{
    static const char *functionName = "onClose";
    FX4Connected_ = false;
    if (!wsStopping_) {
        asynPrint(pasynUserSelf, ASYN_TRACE_ERROR,
            "%s::%s WebSocket closed code=%d, reason=%s\n",
            driverName, functionName, code, reason.c_str());            
    }
}

void drvFX4::onError(const std::string& reason)
{
    static const char *functionName = "onError";
    FX4Connected_ = false;
    if (!wsStopping_) {
        asynPrint(pasynUserSelf, ASYN_TRACE_ERROR,
            "%s::%s WebSocket error, reason=%s\n",
            driverName, functionName, reason.c_str());
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
    ws_.addSubProtocol("mpack");
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

/**
 * sendEventData - serialises an event + data map into MessagePack and sends
 * it as a binary WebSocket frame.
 *
 * @param event  The event name string (e.g. "subscribe", "get").
 * @param data   A pre-packed msgpack::sbuffer containing the "data" value.
 *               Pass an empty/nil buffer to send a nil data field.
 */
void drvFX4::sendEventData(const std::string& event, const msgpack::sbuffer& dataBuf)
{
    static const char *functionName = "sendEventData";
    /* Build the top-level map: { "event": <event>, "data": <data> } */
    msgpack::sbuffer msgBuf;
    msgpack::packer<msgpack::sbuffer> pk(msgBuf);

    /* Pack a 2-element map */
    pk.pack_map(2);

    /* Key: "event", Value: event string */
    pk.pack(std::string("event"));
    pk.pack(event);

    /* Key: "data", Value: the pre-serialised data object (raw bytes) */
    pk.pack(std::string("data"));

    if (dataBuf.size() > 0) {
        /*
         * Write the already-packed data bytes directly into the stream.
         * msgpack::packer::pack_raw_body copies raw bytes verbatim, which
         * is exactly what we want when dataBuf already contains a valid
         * MessagePack object.
         */
        msgBuf.write(dataBuf.data(), dataBuf.size());
    } else {
        /* No data provided – send nil */
        pk.pack_nil();
    }

    std::lock_guard<std::mutex> guard(wsMutex_);
    if (wsStopping_ || !FX4Connected_) return;

    /*
     * Send as a binary WebSocket frame.
     * ws_.sendBinary() accepts a std::string containing raw bytes.
     */
    std::string binaryPayload(msgBuf.data(), msgBuf.size());
    ix::WebSocketSendInfo result = ws_.sendBinary(binaryPayload);
    if (!result.success) {
        asynPrint(pasynUserSelf, ASYN_TRACE_ERROR,
            "%s::%s send failed\n", driverName, functionName);            
    }
}

/**
 * sendSubscribeEvent - subscribe to all four ADC channels and the gate.
 */
void drvFX4::sendSubscribeEvent()
{
    msgpack::sbuffer dataBuf;
    msgpack::packer<msgpack::sbuffer> pk(dataBuf);

    /* Pack a map with 5 entries: 4 ADC paths + 1 gate path */
    pk.pack_map(FX4_NUM_CHANS + 1);
    for (int i = 0; i < FX4_NUM_CHANS; i++) {
        pk.pack(std::string(ADC_PATHS[i]));
        pk.pack(true);
    }
    pk.pack(std::string(GATE_PATH));
    pk.pack(true);

    sendEventData("subscribe", dataBuf);
}

/**
 * sendUnsubscribeEvent - send an empty subscribe map to cancel all subscriptions.
 */
void drvFX4::sendUnsubscribeEvent()
{
    msgpack::sbuffer dataBuf;
    msgpack::packer<msgpack::sbuffer> pk(dataBuf);
    pk.pack_map(0);   /* empty map */

    sendEventData("subscribe", dataBuf);
}

/**
 * sendGetEvent - request the latest values from the device.
 */
void drvFX4::sendGetEvent()
{
    /* Empty sbuffer signals sendEventData() to write nil */
    msgpack::sbuffer dataBuf;
    sendEventData("get", dataBuf);
}

/**
 * onMessageEvent - processes a decoded MessagePack "update" event.
 *
 * The "data" field is expected to be a MessagePack map of the form:
 *   { <path>: [ [<value>, <timestamp_ns>], ... ], ... }
 *
 * @param event   The event name string.
 * @param dataObj The msgpack::object representing the "data" field.
 */
void drvFX4::onMessageEvent(const std::string& event, const msgpack::object& dataObj)
{
    static const char *functionName = "drvFX4::onMessageEvent";
    std::multiset<sortedListElement> eventList;
    double values[4] = {0, 0, 0, 0};
    double times[4]  = {0, 0, 0, 0};
    size_t minSize, maxSize;

    if (!acquiring_) return;
    if (event != "update") goto done;

    /* The data field must be a map */
    if (dataObj.type != msgpack::type::MAP) goto done;

    /*
     * Iterate over every key-value pair in the data map.
     * msgpack::object_kv holds { .key, .val } both as msgpack::object.
     */
    for (uint32_t pi = 0; pi < dataObj.via.map.size; pi++) {
        const msgpack::object_kv& kv = dataObj.via.map.ptr[pi];

        /* Key must be a string (the path name) */
        if (kv.key.type != msgpack::type::STR) continue;

        std::string path;
        kv.key.convert(path);

        int  chan   = -1;
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

        /* Value must be an array of [value, timestamp] pairs */
        if (kv.val.type != msgpack::type::ARRAY) continue;

        for (uint32_t vi = 0; vi < kv.val.via.array.size; vi++) {
            const msgpack::object& v = kv.val.via.array.ptr[vi];

            /* Each element must itself be a 2-element array */
            if (v.type != msgpack::type::ARRAY || v.via.array.size < 2) continue;

            const msgpack::object& rawVal  = v.via.array.ptr[0];
            const msgpack::object& rawTime = v.via.array.ptr[1];

            /* Deserialise the timestamp (nanoseconds, 64-bit integer) */
            epicsInt64 time = 0;
            try {
                rawTime.convert(time);
            } catch (...) {
                continue;
            }

            if (startTime_ == 0) startTime_ = time;
            double timestamp = (time - startTime_) / 1e9;

            if (isGate) {
                /*
                 * Gate value is a boolean in MessagePack.
                 * msgpack::type::BOOLEAN -> rawVal.via.boolean
                 */
                bool gateVal = false;
                try {
                    rawVal.convert(gateVal);
                } catch (...) {
                    continue;
                }
                values[0] = gateVal ? 1.0 : 0.0;
                eventList.insert(sortedListElement(gateEvent, values, timestamp));

                if (!adcCache_[0].empty()) {
                    asynPrint(pasynUserSelf, ASYN_TRACE_WARNING,
                              "Gate event, value=%f, time=%f, ADC1 last time=%f\n",
                              values[0], timestamp, adcCache_[0].back().time);
                }
            } else {
                /* ADC value is a floating-point number */
                double adcValue = 0.0;
                try {
                    rawVal.convert(adcValue);
                } catch (...) {
                    continue;
                }
                adcCache_[chan].push_back({adcValue, timestamp});
            }
        }
    }

    if (adcCache_[0].empty()) goto done;

    asynPrint(pasynUserSelf, ASYN_TRACE_WARNING,
              "%s: Samples=%lu %lu %lu %lu\n"
              "    ADCs oldest=%f %f %f %f\n"
              "   Times oldest=%f %f %f %f\n"
              "    ADCs newest=%f %f %f %f\n"
              "   Times newest=%f %f %f %f\n", functionName,
              (unsigned long)adcCache_[0].size(), (unsigned long)adcCache_[1].size(),
              (unsigned long)adcCache_[2].size(), (unsigned long)adcCache_[3].size(),
              adcCache_[0].front().val,  adcCache_[1].front().val, 
              adcCache_[2].front().val,  adcCache_[3].front().val,
              adcCache_[0].front().time, adcCache_[1].front().time,
              adcCache_[2].front().time, adcCache_[3].front().time,
              adcCache_[0].back().val,   adcCache_[1].back().val,
              adcCache_[2].back().val,   adcCache_[3].back().val,
              adcCache_[0].back().time,  adcCache_[1].back().time,
              adcCache_[2].back().time,  adcCache_[3].back().time);

    minSize = std::min({adcCache_[0].size(), adcCache_[1].size(),
                        adcCache_[2].size(), adcCache_[3].size()});
    maxSize = std::max({adcCache_[0].size(), adcCache_[1].size(),
                        adcCache_[2].size(), adcCache_[3].size()});

    asynPrint(pasynUserSelf, ASYN_TRACE_WARNING,
              "%s minimum size=%lu, size=%lu %lu %lu %lu\n",
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
            times[j]  = adcCache_[j].front().time;
            values[j] = adcCache_[j].front().val;
        }

        if ((times[1] != times[0]) || (times[2] != times[0]) || (times[3] != times[0])) {
            if (!timestampMismatch_) {
                asynPrint(pasynUserSelf, ASYN_TRACE_ERROR,
                          "%s timestamps are not the same for sample %lu %f %f %f %f\n",
                          functionName, (unsigned long)i,
                          times[0], times[1], times[2], times[3]);
                timestampMismatch_ = true;
            }
        } else {
            if (timestampMismatch_) {
                asynPrint(pasynUserSelf, ASYN_TRACE_ERROR,
                          "%s timestamps back to normal\n", functionName);
                timestampMismatch_ = false;
            }
        }

        eventList.insert(sortedListElement(adcEvent, values, times[0]));
        for (size_t j = 0; j < 4; j++) adcCache_[j].pop_front();
    }

    for (const sortedListElement& element : eventList) {
        if (element.eventType == gateEvent) {
            gateLevel_ = (gateLevel_t)element.values[0];
            if (triggerMode_ != QETriggerModeFreeRun) {
                asynPrint(pasynUserSelf, ASYN_TRACEIO_DRIVER,
                          "trigger event: gateLevel=%d, timeStamp=%f, numADCValues=%d\n",
                          gateLevel_, element.timeStamp, numTriggerValues_);
            }
            if (triggerMode_ == QETriggerModeExtTrigger) {
                if (((triggerPolarity_ == QETriggerPolarityPositive) && (gateLevel_ == gateLevelHigh)) ||
                    ((triggerPolarity_ == QETriggerPolarityNegative) && (gateLevel_ == gateLevelLow))) {
                    triggerActive_     = true;
                    numTriggerValues_  = 0;
                }
            } else if (triggerMode_ == QETriggerModeExtBulb) {
                if (((triggerPolarity_ == QETriggerPolarityPositive) && (gateLevel_ == gateLevelLow)) ||
                    ((triggerPolarity_ == QETriggerPolarityNegative) && (gateLevel_ == gateLevelHigh))) {
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
            if ((triggerPolarity_ == QETriggerPolarityPositive) && (gateLevel_ == gateLevelLow))  continue;
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

    int    numAverage;
    int    valuesPerRead;
    double sampleTime;
    double averagingTime;
    int    numAcquire;

    getIntegerParam(P_TriggerMode,     &triggerMode_);
    getIntegerParam(P_TriggerPolarity, &triggerPolarity_);
    getIntegerParam(P_AcquireMode,     &acquireMode_);
    getIntegerParam(P_ValuesPerRead,   &valuesPerRead);
    getDoubleParam (P_AveragingTime,   &averagingTime);
    getIntegerParam(P_NumAcquire,      &numAcquire);

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
        acquiring_         = 1;
        synchronized_      = false;
        timestampMismatch_ = false;
        numTriggerValues_  = 0;
        triggerActive_     = false;
        gateLevel_         = gateLevelUnknown;
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

static const iocshArg initArg0 = { "portName",         iocshArgString };
static const iocshArg initArg1 = { "FX4 IP address",   iocshArgString };
static const iocshArg initArg2 = { "ring buffer size", iocshArgInt    };
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

} // extern "C"
