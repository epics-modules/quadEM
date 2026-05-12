/*
 * drvFX4.cpp
 *
 * Asyn driver that inherits from the drvQuadEM class to control
 * the Pyramid FX4 4-channel picoammeter
 *
 * Author: Mark Rivers
 *
 * Created May 3, 2026
 */

#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <errno.h>
#include <math.h>

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

static const char *driverName="drvFX4";

static void pollThread(void *drvPvt)
{
    drvFX4 *pPvt = (drvFX4 *)drvPvt;
    pPvt->pollThread();
}

/** Constructor for the drvFX4 class.
  * Calls the constructor for the drvQuadEM base class.
  * \param[in] portName The name of the asyn port driver to be created.
  * \param[in] FX4_IP The IP address of the FX4, used for Websocket commmunication
  * \param[in] ringBufferSize The number of samples to hold in the input ring buffer.
  *            This should be large enough to hold all the samples between reads of the
  *            device, e.g. 1 ms SampleTime and 1 second read rate = 1000 samples.
  *            If 0 then default of 2048 is used.
  */
drvFX4::drvFX4(const char *portName, const char *FX4_IP, int ringBufferSize)
   : drvQuadEM(portName, ringBufferSize),
   FX4Connected_(false)

{
    std::string uri = "ws://" + std::string(FX4_IP);
    static const char* functionName = "drvFX4";

    ws_client_.init_asio();
    ws_client_.clear_access_channels(websocketpp::log::alevel::frame_header | websocketpp::log::alevel::frame_payload);
    ws_client_.set_open_handler(bind(&drvFX4::on_open, this, ::_1));
    ws_client_.set_message_handler(bind(&drvFX4::on_message, this, ::_1, ::_2));
    websocketpp::lib::error_code ec;
    auto con = ws_client_.get_connection(uri, ec);
    if (ec) {
        std::cerr << "FX4 connection error: " << ec.message() << std::endl;
        return;
    }
    ws_client_.connect(con);
    ws_thread_ = new std::thread([&]() {
        ws_client_.run();
    });

    // Wait for connection
    while (!FX4Connected_) {
        epicsThreadSleep(0.01);
    }

    acquiring_ = 0;
    resolution_ = 24;
    setIntegerParam(P_Model, QE_ModelFX4);

    /* Create a polling thread that periodically sends get messages to avoid disconnect */
    if (epicsThreadCreate("drvFX4Task",
                          epicsThreadPriorityMedium,
                          epicsThreadGetStackSize(epicsThreadStackMedium),
                          (EPICSTHREADFUNC)::pollThread,
                          this) == NULL) {
        printf("%s::%s: epicsThreadCreate failure\n", driverName, functionName);
        return;
    }

    callParamCallbacks();
}

//--------------------------------------------------
// WebSocket handlers
//--------------------------------------------------
void drvFX4::on_open(connection_hdl hdl) {
    ws_hdl_ = hdl;
    FX4Connected_ = true;
}

void drvFX4::on_message(connection_hdl, client::message_ptr msg) {
    try {
        json response = json::parse(msg->get_payload());
        onMessageEvent(response["event"], response["data"]);
    } catch (std::exception& e) {
        std::cerr << "JSON parse error: " << e.what() << std::endl;
    }
}

//--------------------------------------------------
// Send event
//--------------------------------------------------
void drvFX4::sendEventData(const std::string& event, json data = nullptr) {
    json msg;
    msg["event"] = event;
    msg["data"] = data;

    ws_client_.send(ws_hdl_, msg.dump(), websocketpp::frame::opcode::text);
}

//--------------------------------------------------
void drvFX4::sendSubscribeEvent() {
    json data = {
        {ADC_PATHS[0], true},
        {ADC_PATHS[1], true},
        {ADC_PATHS[2], true},
        {ADC_PATHS[3], true},
        {GATE_PATH, true}
    };
    sendEventData("subscribe", data);
}

//--------------------------------------------------
void drvFX4::sendUnsubscribeEvent() {
    json data = {};
    sendEventData("subscribe", data);
}

//--------------------------------------------------
void drvFX4::sendGetEvent() {
    sendEventData("get");
}

