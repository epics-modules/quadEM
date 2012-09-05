/*
 * drvAH401B.h
 * 
 * Asyn driver that inherits from the asynPortDriver class to control the Ellectra AH401B 4-channel picoammeter
 *
 * Author: Mark Rivers
 *
 * Created June 3, 2012
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
#include <asynOctetSyncIO.h>
#include <iocsh.h>

#include "drvAH401B.h"
#include <epicsExport.h>


static const char *driverName="drvAH401B";
static void readThread(void *drvPvt);


/** Constructor for the drvAH401B class.
  * Calls the constructor for the drvQuadEM base class.
  * \param[in] portName The name of the asyn port driver to be created.
  * \param[in] QEPortName The name of the asyn communication port to the AH401B 
  *            created with drvAsynIPPortConfigure or drvAsynSerialPortConfigure */
drvAH401B::drvAH401B(const char *portName, const char *QEPortName) 
   : drvQuadEM(portName, 0)
  
{
    asynStatus status;
    const char *functionName = "drvAH401B";
    
    QEPortName_ = epicsStrDup(QEPortName);
    
    readDataEvent_ = epicsEventCreate(epicsEventEmpty);
    
    // Connect to the server
    status = pasynOctetSyncIO->connect(QEPortName, 0, &pasynUserMeter_, NULL);
    if (status) {
        printf("%s:%s: error calling pasynOctetSyncIO->connect, status=%d, error=%s\n", 
               driverName, functionName, status, pasynUserMeter_->errorMessage);
        return;
    }

    setAcquire(0);
    setIntegerParam(P_Acquire, 0);
    strcpy(outString_, "VER ?");
    writeReadMeter();
    firmwareVersion_ = epicsStrDup(inString_);
    setBinaryMode();

    // Read the current settings from the device.  This will set parameters in the parameter library.
    getSettings();
    
    /* Create the thread that reads the meter */
    status = (asynStatus)(epicsThreadCreate("drvAH401BTask",
                          epicsThreadPriorityMedium,
                          epicsThreadGetStackSize(epicsThreadStackMedium),
                          (EPICSTHREADFUNC)::readThread,
                          this) == NULL);
    if (status) {
        printf("%s:%s: epicsThreadCreate failure, status=%d\n", driverName, functionName, status);
        return;
    }
}



static void readThread(void *drvPvt)
{
    drvAH401B *pPvt = (drvAH401B *)drvPvt;
    
    pPvt->readThread();
}

/** Sends a command to the AH401B and reads the response.
  * If meter is acquiring turns it off and waits for it to stop sending data */ 
asynStatus drvAH401B::sendCommand()
{
  int prevAcquiring;
  asynStatus status;
  const char *functionName="sendCommand";
  
  prevAcquiring = acquiring_;
  if (prevAcquiring) setAcquire(0);
  status = writeReadMeter();
  if (strcmp(inString_, "ACK") != 0) {
      asynPrint(pasynUserSelf, ASYN_TRACE_ERROR, 
          "%s:%s: error, expected ACK, received %s\n",
          driverName, functionName, inString_);
  }
  if (prevAcquiring) setAcquire(1);
  return status;
}


/** Writes a string to the AH401B and reads the response.
  * If meter is acquiring turns it off and waits for it to stop sending data */ 
asynStatus drvAH401B::writeReadMeter()
{
  size_t nread;
  size_t nwrite;
  asynStatus status;
  int eomReason;
  //const char *functionName="writeReadMeter";
  
  status = pasynOctetSyncIO->writeRead(pasynUserMeter_, outString_, strlen(outString_), 
                                       inString_, sizeof(inString_), AH401B_TIMEOUT, &nwrite, &nread, &eomReason);                                      
  return status;
}


/** Read thread to read the data from the electrometer when it is in continuous acquire mode.
  * Reads the data, computes the sums and positions, and does callbacks.
  */
