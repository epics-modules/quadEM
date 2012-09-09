/*
 * drvAHxxx.h
 * 
 * Asyn driver that inherits from the asynPortDriver class to control the Elettra/CAENEls 4-channel picoammeters
 * This driver supports models 401B, 501, 501C, and 501D, but only the 401B and 501 have been tested.
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

#include "drvAHxxx.h"
#include <epicsExport.h>

#define AHxxx_TIMEOUT 1.0
#define MIN_INTEGRATION_TIME 0.001
#define MAX_INTEGRATION_TIME 1.0

static const char *driverName="drvAHxxx";
static void readThread(void *drvPvt);


/** Constructor for the drvAHxxx class.
  * Calls the constructor for the drvQuadEM base class.
  * \param[in] portName The name of the asyn port driver to be created.
  * \param[in] QEPortName The name of the asyn communication port to the AHxxx 
  *            created with drvAsynIPPortConfigure or drvAsynSerialPortConfigure */
drvAHxxx::drvAHxxx(const char *portName, const char *QEPortName) 
   : drvQuadEM(portName, 0)
  
{
    asynStatus status;
    const char *functionName = "drvAHxxx";
    
    QEPortName_ = epicsStrDup(QEPortName);
    
    readDataEvent_ = epicsEventCreate(epicsEventEmpty);
    
    // Connect to the server
    status = pasynOctetSyncIO->connect(QEPortName, 0, &pasynUserMeter_, NULL);
    if (status) {
        printf("%s:%s: error calling pasynOctetSyncIO->connect, status=%d, error=%s\n", 
               driverName, functionName, status, pasynUserMeter_->errorMessage);
        return;
    }
    
    acquiring_ = 0;

    // Do everything that needs to be done when connecting to the meter initially.
    // Note that the meter could be offline when the IOC starts, so we put this in
    // the reset() function which can be done later when the meter is online.
    reset();

    /* Create the thread that reads the meter */
    status = (asynStatus)(epicsThreadCreate("drvAHxxxTask",
                          epicsThreadPriorityMedium,
                          epicsThreadGetStackSize(epicsThreadStackMedium),
                          (EPICSTHREADFUNC)::readThread,
                          this) == NULL);
    if (status) {
        printf("%s:%s: epicsThreadCreate failure, status=%d\n", driverName, functionName, status);
        return;
    }
    callParamCallbacks();
}



static void readThread(void *drvPvt)
{
    drvAHxxx *pPvt = (drvAHxxx *)drvPvt;
    
    pPvt->readThread();
}

/** Sends a command to the AHxxx and reads the response.
  * If meter is acquiring turns it off and waits for it to stop sending data 
  * \param[in] expectACK True if the meter should respond with ACK to this command 
  */ 
asynStatus drvAHxxx::sendCommand()
{
  int prevAcquiring;
  asynStatus status;
  const char *functionName="sendCommand";
  
  prevAcquiring = acquiring_;
  if (prevAcquiring) setAcquire(0);
  status = writeReadMeter();
  if (status) goto error;
  if (strcmp(inString_, "ACK") != 0) {
      asynPrint(pasynUserSelf, ASYN_TRACE_ERROR, 
          "%s:%s: error, outString=%s expected ACK, received %s\n",
          driverName, functionName, outString_, inString_);
      status = asynError;
      goto error;
  }
  if (prevAcquiring) setAcquire(1);
  error:
  return status;
}


/** Writes a string to the AHxxx and reads the response. */
asynStatus drvAHxxx::writeReadMeter()
{
  size_t nread;
  size_t nwrite;
  asynStatus status;
  int eomReason;
  //const char *functionName="writeReadMeter";
  
  status = pasynOctetSyncIO->writeRead(pasynUserMeter_, outString_, strlen(outString_), 
                                       inString_, sizeof(inString_), AHxxx_TIMEOUT, &nwrite, &nread, &eomReason);                                      
  return status;
}

/** Read thread to read the data from the electrometer when it is in continuous acquire mode.
  * Reads the data, computes the sums and positions, and does callbacks.
  */