//--------------------------------------------------
// Message handler
//--------------------------------------------------
void drvFX4::onMessageEvent(const std::string& event, const json& data) {
    static const char *functionName = "drvFX4::onMessageEvent";
    std::multiset<sortedListElement> eventList;
    double values[4]={0}, times[4];
    size_t minSize, maxSize;


    if (!data.empty()) {
        //asynPrint(pasynUserSelf, ASYN_TRACEIO_DRIVER, "%s: %s %s\n", functionName, event.c_str(), (data.dump()).c_str());
    }

    // If not acquiring then return, ignore the messages from the periodic get requests required to keep the websocket alive
    if (!acquiring_) return;

    if (event != "update") goto done;
    for (auto& [path, vals] : data.items()) {
        int chan=0;
        bool isGate = (path == GATE_PATH);
        for (int i=0; i<FX4_NUM_CHANS; i++) {
            if (path == ADC_PATHS[i]) chan = i;
        }
        for (auto& v : vals) {
            epicsInt64 time = v[1];
            if (startTime_ == 0) startTime_ = time;
            double timestamp = (time - startTime_)/1e9;
            if (isGate) {
                values[0] = v[0] ? 1 : 0;
                eventList.insert(*(new sortedListElement(gateEvent, values, timestamp)));
            } else {
                adcCache_[chan].push_back({v[0], timestamp});
            }
            if (isGate) {
                asynPrint(pasynUserSelf, ASYN_TRACEIO_DRIVER, "Gate event, value=%f, time=%f, ADC1 last time=%f\n",
                         values[0], timestamp, adcCache_[0].back().time);
            }
        }
    }
    if (adcCache_[0].size() <= 0) goto done;
    asynPrint(pasynUserSelf, ASYN_TRACEIO_DRIVER, "%s: Samples=%lu %lu %lu %lu\n"
                                                  "    ADCs oldest=%f %f %f %f\n"
                                                  "   Times oldest=%f %f %f %f\n"
                                                  "    ADCs newest=%f %f %f %f\n"
                                                  "   Times newest=%f %f %f %f\n", functionName,
        adcCache_[0].size(),       adcCache_[1].size(),       adcCache_[2].size(),       adcCache_[3].size(),
        adcCache_[0].front().val,  adcCache_[1].front().val,  adcCache_[2].front().val,  adcCache_[3].front().val,
        adcCache_[0].front().time, adcCache_[1].front().time, adcCache_[2].front().time, adcCache_[3].front().time,
        adcCache_[0].back().val,   adcCache_[1].back().val,   adcCache_[2].back().val,   adcCache_[3].back().val,
        adcCache_[0].back().time,  adcCache_[1].back().time,  adcCache_[2].back().time,  adcCache_[3].back().time);
    minSize = std::min({adcCache_[0].size(), adcCache_[1].size(), adcCache_[2].size(), adcCache_[3].size()});
    maxSize = std::max({adcCache_[0].size(), adcCache_[1].size(), adcCache_[2].size(), adcCache_[3].size()});
    asynPrint(pasynUserSelf, ASYN_TRACEIO_DRIVER, "%s minimum size=%lu, size=%lu %lu %lu %lu\n",
              functionName, minSize, adcCache_[0].size(), adcCache_[1].size(),  adcCache_[2].size(), adcCache_[3].size());
    if (minSize != maxSize) {
        if (!synchronized_) {
            // Throw away these readings and return
            asynPrint(pasynUserSelf, ASYN_TRACE_ERROR, "%s not synchronized and different number of samples per channel=%lu %lu %lu %lu\n",
                  functionName, adcCache_[0].size(), adcCache_[1].size(), adcCache_[2].size(), adcCache_[3].size());
            for (auto& adc : adcCache_) adc.clear();
            goto done;
        }
    } else {
        synchronized_ = true;
    }

    for (size_t i=0; i<minSize; i++) {
        for (size_t j=0; j<4; j++) {
            times[j] = adcCache_[j].front().time;
            values[j] = adcCache_[j].front().val;
        }
        if ((times[1] != times[0]) || (times[2] != times[0]) || (times[3] != times[0])) {
            if (!timestampMismatch_) {
                asynPrint(pasynUserSelf, ASYN_TRACE_ERROR, "%s timestamps are not the same for sample %lu %f %f %f %f\n",
                          functionName, i, times[0], times[1], times[2], times[3]);
                timestampMismatch_ = true;
            }
        } else {
            if (timestampMismatch_) {
                asynPrint(pasynUserSelf, ASYN_TRACE_ERROR, "%s timestamps back to normal\n", functionName);
                timestampMismatch_ = false;
            }
        }
        eventList.insert(*(new sortedListElement(adcEvent, values, times[0])));
        for (size_t j=0; j<4; j++) adcCache_[j].pop_front();
    }

    // We now have a time-sorted list of ADC values and gate events
    for (const sortedListElement& element: eventList) {
        if (element.eventType == gateEvent) {
            gateLevel_ = (gateLevel_t)element.values[0];
            if (triggerMode_ == QETriggerModeExtTrigger) {
                if (((triggerPolarity_ == QETriggerPolarityPositive) && (gateLevel_ == gateLevelHigh)) ||
                    ((triggerPolarity_ == QETriggerPolarityNegative) && (gateLevel_ == gateLevelLow))) {
                    // We just got a trigger event
                    asynPrint(pasynUserSelf, ASYN_TRACEIO_DRIVER, "trigger event: gateLevel=%d, triggerActive=%d, numTriggerValues=%d\n",
                              gateLevel_, triggerActive_, numTriggerValues_);
                    triggerActive_ = true;
                    numTriggerValues_ = 0;
                }
            }
            else if (triggerMode_ == QETriggerModeExtBulb) {
                if (((triggerPolarity_ == QETriggerPolarityPositive) && (gateLevel_ == gateLevelLow)) ||
                    ((triggerPolarity_ == QETriggerPolarityNegative) && (gateLevel_ == gateLevelHigh))) {
                    // We just got a bulb end event
                    asynPrint(pasynUserSelf, ASYN_TRACEIO_DRIVER, "bulb event: gateLevel=%d\n", gateLevel_);
                    // Do callbacks on the trailing edge of the gate
                    triggerCallbacks();
                }
            }
            continue;
        }
        // If we are in external trigger mode only collect data if trigger is active
        if (triggerMode_ == QETriggerModeExtTrigger) {
            if (!triggerActive_) continue;
            numTriggerValues_++;
            if (numTriggerValues_ > numAverage_) {
                triggerActive_ = false;
                continue;
            }
        }
        // If we are in ExtGate or ExtBulb mode and the gate condition is not satisfied then skip this point
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
    while(1) {
        if (!acquiring_) sendGetEvent();
        epicsThreadSleep(5.0);
    }
}

asynStatus drvFX4::setAcquireParams()
{
    int numAverage;
    int valuesPerRead;
    double sampleTime;
    double averagingTime;
    int numAcquire;
    //static const char *functionName = "setAcquireParams";

    getIntegerParam(P_TriggerMode,      &triggerMode_);
    getIntegerParam(P_TriggerPolarity,  &triggerPolarity_);
    getIntegerParam(P_AcquireMode,      &acquireMode_);
    getIntegerParam(P_ValuesPerRead,    &valuesPerRead);
    getDoubleParam (P_AveragingTime,    &averagingTime);
    getIntegerParam(P_NumAcquire,       &numAcquire);

    // Compute the sample time.  This is 10 microseconds times valuesPerRead.
    sampleTime = 10e-6 * valuesPerRead;
    setDoubleParam(P_SampleTime, sampleTime);


    // Compute the number of values that will be accumulated in the ring buffer before averaging
    if (triggerMode_ == QETriggerModeExtBulb) {
        numAverage = 0;
    } else {
        numAverage = (int)((averagingTime / sampleTime) + 0.5);
    }
    setIntegerParam(P_NumAverage, numAverage);
    numAverage_ = numAverage;

    return asynSuccess;
}

/** Starts and stops the electrometer.
  * \param[in] value 1 to start the electrometer, 0 to stop it.
  */
asynStatus drvFX4::setAcquire(epicsInt32 value)
{
     //static const char *functionName = "drvFX4::setAcquire";

    // If we are already in the desired state return immediately
    if (value == acquiring_) return asynSuccess;

    if (value) {
        startTime_ = 0;
        for (auto& adc : adcCache_) adc.clear();
        acquiring_ = 1;
        synchronized_ = false;
        timestampMismatch_ = false;
        numTriggerValues_ = 0;
        triggerActive_ = false;
        sendSubscribeEvent();
        sendGetEvent();
    } else {
        sendUnsubscribeEvent();
        acquiring_ = 0;
    }
    //  Call the base class that does some common things
    return drvQuadEM::setAcquire(value);

    return asynSuccess;
}


/** Sets the acquire mode.
  * \param[in] value Acquire mode.
  */
asynStatus drvFX4::setAcquireMode(epicsInt32 value)
{
    return setAcquireParams();
}

/** Sets the averaging time.
  * \param[in] value Averaging time.
  */
asynStatus drvFX4::setAveragingTime(epicsFloat64 value)
{
    return setAcquireParams();
}

/** Sets the number of triggers.
  * \param[in] value Number of triggers.
  */
asynStatus drvFX4::setNumAcquire(epicsInt32 value)
{
    return setAcquireParams();
}

/** Sets the trigger mode
  * \param[in] value 0 = internal,
  *                  1 = external trigger (with predefined nr of samples)
  *                  2 = external gate.
  */
asynStatus drvFX4::setTriggerMode(epicsInt32 value)
{
    return setAcquireParams();
}

/** Sets the trigger polarity
  * \param[in] value 0 = rising edge,
  *                  1 = falling edge
  */
asynStatus drvFX4::setTriggerPolarity(epicsInt32 value)
{
    return setAcquireParams();
}

/** Sets the values per read.
  * \param[in] value Values per read. Minimum depends on number of channels.
  */
asynStatus drvFX4::setValuesPerRead(epicsInt32 value)
{
    return setAcquireParams();
}

/** Reads all the settings back from the electrometer.
  */
asynStatus drvFX4::readStatus()
{
    return asynSuccess;
}

asynStatus drvFX4::reset()
{
    return asynSuccess;
}

/** Exit handler.  Turns off acquire so we don't waste network bandwidth when the IOC stops */
void drvFX4::exitHandler()
{
    lock();
    setAcquire(0);
    unlock();
}

/** Report  parameters
  * \param[in] fp The file pointer to write to
  * \param[in] details The level of detail requested
  */
void drvFX4::report(FILE *fp, int details)
{
    fprintf(fp, "%s: port=%s\n",
            driverName, portName);
    if (details > 0) {
    }
    drvQuadEM::report(fp, details);
}


/* Configuration routine.  Called directly, or from the iocsh function below */

extern "C" {

/** EPICS iocsh callable function to call constructor for the drvFX4 class.
  * \param[in] portName The name of the asyn port driver to be created.
  * \param[in] FX4_IP The IP address of the FX4.
  * \param[in] ringBufferSize The number of samples to hold in the input ring buffer.
  *            This should be large enough to hold all the samples between reads of the
  *            device, e.g. 1 ms SampleTime and 1 second read rate = 1000 samples.
  *            If 0 then default of 2048 is used.
  */
int drvFX4Configure(const char *portName, const char *FX4_IP, int ringBufferSize)
{
    new drvFX4(portName, FX4_IP, ringBufferSize);
    return(asynSuccess);
}


/* EPICS iocsh shell commands */

static const iocshArg initArg0 = { "portName",iocshArgString};
static const iocshArg initArg1 = { "FX4 IP address",iocshArgString};
static const iocshArg initArg2 = { "ring buffer size",iocshArgInt};
static const iocshArg * const initArgs[] = {&initArg0,
                                            &initArg1,
                                            &initArg2};
static const iocshFuncDef initFuncDef = {"drvFX4Configure",3,initArgs};
static void initCallFunc(const iocshArgBuf *args)
{
    drvFX4Configure(args[0].sval, args[1].sval, args[2].ival);
}

void drvFX4Register(void)
{
    iocshRegister(&initFuncDef,initCallFunc);
}

epicsExportRegistrar(drvFX4Register);

}