void drvAH401B::readThread(void)
{
    int acquire;
    asynStatus status;
    size_t nRead; 
    int eomReason;
    epicsInt32 raw[QE_MAX_INPUTS];
    unsigned char input[12];
    static const char *functionName = "readThread";
    
    /* Loop forever */
    lock();
    while (1) {
        getIntegerParam(P_Acquire, &acquire);
        //  We check both P_Acquire and acquiring_. P_Acquire means that EPICS "wants" to acquire, acquiring_ means
        // that we are actually acquiring, which could be false if the device is unreachable.
        if (!acquire || !acquiring_) {
            unlock();
            epicsEventWait(readDataEvent_);
            lock();
        }
        unlock();
        status = pasynOctetSyncIO->read(pasynUserMeter_, (char *)input, sizeof(input), 
                                        AH401B_TIMEOUT, &nRead, &eomReason);
        lock();
        if ((status == asynSuccess) && (nRead == 12) && (eomReason == ASYN_EOM_CNT)) {
            raw[0] = input[2]<<16 | input[1]<<8 | input[0];
            raw[1] = input[5]<<16 | input[4]<<8 | input[3];
            raw[2] = input[8]<<16 | input[7]<<8 | input[6];
            raw[3] = input[11]<<16 | input[10]<<8 | input[9];
            asynPrint(pasynUserSelf, ASYN_TRACEIO_DRIVER, 
                "%s:%s: nRead=%d, eomReason=%d\n"
                "input=%x %x %x %x %x %x %x %x %x %x %x %x\n"
                "raw=%d %d %d %d\n",
                driverName, functionName, nRead, eomReason, 
                input[0], input[1], input[2], input[3], input[4], input[5], input[6], input[7], input[8], input[9], input[10], input[11], 
                raw[0], raw[1], raw[2], raw[3]);
            computePositions(raw);
        } else if (status != asynTimeout) {
            asynPrint(pasynUserSelf, ASYN_TRACE_ERROR, 
                "%s:%s: unexpected error reading meter status=%d, nRead=%d, eomReason=%d\n", 
                driverName, functionName, status, nRead, eomReason);
            // We got an error reading the meter, it is probably offline.  Wait 1 second before trying again.
            unlock();
            epicsThreadSleep(1.0);
            lock();
        }
    }
}

/** Starts and stops the electrometer.
  * \param[in] value 1 to start the electrometer, 0 to stop it.
  */
asynStatus drvAH401B::setAcquire(epicsInt32 value) 
{
    size_t nread;
    size_t nwrite;
    asynStatus status=asynSuccess, readStatus;
    int eomReason;
    char dummyIn[MAX_COMMAND_LEN];
    //static const char *functionName = "setAcquire";

    if (value == 0) {
        while (1) {
            // Repeat sending ACQ OFF until we get only an ACK back
            status = pasynOctetSyncIO->setInputEos(pasynUserMeter_, "\r\n", 2);
            if (status) break;
            status = pasynOctetSyncIO->writeRead(pasynUserMeter_, "ACQ OFF", strlen("ACQ OFF"), 
                                             dummyIn, MAX_COMMAND_LEN, AH401B_TIMEOUT, &nwrite, &nread, &eomReason);
            if (status) break;
            // Flush the input buffer because there could be more characters sent after ACK
            nread = 0;
            readStatus = pasynOctetSyncIO->read(pasynUserMeter_, dummyIn, MAX_COMMAND_LEN, .1, &nread, &eomReason);
            if ((readStatus == asynTimeout) && (nread == 0)) break;
        }
        acquiring_ = 0;
    } else {
        status = pasynOctetSyncIO->writeRead(pasynUserMeter_, "ACQ ON", strlen("ACQ ON"), 
                                             dummyIn, MAX_COMMAND_LEN, AH401B_TIMEOUT, &nwrite, &nread, &eomReason);
        status = pasynOctetSyncIO->setInputEos(pasynUserMeter_, "", 0);
        // Notify the read thread if acquisition status has started
        epicsEventSignal(readDataEvent_);
        acquiring_ = 1;
    }
    if (status) {
        acquiring_ = 0;
    }
    return status;
}

 
/** Sets the ping-pong setting. 
  * \param[in] value 0: use both ping and pong (HLF OFF), value=1: use just ping (HLF ON) 
  */
asynStatus drvAH401B::setPingPong(epicsInt32 value) 
{
    asynStatus status;
    
    epicsSnprintf(outString_, sizeof(outString_), "HLF %s", value ? "OFF" : "ON");
    status = sendCommand();
    return status;
}

/** Sets the integration time. 
  * \param[in] value The integration time in seconds [0.001 - 1.000]
  */
asynStatus drvAH401B::setIntegrationTime(epicsFloat64 value) 
{
    asynStatus status;
    
    /* Make sure the update time is valid. If not change it and put back in parameter library */
    if (value < MIN_INTEGRATION_TIME) {
        value = MIN_INTEGRATION_TIME;
        setDoubleParam(P_IntegrationTime, value);
    }
    epicsSnprintf(outString_, sizeof(outString_), "ITM %d", (int)(value * 10000));
    status = sendCommand();
    return status;
}

/** Sets the range 
  * \param[in] value The desired range.
  */
asynStatus drvAH401B::setRange(epicsInt32 value) 
{
    asynStatus status;
    
    epicsSnprintf(outString_, sizeof(outString_), "RNG %d", value);
    status = sendCommand();
    return status;
}

/** Downloads all of the current EPICS settings to the electrometer.  
  * Typically used after the electrometer is power-cycled.
  */