void drvAHxxx::readThread(void)
{
    int acquire;
    asynStatus status;
    int i, j;
    size_t nRead;
    size_t nRequested;
    int eomReason;
    epicsInt32 raw[QE_MAX_INPUTS];
    char input[QE_MAX_INPUTS * 3];
    int resolution, numChannels;
    static const char *functionName = "readThread";
    
    /* Loop forever */
    lock();
    while (1) {
        
        //  We check both P_Acquire and acquiring_. P_Acquire means that EPICS "wants" to acquire, acquiring_ means
        // that we are actually acquiring, which could be false if the device is unreachable.
        if (!acquire || !acquiring_) {
            unlock();
            epicsEventWait(readDataEvent_);
            lock();
        }
        getIntegerParam(P_Acquire, &acquire);
        getIntegerParam(P_Resolution, &resolution);
        getIntegerParam(P_NumChannels, &numChannels);
        if (resolution == 16) nRequested = numChannels * 2;
        else nRequested = numChannels * 3;
        unlock();
        status = pasynOctetSyncIO->read(pasynUserMeter_, (char *)input, nRequested, 
                                        AHxxx_TIMEOUT, &nRead, &eomReason);
        lock();
        if ((status == asynSuccess) && (nRead == nRequested) && (eomReason == ASYN_EOM_CNT)) {
            if (resolution == 16) {
                for (i=0; i<numChannels; i++) {
                    j = i*2;
                    raw[i] = (input[j]<<8) + input[j+1];
                    raw[i] = -raw[i];
                }
            }
            else {
                for (i=0; i<numChannels; i++) {
                    j = i*3;
                    raw[i] = (input[j]<<16) + (input[j+1]<<8) + input[j+2];
                    raw[i] = -raw[i];
                }
            }
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

asynStatus drvAHxxx::reset()
{
    asynStatus status;

    setAcquire(0);    
    model_ = QE_ModelUnknown;
    strcpy(firmwareVersion_, "Unknown");
    setIntegerParam(P_Model, model_);
    strcpy(outString_, "VER ?");
    status = writeReadMeter();
    if (status) return status;
    strcpy(firmwareVersion_, inString_);
    if (strstr(firmwareVersion_, "401B") != 0) model_=QE_ModelAH401B;
    else if (strstr(firmwareVersion_, "501 ") != 0) model_=QE_ModelAH501;
    else if (strstr(firmwareVersion_, "501C") != 0) model_=QE_ModelAH501C;
    else if (strstr(firmwareVersion_, "501D") != 0) model_=QE_ModelAH501D;
    setIntegerParam(P_Model, model_);
    // Call the base class method
    status = drvQuadEM::reset();
    return status;
}

    

/** Starts and stops the electrometer.
  * \param[in] value 1 to start the electrometer, 0 to stop it.
  */
asynStatus drvAHxxx::setAcquire(epicsInt32 value) 
{
    size_t nread;
    size_t nwrite;
    asynStatus status=asynSuccess, readStatus;
    int eomReason;
    char dummyIn[MAX_COMMAND_LEN];
    //static const char *functionName = "setAcquire";
    
    if (value == 0) {
        while (1) {
            // Send stop command for both types of meters since initially we don't know which type we are talking to
            status = pasynOctetSyncIO->setInputEos(pasynUserMeter_, "\r\n", 2);
            if (status) break;
            status = pasynOctetSyncIO->writeRead(pasynUserMeter_, "ACQ OFF", strlen("ACQ OFF"), 
                                             dummyIn, MAX_COMMAND_LEN, AHxxx_TIMEOUT, &nwrite, &nread, &eomReason);
            status = pasynOctetSyncIO->writeRead(pasynUserMeter_, "S", strlen("S"), 
                                             dummyIn, MAX_COMMAND_LEN, AHxxx_TIMEOUT, &nwrite, &nread, &eomReason);
            if (status) break;
            // Now do 3 reads with short timeout to flush any responses
            nread = 0;
            readStatus = pasynOctetSyncIO->flush(pasynUserMeter_);
            readStatus = pasynOctetSyncIO->read(pasynUserMeter_, dummyIn, MAX_COMMAND_LEN, .1, &nread, &eomReason);
            if ((readStatus == asynTimeout) && (nread == 0)) break;
        }
        acquiring_ = 0;
    } else {
        // Make sure the meter is in binary mode
        strcpy(outString_, "BIN ON");
        writeReadMeter();
    
        // The AH401B sends an ACK after ACQ ON, other models don't
        if (model_ == QE_ModelAH401B) {
            status = pasynOctetSyncIO->writeRead(pasynUserMeter_, "ACQ ON", strlen("ACQ ON"), 
                        dummyIn, MAX_COMMAND_LEN, AHxxx_TIMEOUT, &nwrite, &nread, &eomReason);
        }
        else {
            status = pasynOctetSyncIO->write(pasynUserMeter_, "ACQ ON", strlen("ACQ ON"), 
                        AHxxx_TIMEOUT, &nwrite);
        }
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
asynStatus drvAHxxx::setPingPong(epicsInt32 value) 
{
    asynStatus status;
    
    if (model_ != QE_ModelAH401B) return asynSuccess;
    epicsSnprintf(outString_, sizeof(outString_), "HLF %s", value ? "OFF" : "ON");
    status = sendCommand();
    return status;
}

/** Sets the integration time. 
  * \param[in] value The integration time in seconds [0.001 - 1.000]
  */
asynStatus drvAHxxx::setIntegrationTime(epicsFloat64 value) 
{
    asynStatus status;
    
    if (model_ != QE_ModelAH401B) return asynSuccess;
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
asynStatus drvAHxxx::setRange(epicsInt32 value) 
{
    asynStatus status;
    
    epicsSnprintf(outString_, sizeof(outString_), "RNG %d", value);
    status = sendCommand();
    return status;
}

/** Sets the trigger mode.
  * \param[in] value 0=Internal trigger, 1=External trigger.
  */
asynStatus drvAHxxx::setTrigger(epicsInt32 value) 
{
    asynStatus status;
    
    epicsSnprintf(outString_, sizeof(outString_), "TRG %s", value ? "ON" : "OFF");
    status = sendCommand();
    return status;
}

/** Sets the number of channels.
  * \param[in] value Number of channels to measure (1, 2, or 4).
  */
asynStatus drvAHxxx::setNumChannels(epicsInt32 value) 
{
    asynStatus status;
    
    if (model_ == QE_ModelAH401B) return asynSuccess;
    epicsSnprintf(outString_, sizeof(outString_), "CHN %d", value);
    status = sendCommand();
    return status;
}

/** Sets the bias state.
  * \param[in] value Bias state.
  */
asynStatus drvAHxxx::setBiasState(epicsInt32 value) 
{
    asynStatus status;
    
    if ((model_ == QE_ModelAH401B) || (model_ == QE_ModelAH501)) return asynSuccess;
    epicsSnprintf(outString_, sizeof(outString_), "HVS %s", value ? "ON" : "OFF");
    status = sendCommand();
    return status;
}

/** Sets the bias voltage.
  * \param[in] value Bias voltage in volts.
  */
asynStatus drvAHxxx::setBiasVoltage(epicsFloat64 value) 
{
    asynStatus status;
    
    if ((model_ == QE_ModelAH401B) || (model_ == QE_ModelAH501)) return asynSuccess;
    epicsSnprintf(outString_, sizeof(outString_), "HVS %f", value);
    status = sendCommand();
    return status;
}

/** Sets the resolution.
  * \param[in] value Resolution in bits (16 or 24).
  */
asynStatus drvAHxxx::setResolution(epicsInt32 value) 
{
    asynStatus status;
    
    if (model_ == QE_ModelAH401B) return asynSuccess;
    epicsSnprintf(outString_, sizeof(outString_), "RES %d", value);
    status = sendCommand();
    return status;
}

/** Reads all the settings back from the electrometer.
  */
asynStatus drvAHxxx::getSettings() 
{
    // Reads the values of all the meter parameters, sets them in the parameter library
    int trigger, range, resolution, pingPong, numChannels, biasState;
    int skipReadings;
    double integrationTime, biasVoltage;
    int prevAcquiring;
    double sampleTime=0.;
    static const char *functionName = "getStatus";
    
    prevAcquiring = acquiring_;
    if (prevAcquiring) setAcquire(0);
    strcpy(outString_, "TRG ?");
    writeReadMeter();
    if (strcmp("TRG ON", inString_) == 0) trigger = 1;
    else if (strcmp("TRG OFF", inString_) == 0) trigger = 0;
    else goto error;
    setIntegerParam(P_Trigger, trigger);
    
    strcpy(outString_, "RNG ?");
    writeReadMeter();
    if (sscanf(inString_, "RNG %d", &range) != 1) goto error;
    setIntegerParam(P_Range, range);
    
    if (model_ == QE_ModelAH401B) {
        strcpy(outString_, "HLF ?");
        writeReadMeter();
        if (strcmp("HLF ON", inString_) == 0) pingPong = 0;
        else if (strcmp("HLF OFF", inString_) == 0) pingPong = 1;
        else goto error;
        setIntegerParam(P_PingPong, pingPong);

        strcpy(outString_, "ITM ?");
        writeReadMeter();
        if (sscanf(inString_, "ITM %lf", &integrationTime) != 1) goto error;
        integrationTime = integrationTime/10000.;
        setDoubleParam(P_IntegrationTime, integrationTime);
        sampleTime = pingPong ? integrationTime : integrationTime*2.;
    }
    if ((model_ == QE_ModelAH501) || (model_ == QE_ModelAH501C) || (model_ == QE_ModelAH501D)) {
        strcpy(outString_, "CHN ?");
        writeReadMeter();
        if (sscanf(inString_, "CHN %d", &numChannels) != 1) goto error;
        setIntegerParam(P_NumChannels, numChannels);

        strcpy(outString_, "RES ?");
        writeReadMeter();
        if (sscanf(inString_, "RES %d", &resolution) != 1) goto error;
        setIntegerParam(P_Resolution, resolution);

        // Compute the sample time.  This is a function of the resolution and number of channels
        sampleTime = 38.4e-6 * numChannels;
        if (resolution == 24) sampleTime = sampleTime * 2.;
    }
    if ((model_ == QE_ModelAH501C) || (model_ == QE_ModelAH501D)) {
        strcpy(outString_, "HVS ?");
        writeReadMeter();
        if (strcmp("HVS OFF", inString_) == 0) {
            biasState = 0;
        }
        else {
            biasState = 1;
            if (sscanf(inString_, "HVS %lf", &biasVoltage) != 1) goto error;
        }
        setIntegerParam(P_BiasState, biasState);
        if (biasState) setDoubleParam(P_BiasVoltage, biasVoltage);
    }
    
    // The sample times computed above don't include skipReadings
    getIntegerParam(P_SkipReadings, &skipReadings);
    sampleTime = sampleTime * (skipReadings + 1);
    setDoubleParam(P_SampleTime, sampleTime);
    if (prevAcquiring) setAcquire(1);
    
    return asynSuccess;

    error:
    asynPrint(pasynUserSelf, ASYN_TRACE_ERROR,
        "%s:%s: error, outString=%s, inString=%s\n",
        driverName, functionName, outString_, inString_);
    return asynError;
}

/** Exit handler.  Turns off acquire so we don't waste network bandwidth when the IOC stops */
void drvAHxxx::exitHandler()
{
    setAcquire(0);
}

/** Report  parameters 
  * \param[in] fp The file pointer to write to
  * \param[in] details The level of detail requested
  */
void drvAHxxx::report(FILE *fp, int details)
{
    fprintf(fp, "%s: port=%s, IP port=%s, firmware version=%s\n",
            driverName, portName, QEPortName_, firmwareVersion_);
    drvQuadEM::report(fp, details);
}


/* Configuration routine.  Called directly, or from the iocsh function below */

extern "C" {

/** EPICS iocsh callable function to call constructor for the drvAHxxx class.
  * \param[in] portName The name of the asyn port driver to be created.
  * \param[in] QEPortName The name of the asyn communication port to the AHxxx 
  *            created with drvAsynIPPortConfigure or drvAsynSerialPortConfigure */
int drvAHxxxConfigure(const char *portName, const char *QEPortName)
{
    new drvAHxxx(portName, QEPortName);
    return(asynSuccess);
}


/* EPICS iocsh shell commands */

static const iocshArg initArg0 = { "portName",iocshArgString};
static const iocshArg initArg1 = { "QEPortName",iocshArgString};
static const iocshArg * const initArgs[] = {&initArg0,
                                            &initArg1};
static const iocshFuncDef initFuncDef = {"drvAHxxxConfigure",2,initArgs};
static void initCallFunc(const iocshArgBuf *args)
{
    drvAHxxxConfigure(args[0].sval, args[1].sval);
}

void drvAHxxxRegister(void)
{
    iocshRegister(&initFuncDef,initCallFunc);
}

epicsExportRegistrar(drvAHxxxRegister);

}

