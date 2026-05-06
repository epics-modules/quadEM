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

/** Constructor for the drvFX4 class.
  * Calls the constructor for the drvQuadEM base class.
  * \param[in] portName The name of the asyn port driver to be created.
  * \param[in] QEPortName The name of the asyn communication port to the TetrAMM
  *            created with drvAsynIPPortConfigure or drvAsynSerialPortConfigure
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

    createParam(P_InterlockStatusString, asynParamInt32,   &P_InterlockStatus);

    acquiring_ = 0;
    resolution_ = 24;
    setIntegerParam(P_Model, QE_ModelFX4);

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
        {"/fx4/adc/channel_1/value", true},
        {"/fx4/adc/channel_2/value", true},
        {"/fx4/adc/channel_3/value", true},
        {"/fx4/adc/channel_4/value", true},
        {"/fx4/gpio_0/22/readback/value", true}
    };

    sendEventData("subscribe", data);
}

//--------------------------------------------------
void drvFX4::sendUnsubscribeEvent() {
    json data = {
        {"/fx4/adc/channel_1/value", true},
        {"/fx4/adc/channel_2/value", true},
        {"/fx4/adc/channel_3/value", true},
        {"/fx4/adc/channel_4/value", true},
        {"/fx4/gpio_0/22/readback/value", true}
    };

    sendEventData("unsubscribe", data);
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
    epicsFloat64 currents[QE_MAX_INPUTS];
    if (!data.empty()) {
//        asynPrint(pasynUserSelf, ASYN_TRACEIO_DRIVER, "%s: %s %s\n", functionName, event.c_str(), (data.dump()).c_str());
    }

    if (event == "update") {
        std::vector<double> adc1, adc2, adc3, adc4;
        std::vector<bool> bit1;
        for (auto& [path, values] : data.items()) {
            for (auto& v : values) {
                if      (path.find("channel_1") != std::string::npos) adc1.push_back(v[0]);
                else if (path.find("channel_2") != std::string::npos) adc2.push_back(v[0]);
                else if (path.find("channel_3") != std::string::npos) adc3.push_back(v[0]);
                else if (path.find("channel_4") != std::string::npos) adc4.push_back(v[0]);
                else if (path.find("gpio") != std::string::npos) bit1.push_back(v[0]);
            }
        }
        if (bit1.size() > 0) {
            asynPrint(pasynUserSelf, ASYN_TRACEIO_DRIVER, "%s: Bit1=%d\n", functionName, bit1[0]?1:0);
        }

        if (adc1.size() > 0) {
            if (adc2.size() != adc1.size() || adc3.size() != adc1.size() || adc4.size() != adc1.size()) {
                asynPrint(pasynUserSelf, ASYN_TRACE_ERROR, "%s unequal number of samples %d %d %d %d\n",
                          functionName, (int)adc1.size(), (int)adc2.size(), (int)adc3.size(), (int)adc4.size());
                goto error;
            }
            asynPrint(pasynUserSelf, ASYN_TRACEIO_DRIVER, "%s: Samples=%d %d %d %d, ADCs[0]=%f %f %f %f\n",
                      functionName, (int)adc1.size(), (int)adc2.size(), (int)adc3.size(), (int)adc4.size(),
                      adc1[0], adc2[0], adc3[0], adc4[0]);
            for (size_t i=0; i<adc1.size(); i++) {
                currents[0] = adc1[i];
                currents[1] = adc2[i];
                currents[2] = adc3[i];
                currents[3] = adc4[i];
                lock();
                computePositions(currents);
                unlock();
            }
            epicsThreadSleep(0.00);
        }
        error:
        epicsThreadSleep(0.01);

        if (acquiring_) sendGetEvent();
    }
}

asynStatus drvFX4::setAcquireParams()
{
    int numAverage;
    int acquireMode;
    int valuesPerRead;
    int triggerMode;
    int triggerPolarity;
    double sampleTime;
    double averagingTime;
    int numAcquire;
    //static const char *functionName = "setAcquireParams";

    getIntegerParam(P_TriggerMode,      &triggerMode);
    getIntegerParam(P_TriggerPolarity,  &triggerPolarity);
    getIntegerParam(P_AcquireMode,      &acquireMode);
    getIntegerParam(P_ValuesPerRead,    &valuesPerRead);
    getDoubleParam (P_AveragingTime,    &averagingTime);
    getIntegerParam(P_NumAcquire,       &numAcquire);

    // Compute the sample time.  This is 10 microseconds times valuesPerRead.
    sampleTime = 10e-6 * valuesPerRead;
    setDoubleParam(P_SampleTime, sampleTime);

    // Compute the number of values that will be accumulated in the ring buffer before averaging
    if (triggerMode == QETriggerModeExtBulb) {
        numAverage = 0;
    } else {
        numAverage = (int)((averagingTime / sampleTime) + 0.5);
    }
    setIntegerParam(P_NumAverage, numAverage);

    return asynSuccess;
}

/** Starts and stops the electrometer.
  * \param[in] value 1 to start the electrometer, 0 to stop it.
  */
asynStatus drvFX4::setAcquire(epicsInt32 value)
{
    if (value) {
        sendSubscribeEvent();
        sendGetEvent();
        acquiring_ = 1;
    } else {
        sendUnsubscribeEvent();
        acquiring_ = 0;
    }

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

