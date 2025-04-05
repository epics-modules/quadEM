/*
 * drvQuadEM.cpp
 * 
 * Asyn driver that inherits from the asynPortDriver class to control the quad electrometers
 *
 * Author: Mark Rivers
 *
 * Created June 14, 2012
 */

#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#include <epicsTypes.h>
#include <epicsString.h>
#include <epicsThread.h>
#include <epicsExit.h>
#include <epicsRingBytes.h>
#include <epicsEvent.h>

#include <asynNDArrayDriver.h>

#include <epicsExport.h>
#include "drvQuadEM.h"

static const char *driverName="drvQuadEM";

static void exitHandlerC(void *pPvt)
{
    drvQuadEM *pdrvQuadEM = (drvQuadEM *)pPvt;
    pdrvQuadEM->exitHandler();
}

static void callbackTaskC(void *pPvt)
{
    drvQuadEM *pdrvQuadEM = (drvQuadEM *)pPvt;
    pdrvQuadEM->callbackTask();
}


/** Constructor for the drvQuadEM class.
  * Calls constructor for the asynPortDriver base class.
  * \param[in] portName The name of the asyn port driver to be created.
  * \param[in] ringBufferSize The number of samples to hold in the input ring buffer.
  *            This should be large enough to hold all the samples between reads of the
  *            device, e.g. 1 ms SampleTime and 1 second read rate = 1000 samples.
  *            If 0 then default of 2048 is used.
  */
