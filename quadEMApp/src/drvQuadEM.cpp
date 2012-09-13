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

#include "drvQuadEM.h"

static const char *driverName="drvQuadEM";

static void exitHandlerC(void *pPvt)
{
    drvQuadEM *pdrvQuadEM = (drvQuadEM *)pPvt;
    pdrvQuadEM->exitHandler();
}


/** Constructor for the drvQuadEM class.
  * Calls constructor for the asynPortDriver base class.
  * \param[in] portName The name of the asyn port driver to be created.
  * \param[in] numParams The number of driver-specific parameters for the derived class
  */
drvQuadEM::drvQuadEM(const char *portName, int numParams) 
   : asynPortDriver(portName, 
                    QE_MAX_DATA, /* maxAddr */ 
                    NUM_QE_PARAMS + numParams,
                    asynInt32Mask | asynInt32ArrayMask | asynFloat64Mask | asynDrvUserMask, /* Interface mask */
                    asynInt32Mask | asynInt32ArrayMask | asynFloat64Mask,                   /* Interrupt mask */
                    ASYN_CANBLOCK | ASYN_MULTIDEVICE, /* asynFlags.  This driver blocks it is multi-device */
                    1, /* Autoconnect */
                    0, /* Default priority */
                    0) /* Default stack size*/    
{
    //const char *functionName = "drvQuadEM";
    
    createParam(P_AcquireString,            asynParamInt32,         &P_Acquire);
    createParam(P_CurrentOffsetString,      asynParamInt32,         &P_CurrentOffset);
    createParam(P_PositionOffsetString,     asynParamInt32,         &P_PositionOffset);
    createParam(P_PositionScaleString,      asynParamInt32,         &P_PositionScale);
    createParam(P_DataString,               asynParamInt32,         &P_Data);
    createParam(P_DoubleDataString,         asynParamFloat64,       &P_DoubleData);
    createParam(P_IntArrayDataString,       asynParamInt32Array,    &P_IntArrayData);
    createParam(P_PingPongString,           asynParamInt32,         &P_PingPong);
    createParam(P_IntegrationTimeString,    asynParamFloat64,       &P_IntegrationTime);
    createParam(P_SampleTimeString,         asynParamFloat64,       &P_SampleTime);
    createParam(P_RangeString,              asynParamInt32,         &P_Range);
    createParam(P_ResetString,              asynParamInt32,         &P_Reset);
    createParam(P_TriggerString,            asynParamInt32,         &P_Trigger);
    createParam(P_NumChannelsString,        asynParamInt32,         &P_NumChannels);
    createParam(P_BiasStateString,          asynParamInt32,         &P_BiasState);
    createParam(P_BiasVoltageString,        asynParamFloat64,       &P_BiasVoltage);
    createParam(P_ResolutionString,         asynParamInt32,         &P_Resolution);
    createParam(P_NumAverageString,         asynParamInt32,         &P_NumAverage);
    createParam(P_ModelString,              asynParamInt32,         &P_Model);
    
    setIntegerParam(P_Acquire, 0);
    setIntegerParam(P_PingPong, 0);
    setDoubleParam(P_IntegrationTime, 0.);
    setIntegerParam(P_Range, 0);
    setIntegerParam(P_Trigger, 0);
    setIntegerParam(P_NumChannels, 4);
    setIntegerParam(P_BiasState, 0);
    setDoubleParam(P_BiasVoltage, 0.);
    setIntegerParam(P_Resolution, 16);
    setIntegerParam(P_NumAverage, 1);
    numAveraged_ = 0;
    numAverage_ = 1;
    
    epicsAtExit(exitHandlerC, this);
}

/** This function computes the sums, diffs and positions, and does callbacks 
  * \param[in] raw Array of raw current readings 
  */
