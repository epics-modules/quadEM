/*
 * drvQuadEM.h
 * 
 * Asyn driver base class that inherits from the asynPortDriver class to control quad electrometers
 *
 * Author: Mark Rivers
 *
 * Created June 14, 2012
 */

#include <epicsExit.h>
#include "asynPortDriver.h"

/* These are the drvInfo strings that are used to identify the parameters.
 * They are used by asyn clients, including standard asyn device support */
#define P_AcquireString            "QE_ACQUIRE"                 /* asynInt32,    r/w */
#define P_CurrentOffsetString      "QE_CURRENT_OFFSET"          /* asynInt32,    r/w */
#define P_PositionOffsetString     "QE_POSITION_OFFSET"         /* asynInt32,    r/w */
#define P_PositionScaleString      "QE_POSITION_SCALE"          /* asynInt32,    r/w */
#define P_DataString               "QE_DATA"                    /* asynInt32,    r/o */
#define P_DoubleDataString         "QE_DOUBLE_DATA"             /* asynFloat64,  r/o */
#define P_IntArrayDataString       "QE_INT_ARRAY_DATA"          /* asynInt32Array,  r/o */
#define P_PingPongString           "QE_PING_PONG"               /* asynInt32,    r/w */
#define P_IntegrationTimeString    "QE_INTEGRATION_TIME"        /* asynFloat64,  r/w */
#define P_SampleTimeString         "QE_SAMPLE_TIME"             /* asynFloat64,  r/o */
#define P_RangeString              "QE_RANGE"                   /* asynInt32,    r/w */
#define P_ResetString              "QE_RESET"                   /* asynInt32,    r/w */
#define P_TriggerString            "QE_TRIGGER"                 /* asynInt32,    r/w */
#define P_NumChannelsString        "QE_NUM_CHANNELS"            /* asynInt32,    r/w */
#define P_BiasStateString          "QE_BIAS_STATE"              /* asynInt32,    r/w */
#define P_BiasVoltageString        "QE_BIAS_VOLTAGE"            /* asynFloat64,  r/w */
#define P_ResolutionString         "QE_RESOLUTION"              /* asynInt32,    r/w */
#define P_NumAverageString         "QE_NUM_AVERAGE"             /* asynInt32,    r/w */
#define P_ModelString              "QE_MODEL"                   /* asynInt32,    r/w */

/* Models */
typedef enum {
    QE_ModelUnknown,
    QE_ModelAPS_EM,
    QE_ModelAH401B,
    QE_ModelAH401D,
    QE_ModelAH501,
    QE_ModelAH501C,
    QE_ModelAH501D
} QEModel_t;

/* These enums give the offsets into the data array for each value */
typedef enum {
    QECurrent1,
    QECurrent2,
    QECurrent3,
    QECurrent4,
    QESum12,
    QESum34,
    QESum1234,
    QEDiff12,
    QEDiff34,
    QEPosition12,
    QEPosition34
} QEData_t;
#define QE_MAX_DATA QEPosition34+1
#define QE_MAX_INPUTS 4

/** Base class to control the quad electrometer */
class drvQuadEM : public asynPortDriver {
public:
    drvQuadEM(const char *portName, int numParams);
                 
    /* These are the methods that we override from asynPortDriver */
    virtual asynStatus writeInt32(asynUser *pasynUser, epicsInt32 value);
    virtual asynStatus writeFloat64(asynUser *pasynUser, epicsFloat64 value);
    virtual void exitHandler();

protected:
    /** Values used for pasynUser->reason, and indexes into the parameter library. */
    int P_Acquire;
    #define FIRST_QE_COMMAND P_Acquire
    int P_CurrentOffset;
    int P_PositionOffset;
    int P_PositionScale;
    int P_Data;
    int P_DoubleData;
    int P_IntArrayData;
    int P_PingPong;
    int P_IntegrationTime;
    int P_SampleTime;
    int P_Range;
    int P_Reset;
    int P_Trigger;
    int P_NumChannels;
    int P_BiasState;
    int P_BiasVoltage;
    int P_Resolution;
    int P_NumAverage;
    int P_Model;
    #define LAST_QE_COMMAND P_Model
    // We cache these values so we don't need to call getIntegerParam inside the
    // fast data reading loop
    int resolution_;
    int numChannels_;
    int numAverage_;
    int numAveraged_;

    void computePositions(epicsInt32 raw[QE_MAX_INPUTS]);
    virtual asynStatus setAcquire(epicsInt32 value)=0;
    virtual asynStatus setPingPong(epicsInt32 value);
    virtual asynStatus setIntegrationTime(epicsFloat64 value);
    virtual asynStatus setRange(epicsInt32 value);
    virtual asynStatus setTrigger(epicsInt32 value);
    virtual asynStatus setNumChannels(epicsInt32 value);
    virtual asynStatus setBiasState(epicsInt32 value);
    virtual asynStatus setBiasVoltage(epicsFloat64 value);
    virtual asynStatus setResolution(epicsInt32 value);
    virtual asynStatus getSettings()=0;
    virtual asynStatus reset()=0;
};


#define NUM_QE_PARAMS ((int)(&LAST_QE_COMMAND - &FIRST_QE_COMMAND + 1))