drvQuadEM::drvQuadEM(const char *portName, int ringBufferSize) 
   : asynNDArrayDriver(portName, 
                    QE_MAX_DATA+1, /* maxAddr */ 
                    0, 0,        /* maxBuffers, maxMemory, no limits */
                    asynInt32Mask | asynInt32ArrayMask | asynFloat64Mask | asynFloat64ArrayMask | asynEnumMask | asynGenericPointerMask | asynDrvUserMask, /* Interface mask */
                    asynInt32Mask | asynInt32ArrayMask | asynFloat64Mask | asynFloat64ArrayMask | asynEnumMask | asynGenericPointerMask,                   /* Interrupt mask */
                    ASYN_CANBLOCK | ASYN_MULTIDEVICE, /* asynFlags.  This driver blocks it is multi-device */
                    1, /* Autoconnect */
                    0, /* Default priority */
                    0) /* Default stack size*/ 
{
    int i;
    int status;
    const char *functionName = "drvQuadEM";
    asynUser *pasynUser;
    
    createParam(P_AcquireModeString,        asynParamInt32,         &P_AcquireMode);
    createParam(P_CurrentOffsetString,      asynParamFloat64,       &P_CurrentOffset);
    createParam(P_CurrentScaleString,       asynParamFloat64,       &P_CurrentScale);
    createParam(P_PositionOffsetString,     asynParamFloat64,       &P_PositionOffset);
    createParam(P_PositionScaleString,      asynParamFloat64,       &P_PositionScale);
    createParam(P_GeometryString,           asynParamInt32,         &P_Geometry);
    createParam(P_DoubleDataString,         asynParamFloat64,       &P_DoubleData);
    createParam(P_IntArrayDataString,       asynParamInt32Array,    &P_IntArrayData);
    createParam(P_RingOverflowsString,      asynParamInt32,         &P_RingOverflows);
    createParam(P_ReadDataString,           asynParamInt32,         &P_ReadData);
    createParam(P_PingPongString,           asynParamInt32,         &P_PingPong);
    createParam(P_IntegrationTimeString,    asynParamFloat64,       &P_IntegrationTime);
    createParam(P_SampleTimeString,         asynParamFloat64,       &P_SampleTime);
    createParam(P_RangeString,              asynParamInt32,         &P_Range);
    createParam(P_ResetString,              asynParamInt32,         &P_Reset);
    createParam(P_TriggerModeString,        asynParamInt32,         &P_TriggerMode);
    createParam(P_TriggerPolarityString,    asynParamInt32,         &P_TriggerPolarity);
    createParam(P_NumChannelsString,        asynParamInt32,         &P_NumChannels);
    createParam(P_BiasStateString,          asynParamInt32,         &P_BiasState);
    createParam(P_BiasVoltageString,        asynParamFloat64,       &P_BiasVoltage);
    createParam(P_BiasInterlockString,      asynParamInt32,         &P_BiasInterlock);
    createParam(P_HVSReadbackString,        asynParamInt32,         &P_HVSReadback);
    createParam(P_HVVReadbackString,        asynParamFloat64,       &P_HVVReadback);
    createParam(P_HVIReadbackString,        asynParamFloat64,       &P_HVIReadback);
    createParam(P_TemperatureString,        asynParamFloat64,       &P_Temperature);
    createParam(P_ReadStatusString,         asynParamInt32,         &P_ReadStatus);
    createParam(P_ResolutionString,         asynParamInt32,         &P_Resolution);
    createParam(P_ValuesPerReadString,      asynParamInt32,         &P_ValuesPerRead);
    createParam(P_NumAcquireString,         asynParamInt32,         &P_NumAcquire);
    createParam(P_NumAcquiredString,        asynParamInt32,         &P_NumAcquired);
    createParam(P_ReadFormatString,         asynParamInt32,         &P_ReadFormat);
    createParam(P_AveragingTimeString,      asynParamFloat64,       &P_AveragingTime);
    createParam(P_NumAverageString,         asynParamInt32,         &P_NumAverage);
    createParam(P_NumAveragedString,        asynParamInt32,         &P_NumAveraged);
    createParam(P_ModelString,              asynParamInt32,         &P_Model);
    createParam(P_FirmwareString,           asynParamOctet,         &P_Firmware);
    createParam(P_RawDataString,            asynParamInt32,         &P_RawData);
    
    setIntegerParam(P_RingOverflows, 0);
    setIntegerParam(P_PingPong, 0);
    setDoubleParam(P_IntegrationTime, 0.);
    // Need to define something for the SampleTime or else drvEpidFast gets an error
    setDoubleParam(P_SampleTime, 0.1);
    setIntegerParam(P_Range, 0);
    setIntegerParam(P_TriggerMode, 0);
    setIntegerParam(P_NumChannels, 4);
    numChannels_ = 4;
    setIntegerParam(P_BiasState, 0);
    setDoubleParam(P_BiasVoltage, 0.);
    setIntegerParam(P_Resolution, 16);
    setIntegerParam(P_ValuesPerRead, 1);
    setIntegerParam(P_ReadFormat, 0);
    for (i=0; i<QE_MAX_DATA; i++) {
        setDoubleParam(i, P_DoubleData, 0.0);
    }
    valuesPerRead_ = 1;
    
    if (ringBufferSize <= 0) ringBufferSize = QE_DEFAULT_RING_BUFFER_SIZE;
    
    ringBuffer_ = epicsRingBytesCreate(ringBufferSize * QE_MAX_DATA * sizeof(epicsFloat64));
    msgQId_ = epicsMessageQueueCreate(ringBufferSize, sizeof(int));

    /* Create the thread that does callbacks when the ring buffer has numAverage samples */
    status = (asynStatus)(epicsThreadCreate("drvQuadEMCallbackTask",
                          epicsThreadPriorityMedium,
                          epicsThreadGetStackSize(epicsThreadStackMedium),
                          (EPICSTHREADFUNC)::callbackTaskC,
                          this) == NULL);
    if (status) {
        printf("%s:%s: epicsThreadCreate failure, status=%d\n", driverName, functionName, status);
        return;
    }
    
    // This driver supports QE_MAX_DATA+1 addresses = 0-11 with autoConnect=1.  But there are only records
    // connected to addresses 0-3, so addresses 4-11 never show as "connected" since nothing ever calls
    // pasynManager->queueRequest.  So we do an exceptionConnect to each address so asynManager will show
    // them as connected.  Note that this is NOT necessary for the driver to function correctly, the
    // NDPlugins will still get called even for addresses that are not "connected".  It is just to
    // avoid confusion.
    for (i=0; i<QE_MAX_DATA+1; i++) {
        pasynUser = pasynManager->createAsynUser(0,0);
        pasynManager->connectDevice(pasynUser, portName, i);
        pasynManager->exceptionConnect(pasynUser);
    }
   
    epicsAtExit(exitHandlerC, this);
}

