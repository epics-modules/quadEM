/*
 * drvTetrAMM.cpp
 * 
 * Asyn driver that inherits from the drvQuadEM class to control the CaenEls TetrAMM 4-channel picoammeter
 *
 * Author: Mark Rivers
 *
 * Created July 14, 2015
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

#include <epicsExport.h>
#include "drvTetrAMM.h"

#define TetrAMM_TIMEOUT 1.0

static const char *driverName="drvTetrAMM";
static void readThread(void *drvPvt);


/** Constructor for the drvTetrAMM class.
  * Calls the constructor for the drvQuadEM base class.
  * \param[in] portName The name of the asyn port driver to be created.
  * \param[in] QEPortName The name of the asyn communication port to the TetrAMM 
  *            created with drvAsynIPPortConfigure or drvAsynSerialPortConfigure
  * \param[in] ringBufferSize The number of samples to hold in the input ring buffer.
  *            This should be large enough to hold all the samples between reads of the
  *            device, e.g. 1 ms SampleTime and 1 second read rate = 1000 samples.
  *            If 0 then default of 2048 is used.
  */
drvTetrAMM::drvTetrAMM(const char *portName, const char *QEPortName, int ringBufferSize) 
   : drvQuadEM(portName, 0, ringBufferSize)
  
{
    asynStatus status;
    const char *functionName = "drvTetrAMM";
    
    QEPortName_ = epicsStrDup(QEPortName);
    
    acquireStartEvent_ = epicsEventCreate(epicsEventEmpty);

    // Connect to the server
    status = pasynOctetSyncIO->connect(QEPortName, 0, &pasynUserMeter_, NULL);
    if (status) {
        printf("%s:%s: error calling pasynOctetSyncIO->connect, status=%d, error=%s\n", 
               driverName, functionName, status, pasynUserMeter_->errorMessage);
        return;
    }
    
    acquiring_ = 0;
    readingActive_ = 0;
    resolution_ = 24;
    setIntegerParam(P_Model, QE_ModelTetrAMM);

    // Do everything that needs to be done when connecting to the meter initially.
    // Note that the meter could be offline when the IOC starts, so we put this in
    // the reset() function which can be done later when the meter is online.
    lock();
    reset();
    unlock();

    /* Create the thread that reads the meter */
    status = (asynStatus)(epicsThreadCreate("drvTetrAMMTask",
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
    drvTetrAMM *pPvt = (drvTetrAMM *)drvPvt;
    
    pPvt->readThread();
}

/** Sends a command to the TetrAMM and reads the response.
  * If meter is acquiring turns it off and waits for it to stop sending data 
  * \param[in] expectACK True if the meter should respond with ACK to this command 
  */ 
asynStatus drvTetrAMM::sendCommand()
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


/** Writes a string to the TetrAMM and reads the response. */
asynStatus drvTetrAMM::writeReadMeter()
{
  size_t nread;
  size_t nwrite;
  asynStatus status;
  int eomReason;
  //const char *functionName="writeReadMeter";
  
  status = pasynOctetSyncIO->writeRead(pasynUserMeter_, outString_, strlen(outString_), 
                                       inString_, sizeof(inString_), TetrAMM_TIMEOUT, &nwrite, &nread, &eomReason);                                      
  return status;
}

/** Read thread to read the data from the electrometer when it is in continuous acquire mode.
  * Reads the data, computes the sums and positions, and does callbacks.
  */

inline void swapDouble(char *in)
{
    char temp;
    temp = in[0]; in[0] = in[7]; in[7] = temp;
    temp = in[1]; in[1] = in[6]; in[6] = temp;
    temp = in[2]; in[2] = in[5]; in[5] = temp;
    temp = in[3]; in[3] = in[4]; in[4] = temp;
}

void drvTetrAMM::readThread(void)
{
    asynStatus status;
    size_t nRead;
    int bytesPerValue=8;
    int eomReason;
    int acquireMode;
    int numAverage;
    asynUser *pasynUser;
    asynInterface *pasynInterface;
    asynOctet *pasynOctet;
    void *octetPvt;
    epicsFloat64 data[5];
    int64_t ltemp;
    size_t nRequested;
    bool littleEndian = true;
    static const char *functionName = "readThread";

    /* Create an asynUser */
    pasynUser = pasynManager->createAsynUser(0, 0);
    pasynUser->timeout = TetrAMM_TIMEOUT;
    status = pasynManager->connectDevice(pasynUser, QEPortName_, 0);
    if(status!=asynSuccess) {
        asynPrint(pasynUserSelf, ASYN_TRACE_ERROR,
            "%s:%s: connectDevice failed, status=%d\n",
            driverName, functionName, status);
    }
    pasynInterface = pasynManager->findInterface(pasynUser, asynOctetType, 1);
    if(!pasynInterface) {;
        asynPrint(pasynUserSelf, ASYN_TRACE_ERROR,
            "%s:%s: findInterface failed for asynOctet, status=%d\n",
            driverName, functionName, status);
    }
    pasynOctet = (asynOctet *)pasynInterface->pinterface;
    octetPvt = pasynInterface->drvPvt;
    
    /* Loop forever */
    lock();
    while (1) {
        if (acquiring_ == 0) {
            readingActive_ = 0;
            unlock();
            epicsEventWait(acquireStartEvent_);
            lock();
            readingActive_ = 1;
            numAcquired_ = 0;
            getIntegerParam(P_AcquireMode, &acquireMode);
            getIntegerParam(P_NumAverage, &numAverage);
        }
        nRequested = (numChannels_ + 1) * bytesPerValue;
        unlock();
        pasynManager->lockPort(pasynUser);
        status = pasynOctet->read(octetPvt, pasynUser, (char*)data, nRequested, 
                                  &nRead, &eomReason);
        pasynManager->unlockPort(pasynUser);
        lock();

        if ((status == asynSuccess) && (nRead == nRequested) && (eomReason == ASYN_EOM_CNT)) {
            // If we are on a little-endian machine we need to swap the byte order
            if (littleEndian) {
                for (int i=0; i<=numChannels_; i++) swapDouble((char *)&data[i]);
            }
            if (isnan(data[numChannels_])) {
                asynPrint(pasynUserSelf, ASYN_TRACEIO_DRIVER,
                    "%s::%s data=%e %e %e %e\n", 
                    driverName, functionName, data[0], data[1], data[2], data[3]);
                computePositions(data);
                numAcquired_++;
                if ((acquireMode == QEAcquireModeOneShot) &&
                    (numAcquired_ >= numAverage)) {
                    acquiring_ = 0;
                }
            } else {
                memcpy(&ltemp, &data[numChannels_], 8);
                asynPrint(pasynUserSelf, ASYN_TRACE_ERROR, 
                    "%s:%s: did not get S_NAN when expected, got=0x%lx\n", 
                    driverName, functionName, ltemp);
            }
        } else if (status != asynTimeout) {
            asynPrint(pasynUserSelf, ASYN_TRACE_ERROR, 
                "%s:%s: unexpected error reading meter status=%d, nRead=%lu, eomReason=%d\n", 
                driverName, functionName, status, (unsigned long)nRead, eomReason);
            // We got an error reading the meter, it is probably offline.  Wait 1 second before trying again.
            unlock();
            epicsThreadSleep(1.0);
            lock();
        }
    }
}

asynStatus drvTetrAMM::reset()
{
    asynStatus status;
    //static const char *functionName = "reset";

    setAcquire(0);    
    strcpy(firmwareVersion_, "Unknown");
    strcpy(outString_, "VER:?");
    status = writeReadMeter();
    strcpy(firmwareVersion_, &inString_[4]);
    setStringParam(P_Firmware, firmwareVersion_);
    // Call the base class method
    status = drvQuadEM::reset();
    return status;
}

    

/** Starts and stops the electrometer.
  * \param[in] value 1 to start the electrometer, 0 to stop it.
  */
asynStatus drvTetrAMM::setAcquire(epicsInt32 value) 
{
    size_t nread;
    size_t nwrite;
    asynStatus status=asynSuccess, readStatus;
    int eomReason;
    int triggerMode;
    int numAverage;
    int numAcquire;
    int acquireMode;
    char dummyIn[MAX_COMMAND_LEN];
    static const char *functionName = "setAcquire";
    
    getIntegerParam(P_TriggerMode, &triggerMode);
    getIntegerParam(P_AcquireMode, &acquireMode);
    getIntegerParam(P_NumAverage, &numAverage);

    // Make sure the input EOS is set
    status = pasynOctetSyncIO->setInputEos(pasynUserMeter_, "\r\n", 2);
    if (status) {
        asynPrint(pasynUserSelf, ASYN_TRACE_ERROR,
            "%s:%s: error calling pasynOctetSyncIO->setInputEos, status=%d\n",
            driverName, functionName, status);
        return asynError;
    }

    if (value == 0) {
        // We assume that if acquiring_=0, readingActive_=0 and acquireMode=one shot that the meter stopped itself
        // and we don't need to do anything further.  This really speeds things up.
        if ((acquiring_ == 0) && (readingActive_ == 0) && (acquireMode == QEAcquireModeOneShot)) 
            return asynSuccess;

        // Setting this flag tells the read thread to stop
        acquiring_ = 0;
        // Wait for the read thread to stop
        while (readingActive_) {
            unlock();
            epicsThreadSleep(0.1);
            lock();
        }
        while (1) {
            status = pasynOctetSyncIO->writeRead(pasynUserMeter_, "ACQ:OFF", strlen("ACQ:OFF"), 
                                             dummyIn, MAX_COMMAND_LEN, TetrAMM_TIMEOUT, &nwrite, &nread, &eomReason);
            // Also send TRG OFF because we don't know what mode the meter might be in when we start
            status = pasynOctetSyncIO->writeRead(pasynUserMeter_, "TRG:OFF", strlen("TRG:OFF"), 
                                             dummyIn, MAX_COMMAND_LEN, TetrAMM_TIMEOUT, &nwrite, &nread, &eomReason);
            // Also send GATE OFF because we don't know what mode the meter might be in when we start
            status = pasynOctetSyncIO->writeRead(pasynUserMeter_, "TRG:OFF", strlen("GATE:OFF"), 
                                             dummyIn, MAX_COMMAND_LEN, TetrAMM_TIMEOUT, &nwrite, &nread, &eomReason);
            if (status) {
                asynPrint(pasynUserSelf, ASYN_TRACE_ERROR,
                    "%s:%s: error calling pasynOctetSyncIO->writeRead, status=%d\n",
                    driverName, functionName, status);
                break;
            }
            // Now do flush and read with short timeout to flush any responses
            nread = 0;
            readStatus = pasynOctetSyncIO->flush(pasynUserMeter_);
            readStatus = pasynOctetSyncIO->read(pasynUserMeter_, dummyIn, MAX_COMMAND_LEN, .5, &nread, &eomReason);
            if ((readStatus == asynTimeout) && (nread == 0)) break;
        }
    } else {
        // Make sure the meter is in binary mode
        strcpy(outString_, "ASCII:OFF");
        writeReadMeter();
        
        // If we are in one-shot mode then send NAQ to request specific number of samples
        if (acquireMode == QEAcquireModeOneShot) {
            numAcquire = numAverage;
        } else {
            numAcquire = 0;
        }
        sprintf(outString_, "NAQ:%d", numAcquire);
        writeReadMeter();

        // If we are in external trigger mode then send the TRG ON command
        switch (triggerMode) {
            case QETriggerModeInternal:    
                status = pasynOctetSyncIO->write(pasynUserMeter_, "ACQ:ON", strlen("ACQ:ON"), 
                            TetrAMM_TIMEOUT, &nwrite);
            break;
            case QETriggerModeExtTrigger:    
                status = pasynOctetSyncIO->writeRead(pasynUserMeter_, "TRG:ON", strlen("TRG:ON"), 
                            dummyIn, MAX_COMMAND_LEN, TetrAMM_TIMEOUT, &nwrite, &nread, &eomReason);
            break;
            case QETriggerModeExtGate:    
                status = pasynOctetSyncIO->writeRead(pasynUserMeter_, "GATE:ON", strlen("GATE:ON"), 
                            dummyIn, MAX_COMMAND_LEN, TetrAMM_TIMEOUT, &nwrite, &nread, &eomReason);
            break;
        }
        status = pasynOctetSyncIO->setInputEos(pasynUserMeter_, "", 0);
        // Notify the read thread if acquisition status has started
        epicsEventSignal(acquireStartEvent_);
        acquiring_ = 1;
    }
    if (status) {
        acquiring_ = 0;
    }
    return status;
}

/** Sets the range 
  * \param[in] value The desired range.
  */
asynStatus drvTetrAMM::setRange(epicsInt32 value) 
{
    asynStatus status;
    
    epicsSnprintf(outString_, sizeof(outString_), "RNG:%d", value);
    status = sendCommand();
    return status;
}

/** Sets the number of channels.
  * \param[in] value Number of channels to measure (1, 2, or 4).
  */
asynStatus drvTetrAMM::setNumChannels(epicsInt32 value) 
{
    asynStatus status;
    
    epicsSnprintf(outString_, sizeof(outString_), "CHN:%d", value);
    status = sendCommand();
    return status;
}

/** Sets the values per read.
  * \param[in] value Values per read. Minimum depends on number of channels.
  */
asynStatus drvTetrAMM::setValuesPerRead(epicsInt32 value) 
{
    asynStatus status;
    
    epicsSnprintf(outString_, sizeof(outString_), "NRSAMP:%d", value);
    status = sendCommand();
    return status;
}

/** Sets the bias state.
  * \param[in] value Bias state.
  */
asynStatus drvTetrAMM::setBiasState(epicsInt32 value) 
{
    asynStatus status;
    
    epicsSnprintf(outString_, sizeof(outString_), "HVS:%s", value ? "ON" : "OFF");
    status = sendCommand();
    return status;
}

/** Sets the bias voltage.
  * \param[in] value Bias voltage in volts.
  */
asynStatus drvTetrAMM::setBiasVoltage(epicsFloat64 value) 
{
    asynStatus status;
    
    epicsSnprintf(outString_, sizeof(outString_), "HVS:%f", value);
    status = sendCommand();
    return status;
}

/** Sets the bias interlock.
  * \param[in] value Bias interlock off (0) or on (1).
  */
asynStatus drvTetrAMM::setBiasInterlock(epicsInt32 value) 
{
    asynStatus status;
    
    epicsSnprintf(outString_, sizeof(outString_), "INTERLOCK:%s",  value ? "ON" : "OFF");
    status = sendCommand();
    return status;
}

/** Reads all the settings back from the electrometer.
  */
asynStatus drvTetrAMM::getSettings() 
{
    // Reads the values of all the meter parameters, sets them in the parameter library
    int range, numChannels, numAverage, valuesPerRead;
    double biasVoltage, voltageReadback, currentReadback, temperature;
    int prevAcquiring;
    double sampleTime=0., averagingTime;
    static const char *functionName = "getStatus";
    
    prevAcquiring = acquiring_;
    if (prevAcquiring) setAcquire(0);

    strcpy(outString_, "RNG:?");
    writeReadMeter();
    // Note: the TetrAMM can return 4 ranges, but we only support a single range so we
    // only parse a single character in the response
    if (sscanf(inString_, "RNG:%1d", &range) != 1) goto error;
    setIntegerParam(P_Range, range);
    
    strcpy(outString_, "CHN:?");
    writeReadMeter();
    if (sscanf(inString_, "CHN:%d", &numChannels) != 1) goto error;
    setIntegerParam(P_NumChannels, numChannels);
    numChannels_ = numChannels;

    strcpy(outString_, "NRSAMP:?");
    writeReadMeter();
    if (sscanf(inString_, "NRSAMP:%d", &valuesPerRead) != 1) goto error;
    setIntegerParam(P_ValuesPerRead, valuesPerRead);

    // Compute the sample time.  This is 10 microseconds times valuesPerRead. 
    sampleTime = 10e-6 * valuesPerRead;
    setDoubleParam(P_SampleTime, sampleTime);

    strcpy(outString_, "HVS:?");
    writeReadMeter();
    if (strncmp(inString_, "HVS:OFF", sizeof(inString_)) == 0) {
        setIntegerParam(P_BiasState, 0);
    } else {
        setIntegerParam(P_BiasState, 1);
        if (sscanf(inString_, "HVS:%lf", &biasVoltage) != 1) goto error;
        setDoubleParam(P_BiasVoltage, biasVoltage);
    }

    strcpy(outString_, "HVV:?");
    writeReadMeter();
    if (sscanf(inString_, "HVV:%lf", &voltageReadback) != 1) goto error;
    setDoubleParam(P_HVVReadback, voltageReadback);

    strcpy(outString_, "HVI:?");
    writeReadMeter();
    if (sscanf(inString_, "HVI:%lf", &currentReadback) != 1) goto error;
    setDoubleParam(P_HVIReadback, currentReadback);

    strcpy(outString_, "TEMP:?");
    writeReadMeter();
    if (sscanf(inString_, "TEMP:%lf", &temperature) != 1) goto error;
    setDoubleParam(P_Temperature, temperature);

    // Compute the number of values that will be accumulated in the ring buffer before averaging
    getDoubleParam(P_AveragingTime, &averagingTime);
    numAverage = (int)((averagingTime / sampleTime) + 0.5);
    setIntegerParam(P_NumAverage, numAverage);

    if (prevAcquiring) setAcquire(1);
    
    return asynSuccess;

    error:
    asynPrint(pasynUserSelf, ASYN_TRACE_ERROR,
        "%s:%s: error, outString=%s, inString=%s\n",
        driverName, functionName, outString_, inString_);
    return asynError;
}

/** Exit handler.  Turns off acquire so we don't waste network bandwidth when the IOC stops */
void drvTetrAMM::exitHandler()
{
    lock();
    setAcquire(0);
    unlock();
}

/** Report  parameters 
  * \param[in] fp The file pointer to write to
  * \param[in] details The level of detail requested
  */
void drvTetrAMM::report(FILE *fp, int details)
{
    fprintf(fp, "%s: port=%s, IP port=%s, firmware version=%s\n",
            driverName, portName, QEPortName_, firmwareVersion_);
    drvQuadEM::report(fp, details);
}


/* Configuration routine.  Called directly, or from the iocsh function below */

extern "C" {

/** EPICS iocsh callable function to call constructor for the drvTetrAMM class.
  * \param[in] portName The name of the asyn port driver to be created.
  * \param[in] QEPortName The name of the asyn communication port to the TetrAMM 
  *            created with drvAsynIPPortConfigure or drvAsynSerialPortConfigure.
  * \param[in] ringBufferSize The number of samples to hold in the input ring buffer.
  *            This should be large enough to hold all the samples between reads of the
  *            device, e.g. 1 ms SampleTime and 1 second read rate = 1000 samples.
  *            If 0 then default of 2048 is used.
  */
int drvTetrAMMConfigure(const char *portName, const char *QEPortName, int ringBufferSize)
{
    new drvTetrAMM(portName, QEPortName, ringBufferSize);
    return(asynSuccess);
}


/* EPICS iocsh shell commands */

static const iocshArg initArg0 = { "portName",iocshArgString};
static const iocshArg initArg1 = { "QEPortName",iocshArgString};
static const iocshArg initArg2 = { "ring buffer size",iocshArgInt};
static const iocshArg * const initArgs[] = {&initArg0,
                                            &initArg1,
                                            &initArg2};
static const iocshFuncDef initFuncDef = {"drvTetrAMMConfigure",3,initArgs};
static void initCallFunc(const iocshArgBuf *args)
{
    drvTetrAMMConfigure(args[0].sval, args[1].sval, args[2].ival);
}

void drvTetrAMMRegister(void)
{
    iocshRegister(&initFuncDef,initCallFunc);
}

epicsExportRegistrar(drvTetrAMMRegister);

}

