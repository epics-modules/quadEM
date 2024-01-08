/*  drvSoftQuadEM.cpp
    A driver to get electronmeter readouts from a 4-element EPICS array (waveform record).
*/

#include <math.h>

#include <epicsThread.h>
#include <epicsTime.h>
#include <errlog.h>
#include <iocsh.h>

#include <drvQuadEM.h>

#include <epicsExport.h>

static const char *driverName = "drvSoftQuadEM";

#define QES_DataInString  "QES_DATA_IN"

class drvSoftQuadEM : public drvQuadEM
{
public:
    drvSoftQuadEM(const char *portName, int ringBufferSize);
    ~drvSoftQuadEM();

    asynStatus writeFloat64Array(asynUser *pasynUser, epicsFloat64 *value, size_t nElements);

protected:
    virtual asynStatus setAveragingTime(epicsFloat64 value);
    virtual asynStatus setAcquire(epicsInt32 value);
    virtual asynStatus readStatus();
    virtual asynStatus reset();

private:
    int QES_DataIn;
    #define FIRST_QES_COMMAND QES_DataIn

    int acquire_;
};

drvSoftQuadEM::drvSoftQuadEM(const char *portName, int ringBufferSize)
    : drvQuadEM(portName, ringBufferSize), acquire_(0)
{
    createParam(QES_DataInString,     asynParamFloat64Array,  &QES_DataIn);

    setIntegerParam(P_Model, QE_ModelSoftDevice);
}

drvSoftQuadEM::~drvSoftQuadEM()
{
}

asynStatus drvSoftQuadEM::setAcquire(epicsInt32 value)
{
    acquire_ = value;
    return asynSuccess;
}

asynStatus drvSoftQuadEM::readStatus()
{
    return asynSuccess;
}

asynStatus drvSoftQuadEM::reset()
{
    asynStatus status;

    status = drvQuadEM::reset();
    return status;
}

asynStatus drvSoftQuadEM::setAveragingTime(epicsFloat64 value)
{
    int numAverage;
    double sampleTime;
    double averagingTime;

    getDoubleParam (P_SampleTime, &sampleTime);
    getDoubleParam (P_AveragingTime, &averagingTime);

    numAverage = (int)((averagingTime / sampleTime) + 0.5);
    setIntegerParam(P_NumAverage, numAverage);

    return asynSuccess;
}

asynStatus drvSoftQuadEM::writeFloat64Array(asynUser *pasynUser, epicsFloat64 *value, size_t nElements)
{
    int function = pasynUser->reason;
    int status = asynSuccess;
    const char *paramName;
    const char* functionName = "writeFloat64Array";

    /* Fetch the parameter string name for possible use in debugging */
    getParamName(function, &paramName);

    if (function == QES_DataIn) {
        if (nElements == 4) {
            if (acquire_)
                computePositions(value);
        }
        else {
            status = asynError;
        }
    }
    else if (function < FIRST_QES_COMMAND) {
        status = drvQuadEM::writeFloat64Array(pasynUser, value, nElements);
    }

    if (status)
        epicsSnprintf(pasynUser->errorMessage, pasynUser->errorMessageSize, 
                  "%s:%s: status=%d, function=%d, name=%s", 
                  driverName, functionName, status, function, paramName);
    else        
        asynPrint(pasynUser, ASYN_TRACEIO_DRIVER, 
              "%s:%s: function=%d, name=%s\n", 
              driverName, functionName, function, paramName);

    return (asynStatus)status;
}

extern "C" {

int drvSoftQuadEMConfigure(const char *portName, int ringBufferSize)
{
    new drvSoftQuadEM(portName, ringBufferSize);
    return(asynSuccess);
}


/* EPICS iocsh shell commands */

static const iocshArg initArg0 = { "portName", iocshArgString};
static const iocshArg initArg1 = { "ring buffer size",iocshArgInt};
static const iocshArg * const initArgs[] = {&initArg0, &initArg1};
static const iocshFuncDef initFuncDef = {"drvSoftQuadEMConfigure", 2, initArgs};
static void initCallFunc(const iocshArgBuf *args)
{
    drvSoftQuadEMConfigure(args[0].sval, args[2].ival);
}

void drvSoftQuadEMRegister(void)
{
    iocshRegister(&initFuncDef, initCallFunc);
}

epicsExportRegistrar(drvSoftQuadEMRegister);

}