/** This function computes the sums, diffs and positions, and does callbacks 
  * \param[in] raw Array of raw current readings 
  */
void drvQuadEM::computePositions(epicsFloat64 raw[QE_MAX_INPUTS])
{
    int i;
    int count;
    int numAverage;
    int ringOverflows;
    int geometry;
    epicsFloat64 currentOffset[QE_MAX_INPUTS];
    epicsFloat64 currentScale[QE_MAX_INPUTS];
    epicsFloat64 positionOffset[2];
    epicsFloat64 positionScale[2];
    epicsInt32 intData[QE_MAX_DATA];
    epicsFloat64 doubleData[QE_MAX_DATA];
    epicsFloat64 denom;
    static const char *functionName = "computePositions";
    
    getIntegerParam(P_Geometry, &geometry);
    // If the ring buffer is full then remove the oldest entry
    if (epicsRingBytesFreeBytes(ringBuffer_) < (int)sizeof(doubleData)) {
        asynPrint(pasynUserSelf, ASYN_TRACE_WARNING,
            "%s::%s warning ring buffer overflow\n",
            driverName, functionName);
        count = epicsRingBytesGet(ringBuffer_, (char *)&doubleData, sizeof(doubleData));
        ringCount_--;
        rawCount_--;
        getIntegerParam(P_RingOverflows, &ringOverflows);
        ringOverflows++;
        setIntegerParam(P_RingOverflows, ringOverflows);
    }
    
    for (i=0; i<QE_MAX_INPUTS; i++) {
        getDoubleParam(i, P_CurrentOffset, &currentOffset[i]);
        getDoubleParam(i, P_CurrentScale,  &currentScale[i]);
        doubleData[i] = raw[i]*currentScale[i] - currentOffset[i];
        if (i <= QECurrent4) {
            setIntegerParam(i, P_RawData, raw[i]);
        }
    }
    for (i=0; i<2; i++) {
        getDoubleParam(i, P_PositionOffset, &positionOffset[i]);
        getDoubleParam(i, P_PositionScale, &positionScale[i]);
    }
    
    doubleData[QESumAll] = doubleData[QECurrent1] + doubleData[QECurrent2] +
                           doubleData[QECurrent3] + doubleData[QECurrent4];
    if (geometry == QEGeometrySquare) {
        doubleData[QESumX]   = doubleData[QESumAll];
        doubleData[QESumY]   = doubleData[QESumAll];
        doubleData[QEDiffX]  = (doubleData[QECurrent2] + doubleData[QECurrent3]) -
                               (doubleData[QECurrent1] + doubleData[QECurrent4]);
        doubleData[QEDiffY]  = (doubleData[QECurrent1] + doubleData[QECurrent2]) -
                               (doubleData[QECurrent3] + doubleData[QECurrent4]);
    }
    else if (geometry == QEGeometrySquareCC) {
        doubleData[QESumX]   = doubleData[QESumAll];
        doubleData[QESumY]   = doubleData[QESumAll];
        doubleData[QEDiffX]  = (doubleData[QECurrent3] + doubleData[QECurrent4]) -
                               (doubleData[QECurrent1] + doubleData[QECurrent2]);
        doubleData[QEDiffY]  = (doubleData[QECurrent1] + doubleData[QECurrent4]) -
                               (doubleData[QECurrent2] + doubleData[QECurrent3]);
    }
    else {
        doubleData[QESumX]   = doubleData[QECurrent1] + doubleData[QECurrent2];
        doubleData[QESumY]   = doubleData[QECurrent3] + doubleData[QECurrent4];
        doubleData[QEDiffX]  = doubleData[QECurrent2] - doubleData[QECurrent1];
        doubleData[QEDiffY]  = doubleData[QECurrent4] - doubleData[QECurrent3];
    }
    denom = doubleData[QESumX];
    if (denom == 0.) denom = 1.;
    doubleData[QEPositionX] = (positionScale[0] * doubleData[QEDiffX] / denom) -  positionOffset[0];
    denom = doubleData[QESumY];
    if (denom == 0.) denom = 1.;
    doubleData[QEPositionY] = (positionScale[1] * doubleData[QEDiffY] / denom) -  positionOffset[1];

    count = epicsRingBytesPut(ringBuffer_, (char *)&doubleData, sizeof(doubleData));
    ringCount_++;
    rawCount_++;
    if (count != sizeof(doubleData)) {
        asynPrint(pasynUserSelf, ASYN_TRACE_ERROR,
               "%s:%s: error writing ring buffer, count=%d, should be %d\n", 
               driverName, functionName, count, (int)sizeof(doubleData));
    }

    getIntegerParam(P_NumAverage, &numAverage);
    if (numAverage > 0) {
        if (rawCount_ >= numAverage) {
            triggerCallbacks();
        }
    }

    for (i=0; i<QE_MAX_DATA; i++) {
        intData[i] = (epicsInt32)doubleData[i];
        setDoubleParam(i, P_DoubleData, doubleData[i]);
        callParamCallbacks(i);
    }
    doCallbacksInt32Array(intData, QE_MAX_DATA, P_IntArrayData, 0);
}