void drvQuadEM::computePositions(epicsInt32 raw[QE_MAX_INPUTS])
{
    int i;
    epicsInt32 currentOffset[QE_MAX_INPUTS];
    epicsInt32 positionOffset[2];
    epicsInt32 positionScale[2];
    epicsInt32 data[QE_MAX_DATA];
    epicsFloat64 doubleData[QE_MAX_DATA];
    static const char *functionName = "computePositions";
    
    for (i=0; i<QE_MAX_INPUTS; i++) {
        getIntegerParam(i, P_CurrentOffset, &currentOffset[i]);
        data[i] = raw[i] - currentOffset[i];
    }
    for (i=0; i<2; i++) {
        getIntegerParam(i, P_PositionOffset, &positionOffset[i]);
        getIntegerParam(i, P_PositionScale, &positionScale[i]);
    }
    
    data[QESum12] = data[QECurrent1] + data[QECurrent2];
    data[QESum34] = data[QECurrent3] + data[QECurrent4];
    data[QESum1234] = data[QESum12] + data[QESum34];
    data[QEDiff12] = data[QECurrent2] - data[QECurrent1];
    data[QEDiff34] = data[QECurrent4] - data[QECurrent3];
    data[QEPosition12] = (epicsInt32)((positionScale[0] * data[QEDiff12] / (double)data[QESum12]) +
                                       positionOffset[0] + 0.5);
    data[QEPosition34] = (epicsInt32)((positionScale[1] * data[QEDiff34] / (double)data[QESum34]) +
                                       positionOffset[1] + 0.5);
    for (i=0; i<QE_MAX_DATA; i++) {
        setIntegerParam(i, P_Data, data[i]);
        doubleData[i] = (double)data[i];
        setDoubleParam(i, P_DoubleData, doubleData[i]);
    }
    asynPrint(pasynUserSelf, ASYN_TRACEIO_DRIVER, 
               "%s:%s:\n", 
               driverName, functionName);

    for (i=0; i<QE_MAX_DATA; i++) {
        callParamCallbacks(i);
        asynPrint(pasynUserSelf, ASYN_TRACEIO_DRIVER, 
               "  data[%d]=%d\n", data[i]);
    }
    doCallbacksInt32Array(data, QE_MAX_DATA, P_IntArrayData, 0);
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

    if (function == P_Acquire) {
        status |= setAcquire(value);
    } 
    else if (function == P_PingPong) {
        status |= setPingPong(value);
    }
    else if (function == P_Range) {
        status |= setRange(value);
    }
    else if (function == P_Trigger) {
        status |= setTrigger(value);
    }
    else if (function == P_NumChannels) {
        status |= setNumChannels(value);
    }
    else if (function == P_BiasState) {
        status |= setBiasState(value);
    }
    else if (function == P_Resolution) {
        status |= setResolution(value);
    }
    else if (function == P_NumAverage) {
        numAverage_ = value;
    }
    else if (function == P_Reset) {
        status |= reset();
    }
    else {
        /* All other parameters just get set in parameter list, no need to
         * act on them here */
    }
    
    if (function != P_Acquire) status |= getSettings();
    
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

    if (function == P_IntegrationTime) {
        /* Make sure the update time is valid. If not change it and put back in parameter library */
        status |= setIntegrationTime(value);
    }
    else if (function == P_BiasVoltage) {
        status |= setBiasVoltage(value);
    } else {
        /* All other parameters just get set in parameter list, no need to
         * act on them here */
    }
    
    status |= getSettings();
    
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

/** Downloads all of the current EPICS settings to the electrometer.  
  * Typically used after the electrometer is power-cycled.
  */
asynStatus drvQuadEM::reset() 
{
    epicsInt32 iValue;
    epicsFloat64 dValue;

    getIntegerParam(P_Range, &iValue);
    setRange(iValue);

    getIntegerParam(P_Trigger, &iValue);
    setTrigger(iValue);
    
    getIntegerParam(P_NumChannels, &iValue);
    setNumChannels(iValue);
    
    getIntegerParam(P_BiasState, &iValue);
    setBiasState(iValue);
    
    getDoubleParam(P_BiasVoltage, &dValue);
    setBiasVoltage(dValue);
    
    getIntegerParam(P_Resolution, &iValue);
    setResolution(iValue);
    
    getSettings();
    
    getIntegerParam(P_Acquire, &iValue);
    setAcquire(iValue);

    return asynSuccess;
}

// Dummy implementations of set functions.  These return success. 
//  These will be called when a derived class does not implement a function
void       drvQuadEM::exitHandler()                          {return;}
asynStatus drvQuadEM::setPingPong(epicsInt32 value)          {return asynSuccess;}
asynStatus drvQuadEM::setIntegrationTime(epicsFloat64 value) {return asynSuccess;}
asynStatus drvQuadEM::setRange(epicsInt32 value)             {return asynSuccess;}
asynStatus drvQuadEM::setTrigger(epicsInt32 value)           {return asynSuccess;}
asynStatus drvQuadEM::setNumChannels(epicsInt32 value)       {return asynSuccess;}
asynStatus drvQuadEM::setBiasState(epicsInt32 value)         {return asynSuccess;}
asynStatus drvQuadEM::setBiasVoltage(epicsFloat64 value)     {return asynSuccess;}
asynStatus drvQuadEM::setResolution(epicsInt32 value)        {return asynSuccess;}
