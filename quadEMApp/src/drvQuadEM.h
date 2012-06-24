/*
 * drvQuadEM.h
 * 
 * Asyn driver base class that inherits from the asynPortDriver class to control quad electrometers
 *
 * Author: Mark Rivers
 *
 * Created June 14, 2012
 */

#include "asynPortDriver.h"

/* These are the drvInfo strings that are used to identify the parameters.
 * They are used by asyn clients, including standard asyn device support */
#define P_AcquireString            "QE_ACQUIRE"                 /* asynInt32,    r/w */
#define P_CurrentOffsetString      "QE_CURRENT_OFFSET"          /* asynInt32,    r/w */
#define P_DataString               "QE_DATA"                    /* asynInt32,    r/o */
#define P_DoubleDataString         "QE_DOUBLE_DATA"             /* asynFloat64,  r/o */
#define P_IntArrayDataString       "QE_INT_ARRAY_DATA"          /* asynInt32Array,  r/o */
#define P_PingPongString           "QE_PING_PONG"               /* asynInt32,    r/w */
#define P_IntegrationTimeString    "QE_INTEGRATION_TIME"        /* asynFloat64,  r/w */
#define P_RangeString              "QE_RANGE"                   /* asynInt32,    r/w */
#define P_ResetString              "QE_RESET"                   /* asynInt32,    r/w */
#define P_TriggerString            "QE_TRIGGER"                 /* asynInt32,    r/w */

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
/* This defines the scale factor for integer positions, which preserves 20 bit resolution */
#define QE_POSITION_SCALE 1048576

/** Base class to control the quad electrometer */
class drvQuadEM : public asynPortDriver {
public:
    drvQuadEM(const char *portName, int numParams);
                 
    /* These are the methods that we override from asynPortDriver */
    virtual asynStatus writeInt32(asynUser *pasynUser, epicsInt32 value);
    virtual asynStatus writeFloat64(asynUser *pasynUser, epicsFloat64 value);

protected:
    /** Values used for pasynUser->reason, and indexes into the parameter library. */
    int P_Acquire;
    #define FIRST_QE_COMMAND P_Acquire
    int P_CurrentOffset;
    int P_Data;
    int P_DoubleData;
    int P_IntArrayData;
    int P_PingPong;
    int P_IntegrationTime;
    int P_Range;
    int P_Reset;
    int P_Trigger;
    #define LAST_QE_COMMAND P_Trigger
    
    void computePositions(epicsInt32 raw[QE_MAX_INPUTS]);
    virtual asynStatus setAcquire(epicsInt32 value) = 0;
    virtual asynStatus setPingPong(epicsInt32 value) = 0;
    virtual asynStatus setIntegrationTime(epicsFloat64 value) = 0;
    virtual asynStatus setRange(epicsInt32 value) = 0;
    virtual asynStatus setReset() = 0;
    virtual asynStatus setTrigger(epicsInt32 value) = 0;
    virtual asynStatus getSettings() = 0;
 };


#define NUM_QE_PARAMS (&LAST_QE_COMMAND - &FIRST_QE_COMMAND + 1)