asynStatus drvQuadEM::triggerCallbacks()
{
    int status;
    static const char *functionName = "triggerCallbacks";

    // If rawCount_ < 1 there is nothing to do.  This can happen if users presses Read when not acquiring
    if (rawCount_ < 1) return asynSuccess;
    status = epicsMessageQueueSend(msgQId_, &rawCount_, sizeof(rawCount_));
    if (status) {
        asynPrint(pasynUserSelf, ASYN_TRACE_ERROR, 
            "%s::%s, error calling epicsMessageQueueSend\n",
            driverName, functionName);
    }
    rawCount_ = 0;
    return (status ? asynError : asynSuccess);
}

asynStatus drvQuadEM::doDataCallbacks(int numRead)
{
    epicsFloat64 *pIn, *pOut;
    int sampleSize = QE_MAX_DATA * sizeof(epicsFloat64);
    int ringSize;
    int count;
    epicsTimeStamp now;
    epicsFloat64 timeStamp;
    int arrayCounter;
    int i, j;
    size_t dims[2];
    NDArray *pArrayAll, *pArraySingle;
    static const char *functionName = "doDataCallbacks";
    
    ringSize = epicsRingBytesUsedBytes(ringBuffer_) / sampleSize;
    if (ringSize < numRead) {
        asynPrint(pasynUserSelf, ASYN_TRACE_ERROR,
            "%s::%s: not enough bytes in ring buffer, expecting %d actually %d, rawCount_=%d, ringCount_=%d\n",
            driverName, functionName, numRead, ringSize, rawCount_, ringCount_);
        return asynError;
    }

    dims[0] = QE_MAX_DATA;
    dims[1] = numRead;
    setIntegerParam(P_NumAveraged, numRead);

    epicsTimeGetCurrent(&now);
    getIntegerParam(NDArrayCounter, &arrayCounter);
    arrayCounter++;
    setIntegerParam(NDArrayCounter, arrayCounter);

    pArrayAll = pNDArrayPool->alloc(2, dims, NDFloat64, 0, 0);
    pArrayAll->uniqueId = arrayCounter;
    timeStamp = now.secPastEpoch + now.nsec / 1.e9;
    pArrayAll->timeStamp = timeStamp;
    getAttributes(pArrayAll->pAttributeList);

    count = epicsRingBytesGet(ringBuffer_, (char *)pArrayAll->pData, numRead * sampleSize);
    if (count != numRead * sampleSize) {
        asynPrint(pasynUserSelf, ASYN_TRACE_ERROR,
            "%s::%s ring read failed\n",
            driverName, functionName);
        return asynError;
    }
    ringCount_ -= numRead;
    doCallbacksGenericPointer(pArrayAll, NDArrayData, QE_MAX_DATA);
    // Copy data to arrays for each type of data, do callbacks on that.
    dims[0] = numRead;
    for (i=0; i<QE_MAX_DATA; i++) {
        pArraySingle = pNDArrayPool->alloc(1, dims, NDFloat64, 0, 0);
        pArraySingle->uniqueId = arrayCounter;
        pArraySingle->timeStamp = timeStamp;
        getAttributes(pArraySingle->pAttributeList);
        pIn = (epicsFloat64 *)pArrayAll->pData;
        pOut = (epicsFloat64 *)pArraySingle->pData;
        for (j=0; j<numRead; j++) {
            pOut[j] = pIn[i];
            pIn += QE_MAX_DATA;
        }
        doCallbacksGenericPointer(pArraySingle, NDArrayData, i);
        pArraySingle->release();
    }   
    pArrayAll->release();
    callParamCallbacks();
    setIntegerParam(P_RingOverflows, 0);
    callParamCallbacks();
    return asynSuccess;
}