asynStatus drvAH401B::setReset() 
{
    epicsInt32 iValue;
    epicsFloat64 dValue;

    getIntegerParam(P_PingPong, &iValue);
    setPingPong(iValue);

    getDoubleParam(P_IntegrationTime, &dValue);
    setIntegrationTime(dValue);

    getIntegerParam(P_Range, &iValue);
    setRange(iValue);

    getIntegerParam(P_Trigger, &iValue);
    setTrigger(iValue);
    
    setBinaryMode();

    getSettings();
    
    getIntegerParam(P_Acquire, &iValue);
    setAcquire(iValue);

    return asynSuccess;
}

/** Put the electrometer in binary mode, which is the only mode this driver uses.
  */
asynStatus drvAH401B::setBinaryMode()
{
    strcpy(outString_, "BIN ON");
    writeReadMeter();
    return asynSuccess;
}

/** Sets the trigger mode.
  * \param[in] value 0=Internal trigger, 1=External trigger.
  */
asynStatus drvAH401B::setTrigger(epicsInt32 value) 
{
    asynStatus status;
    
    epicsSnprintf(outString_, sizeof(outString_), "TRG %s", value ? "ON" : "OFF");
    status = sendCommand();
    return status;
}

/** Reads all the settings back from the electrometer.
  */
asynStatus drvAH401B::getSettings() 
{
    // Reads the values of all the meter parameters, sets them in the parameter library
    int pingPong, trigger, range;
    double integrationTime;
    double sampleTime;
    int prevAcquiring;
    static const char *functionName = "getStatus";
    
    prevAcquiring = acquiring_;
    if (prevAcquiring) setAcquire(0);
    strcpy(outString_, "TRG ?");
    writeReadMeter();
    if (strcmp("TRG ON", inString_) == 0) trigger = 1;
    else if (strcmp("TRG OFF", inString_) == 0) trigger = 0;
    else goto error;
    setIntegerParam(P_Trigger, trigger);
    
    strcpy(outString_, "HLF ?");
    writeReadMeter();
    if (strcmp("HLF ON", inString_) == 0) pingPong = 0;
    else if (strcmp("HLF OFF", inString_) == 0) pingPong = 1;
    else goto error;
    setIntegerParam(P_PingPong, pingPong);
    
    strcpy(outString_, "RNG ?");
    writeReadMeter();
    if (sscanf(inString_, "RNG %d", &range) != 1) goto error;
    setIntegerParam(P_Range, range);
    
    strcpy(outString_, "ITM ?");
    writeReadMeter();
    if (sscanf(inString_, "ITM %lf", &integrationTime) != 1) goto error;
    integrationTime = integrationTime/10000.;
    setDoubleParam(P_IntegrationTime, integrationTime);
    sampleTime = pingPong ? integrationTime : integrationTime*2.;
    setDoubleParam(P_SampleTime, sampleTime);
    
    if (prevAcquiring) setAcquire(1);
    
    return asynSuccess;

    error:
    asynPrint(pasynUserSelf, ASYN_TRACE_ERROR,
        "%s:%s: error, outString=%s, inString=%s\n",
        driverName, functionName, outString_, inString_);
    return asynError;
}


/** Report  parameters 
  * \param[in] fp The file pointer to write to
  * \param[in] details The level of detail requested
  */
void drvAH401B::report(FILE *fp, int details)
{
    fprintf(fp, "%s: port=%s, IP port=%s, firmware version=%s\n",
            driverName, portName, QEPortName_, firmwareVersion_);
    drvQuadEM::report(fp, details);
}


/* Configuration routine.  Called directly, or from the iocsh function below */

extern "C" {

/** EPICS iocsh callable function to call constructor for the drvAH401B class.
  * \param[in] portName The name of the asyn port driver to be created.
  * \param[in] QEPortName The name of the asyn communication port to the AH401B 
  *            created with drvAsynIPPortConfigure or drvAsynSerialPortConfigure */
int drvAH401BConfigure(const char *portName, const char *QEPortName)
{
    new drvAH401B(portName, QEPortName);
    return(asynSuccess);
}


/* EPICS iocsh shell commands */

static const iocshArg initArg0 = { "portName",iocshArgString};
static const iocshArg initArg1 = { "QEPortName",iocshArgString};
static const iocshArg * const initArgs[] = {&initArg0,
                                            &initArg1};
static const iocshFuncDef initFuncDef = {"drvAH401BConfigure",2,initArgs};
static void initCallFunc(const iocshArgBuf *args)
{
    drvAH401BConfigure(args[0].sval, args[1].sval);
}

void drvAH401BRegister(void)
{
    iocshRegister(&initFuncDef,initCallFunc);
}

epicsExportRegistrar(drvAH401BRegister);

}