void drvQuadEM::callbackTask()
{
    int numAcquire;
    int acquireMode;
    int numRead;
    int readSize = sizeof(numRead);
    int status;
    static const char *functionName = "callbackTask";
    
    lock();
    while (1) {
        unlock();
        status = epicsMessageQueueReceive(msgQId_, &numRead, readSize);
        lock();
        if (status != readSize) {
            asynPrint(pasynUserSelf, ASYN_TRACE_ERROR, 
                "%s::%s error calling epicsMessageQueueReceive expected %d bytes, actual=%d\n",
                driverName, functionName, readSize, status);
            continue;
        } 
        getIntegerParam(P_AcquireMode, &acquireMode);
        getIntegerParam(P_NumAcquire, &numAcquire);
        if (acquireMode == QEAcquireModeSingle) numAcquire = 1;

        if (acquireMode == QEAcquireModeContinuous) {
            doDataCallbacks(numRead);
            numAcquired_++;
            setIntegerParam(P_NumAcquired, numAcquired_);
        } 
        else {
            if (numAcquired_ < numAcquire) {
                doDataCallbacks(numRead);
                numAcquired_++;
                setIntegerParam(P_NumAcquired, numAcquired_);
                if (numAcquired_ == numAcquire) {
                    setAcquire(0);
                    setIntegerParam(ADAcquire, 0);
                }
            }
        }
        callParamCallbacks();
    }
}

/** Called when asyn clients call pasynInt32->write().
  * \param[in] pasynUser pasynUser structure that encodes the reason and address.
  * \param[in] value Value to write. */
asynStatus drvQuadEM::writeInt32(asynUser *pasynUser, epicsInt32 value)
{
    int function = pasynUser->reason;
    int status = asynSuccess;
    int channel;
    const char *paramName;
    const char* functionName = "writeInt32";

    getAddress(pasynUser, &channel);
    
    /* Set the parameter in the parameter library. */
    status |= setIntegerParam(channel, function, value);
    
    /* Fetch the parameter string name for possible use in debugging */
    getParamName(function, &paramName);

    if (function == ADAcquire) {
        if (value) {
            epicsRingBytesFlush(ringBuffer_);
            ringCount_ = 0;
            rawCount_ = 0;
        }
        status |= setAcquire(value);
    } 
    else if (function == P_AcquireMode) {
        if (value != QEAcquireModeContinuous) {
            status |= setAcquire(0);
            setIntegerParam(ADAcquire, 0);
        } 
        status |= setAcquireMode(value);
        status |= readStatus();
    }
    else if (function == P_BiasState) {
        status |= setBiasState(value);
        status |= readStatus();
    }
    else if (function == P_BiasInterlock) {
        status |= setBiasInterlock(value);
        status |= readStatus();
    }
    else if (function == P_NumChannels) {
        status |= setNumChannels(value);
        status |= readStatus();
    }
    else if (function == P_NumAcquire) {
        status |= setNumAcquire(value);
        status |= readStatus();
    }
    else if (function == P_PingPong) {
        status |= setPingPong(value);
        status |= readStatus();
    }
    else if (function == P_Range) {
        if (channel == 0) {
            status |= setRange(value);
        } else {
           status |= setRange(channel, value);
        }
        status |= readStatus();
    }
    else if (function == P_ReadData) {
        status |= triggerCallbacks();
    }
    else if (function == P_Resolution) {
        status |= setResolution(value);
        status |= readStatus();
    }
    else if (function == P_TriggerMode) {
        status |= setTriggerMode(value);
        status |= readStatus();
    }
    else if (function == P_TriggerPolarity) {
        status |= setTriggerPolarity(value);
        status |= readStatus();
    }
    else if (function == P_ValuesPerRead) {
        valuesPerRead_ = value;
        status |= setValuesPerRead(value);
        status |= readStatus();
    }
    else if (function == P_ReadFormat) {
        status |= setReadFormat(value);
        status |= readStatus();
    }
    else if (function == P_ReadStatus) {
        // We don't do this if we are acquiring, too disruptive
        if (!acquiring_) {
            status |= readStatus();
        }
    }
    else if (function == P_Reset) {
        status |= reset();
        status |= readStatus();
    }
    else if (function < FIRST_QE_COMMAND) {
        status = asynNDArrayDriver::writeInt32(pasynUser, value);
    }
    
    /* Do callbacks so higher layers see any changes */
    status |= (asynStatus) callParamCallbacks();
    
    if (status) 
        epicsSnprintf(pasynUser->errorMessage, pasynUser->errorMessageSize, 
                  "%s:%s: status=%d, function=%d, name=%s, value=%d", 
                  driverName, functionName, status, function, paramName, value);
    else        
        asynPrint(pasynUser, ASYN_TRACEIO_DRIVER, 
              "%s:%s: function=%d, name=%s, value=%d\n", 
              driverName, functionName, function, paramName, value);
    return (asynStatus)status;
}

/** Called when asyn clients call pasynFloat64->write().
  * This function sends a signal to the simTask thread if the value of P_UpdateTime has changed.
  * For all  parameters it  sets the value in the parameter library and calls any registered callbacks.
  * \param[in] pasynUser pasynUser structure that encodes the reason and address.
  * \param[in] value Value to write. */
asynStatus drvQuadEM::writeFloat64(asynUser *pasynUser, epicsFloat64 value)
{
    int function = pasynUser->reason;
    int status = asynSuccess;
    int channel;
    const char *paramName;
    const char* functionName = "writeFloat64";

    getAddress(pasynUser, &channel);
    
    /* Set the parameter in the parameter library. */
    status |= setDoubleParam(channel, function, value);

    /* Fetch the parameter string name for possible use in debugging */
    getParamName(function, &paramName);

    if (function == P_AveragingTime) {
        status |= setAveragingTime(value);
        epicsRingBytesFlush(ringBuffer_);
        ringCount_ = 0;
        rawCount_ = 0;
        status |= readStatus();
    }
    else if (function == P_BiasVoltage) {
        status |= setBiasVoltage(value);
        status |= readStatus();
    }
    else if (function == P_IntegrationTime) {
        status |= setIntegrationTime(value);
        status |= readStatus();
    } 
    else {
        /* All other parameters just get set in parameter list, no need to
         * act on them here */
    }
    
    /* Do callbacks so higher layers see any changes */
    status |= (asynStatus) callParamCallbacks();
    
    if (status) 
        epicsSnprintf(pasynUser->errorMessage, pasynUser->errorMessageSize, 
                  "%s:%s: status=%d, function=%d, name=%s, value=%f", 
                  driverName, functionName, status, function, paramName, value);
    else        
        asynPrint(pasynUser, ASYN_TRACEIO_DRIVER, 
              "%s:%s: function=%d, name=%s, value=%f\n", 
              driverName, functionName, function, paramName, value);
    return (asynStatus)status;
}

/** Starts and stops the electrometer.
  * \param[in] value 1 to start the electrometer, 0 to stop it.
  * This base class method must be called at the beginning of the derived class when value=1
  * and at the end of the derived class when value=0.
  */

asynStatus drvQuadEM::setAcquire(epicsInt32 value)
{
    if (value == 1) {
        numAcquired_ = 0;
        setIntegerParam(P_NumAcquired, 0);
        callParamCallbacks();
    } 
    else {

    }
    return asynSuccess;
}


/** Downloads all of the current EPICS settings to the electrometer.  
  * Typically used after the electrometer is power-cycled.
  */
asynStatus drvQuadEM::reset() 
{
    epicsInt32 iValue;
    epicsFloat64 dValue;

    getIntegerParam(P_Range, &iValue);
    setRange(iValue);

    getIntegerParam(P_ValuesPerRead, &iValue);
    setValuesPerRead(iValue);

    getDoubleParam(P_AveragingTime, &dValue);
    setAveragingTime(dValue);

    getIntegerParam(P_TriggerMode, &iValue);
    setTriggerMode(iValue);
    
    getIntegerParam(P_NumChannels, &iValue);
    setNumChannels(iValue);
    
    getIntegerParam(P_BiasState, &iValue);
    setBiasState(iValue);
    
    getIntegerParam(P_BiasInterlock, &iValue);
    setBiasInterlock(iValue);
    
    getDoubleParam(P_BiasVoltage, &dValue);
    setBiasVoltage(dValue);
    
    getIntegerParam(P_Resolution, &iValue);
    setResolution(iValue);

    getIntegerParam(P_ReadFormat, &iValue);
    setReadFormat(iValue);
    
    getDoubleParam(P_IntegrationTime, &dValue);
    setIntegrationTime(dValue);
    
    readStatus();
    
    getIntegerParam(ADAcquire, &iValue);
    setAcquire(iValue);

    return asynSuccess;
}

// Dummy implementations of set functions.  These return success. 
//  These will be called when a derived class does not implement a function
void       drvQuadEM::exitHandler()                          {return;}
asynStatus drvQuadEM::setAcquireMode(epicsInt32 value)       {return asynSuccess;}
asynStatus drvQuadEM::setAveragingTime(epicsFloat64 value)   {return asynSuccess;}
asynStatus drvQuadEM::setBiasState(epicsInt32 value)         {return asynSuccess;}
asynStatus drvQuadEM::setBiasVoltage(epicsFloat64 value)     {return asynSuccess;}
asynStatus drvQuadEM::setBiasInterlock(epicsInt32 value)     {return asynSuccess;}
asynStatus drvQuadEM::setIntegrationTime(epicsFloat64 value) {return asynSuccess;}
asynStatus drvQuadEM::setNumChannels(epicsInt32 value)       {return asynSuccess;}
asynStatus drvQuadEM::setNumAcquire(epicsInt32 value)        {return asynSuccess;}
asynStatus drvQuadEM::setPingPong(epicsInt32 value)          {return asynSuccess;}
asynStatus drvQuadEM::setRange(epicsInt32 value)             {return asynSuccess;}
asynStatus drvQuadEM::setRange(int chan, epicsInt32 value)   {return asynSuccess;}
asynStatus drvQuadEM::setReadFormat(epicsInt32 value)        {return asynSuccess;}
asynStatus drvQuadEM::setResolution(epicsInt32 value)        {return asynSuccess;}
asynStatus drvQuadEM::setTriggerMode(epicsInt32 value)       {return asynSuccess;}
asynStatus drvQuadEM::setTriggerPolarity(epicsInt32 value)   {return asynSuccess;}
asynStatus drvQuadEM::setValuesPerRead(epicsInt32 value)     {return asynSuccess;}
