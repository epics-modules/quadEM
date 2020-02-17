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
#include <epicsRingBytes.h>
#include <epicsMessageQueue.h>
#include "asynNDArrayDriver.h"

/* These are the drvInfo strings that are used to identify the parameters.
 * They are used by asyn clients, including standard asyn device support */
#define P_AcquireModeString        "QE_ACQUIRE_MODE"            /* asynInt32,    r/w */
#define P_CurrentOffsetString      "QE_CURRENT_OFFSET"          /* asynFloat64,  r/w */
#define P_CurrentScaleString       "QE_CURRENT_SCALE"           /* asynFloat64,  r/w */
#define P_PositionOffsetString     "QE_POSITION_OFFSET"         /* asynFloat64,  r/w */
#define P_PositionScaleString      "QE_POSITION_SCALE"          /* asynFloat64,  r/w */
#define P_GeometryString           "QE_GEOMETRY"                /* asynInt32,    r/w */
#define P_DoubleDataString         "QE_DOUBLE_DATA"             /* asynFloat64,  r/o */
#define P_IntArrayDataString       "QE_INT_ARRAY_DATA"          /* asynInt32Array,  r/o */
#define P_RingOverflowsString      "QE_RING_OVERFLOWS"          /* asynInt32,    r/o */
#define P_ReadDataString           "QE_READ_DATA"               /* asynInt32,    r/w */
#define P_PingPongString           "QE_PING_PONG"               /* asynInt32,    r/w */
#define P_IntegrationTimeString    "QE_INTEGRATION_TIME"        /* asynFloat64,  r/w */
#define P_SampleTimeString         "QE_SAMPLE_TIME"             /* asynFloat64,  r/o */
#define P_RangeString              "QE_RANGE"                   /* asynInt32,    r/w */
#define P_ResetString              "QE_RESET"                   /* asynInt32,    r/w */
#define P_TriggerModeString        "QE_TRIGGER_MODE"            /* asynInt32,    r/w */
#define P_TriggerPolarityString    "QE_TRIGGER_POLARITY"        /* asynInt32,    r/w */
#define P_NumChannelsString        "QE_NUM_CHANNELS"            /* asynInt32,    r/w */
#define P_BiasStateString          "QE_BIAS_STATE"              /* asynInt32,    r/w */
#define P_BiasVoltageString        "QE_BIAS_VOLTAGE"            /* asynFloat64,  r/w */
#define P_BiasInterlockString      "QE_BIAS_INTERLOCK"          /* asynInt32,    r/w */
#define P_HVSReadbackString        "QE_HVS_READBACK"            /* asynInt32,    r/w */
#define P_HVVReadbackString        "QE_HVV_READBACK"            /* asynFloat64,  r/w */
#define P_HVIReadbackString        "QE_HVI_READBACK"            /* asynFloat64,  r/w */
#define P_TemperatureString        "QE_TEMPERATURE"             /* asynFloat64,  r/w */
#define P_ReadStatusString         "QE_READ_STATUS"             /* asynInt32,    r/w */
#define P_ResolutionString         "QE_RESOLUTION"              /* asynInt32,    r/w */
#define P_ValuesPerReadString      "QE_VALUES_PER_READ"         /* asynInt32,    r/w */
#define P_ReadFormatString         "QE_READ_FORMAT"             /* asynInt32,    r/w */
#define P_AveragingTimeString      "QE_AVERAGING_TIME"          /* asynFloat64,  r/w */
#define P_NumAverageString         "QE_NUM_AVERAGE"             /* asynInt32,    r/o */
#define P_NumAveragedString        "QE_NUM_AVERAGED"            /* asynInt32,    r/o */
#define P_NumAcquireString         "QE_NUM_ACQUIRE"             /* asynInt32,    r/o */
#define P_NumAcquiredString        "QE_NUM_ACQUIRED"            /* asynInt32,    r/o */
#define P_ModelString              "QE_MODEL"                   /* asynInt32,    r/w */
#define P_FirmwareString           "QE_FIRMWARE"                /* asynOctet,    r/w */


/* Models */
typedef enum {
    QE_ModelUnknown,
    QE_ModelAPS_EM,  // Steve Ross' electrometer
    QE_ModelAH401B,
    QE_ModelAH401D,
    QE_ModelAH501,
    QE_ModelAH501BE,  // Elettra version of AH501B.  Different firmware from CaenEls
    QE_ModelAH501C,
    QE_ModelAH501D,
    QE_ModelTetrAMM,
    QE_ModelNSLS_EM,
    QE_ModelNSLS2_EM,
    QE_ModelNSLS2_IC
} QEModel_t;

/* These enums give the offsets into the data array for each value */
typedef enum {
    QECurrent1,
    QECurrent2,
    QECurrent3,
    QECurrent4,
    QESumX,
    QESumY,
    QESumAll,
    QEDiffX,
    QEDiffY,
    QEPositionX,
    QEPositionY
} QEData_t;

typedef enum {
    QEGeometryDiamond,
    QEGeometrySquare
} QEGeometry_t;

/* Acquire modes */
typedef enum {
    QEAcquireModeContinuous,
    QEAcquireModeMultiple,
    QEAcquireModeSingle
} QEAcquireMode_t;

/* Trigger modes */
typedef enum {
    QETriggerModeFreeRun,
    QETriggerModeSoftware,
    QETriggerModeExtTrigger,
    QETriggerModeExtBulb,
    QETriggerModeExtGate,
} QETriggerMode_t;

/* Trigger polarity */
typedef enum {
    QETriggerPolarityPositive,
    QETriggerPolarityNegative

} QETriggerPolarity_t;


/* Read format */
typedef enum {
    QEReadFormatBinary,
    QEReadFormatASCII
} QEReadFormat_t;


#define QE_MAX_DATA (QEPositionY+1)
#define QE_MAX_INPUTS 4
#define QE_DEFAULT_RING_BUFFER_SIZE 2048

/** Base class to control the quad electrometer */
class drvQuadEM : public asynNDArrayDriver {
public:
    drvQuadEM(const char *portName, int ringBufferSize);
                 
    /* These are the methods that we override from asynPortDriver */
    virtual asynStatus writeInt32(asynUser *pasynUser, epicsInt32 value);
    virtual asynStatus writeFloat64(asynUser *pasynUser, epicsFloat64 value);
    virtual void exitHandler();
    void callbackTask();

protected:
    /** Values used for pasynUser->reason, and indexes into the parameter library. */
    int P_AcquireMode;
    #define FIRST_QE_COMMAND P_AcquireMode
    int P_CurrentOffset;
    int P_CurrentScale;
    int P_PositionOffset;
    int P_PositionScale;
    int P_Geometry;
    int P_DoubleData;
    int P_IntArrayData;
    int P_RingOverflows;
    int P_ReadData;
    int P_PingPong;
    int P_IntegrationTime;
    int P_SampleTime;
    int P_Range;
    int P_Reset;
    int P_TriggerMode;
    int P_TriggerPolarity;
    int P_NumChannels;
    int P_BiasState;
    int P_BiasVoltage;
    int P_BiasInterlock;
    int P_HVSReadback;
    int P_HVVReadback;
    int P_HVIReadback;
    int P_Temperature;
    int P_ReadStatus;
    int P_Resolution;
    int P_ValuesPerRead;
    int P_ReadFormat;
    int P_AveragingTime;
    int P_NumAverage;
    int P_NumAveraged;
    int P_NumAcquire;
    int P_NumAcquired;
    int P_Model;
    int P_Firmware;
    // We cache these values so we don't need to call getIntegerParam inside the
    // fast data reading loop
    int resolution_;
    int numChannels_;
    int valuesPerRead_;
    int acquiring_;
    int numAcquired_;

    void computePositions(epicsFloat64 raw[QE_MAX_INPUTS]);
    virtual asynStatus readStatus()=0;
    virtual asynStatus reset()=0;
    virtual asynStatus setAcquire(epicsInt32 value);
    virtual asynStatus setAcquireMode(epicsInt32 value);
    virtual asynStatus setAveragingTime(epicsFloat64 value);
    virtual asynStatus setBiasState(epicsInt32 value);
    virtual asynStatus setBiasVoltage(epicsFloat64 value);
    virtual asynStatus setBiasInterlock(epicsInt32 value);
    virtual asynStatus setIntegrationTime(epicsFloat64 value);
    virtual asynStatus setNumChannels(epicsInt32 value);
    virtual asynStatus setNumAcquire(epicsInt32 value);
    virtual asynStatus setPingPong(epicsInt32 value);
    virtual asynStatus setRange(epicsInt32 value);
    virtual asynStatus setReadFormat(epicsInt32 value);
    virtual asynStatus setResolution(epicsInt32 value);
    virtual asynStatus setTriggerMode(epicsInt32 value);
    virtual asynStatus setTriggerPolarity(epicsInt32 value);
    virtual asynStatus setValuesPerRead(epicsInt32 value);
    virtual asynStatus triggerCallbacks();
    
private:
    virtual asynStatus doDataCallbacks(int numRead);
    int ringCount_;
    int rawCount_;
    epicsRingBytesId ringBuffer_;
    epicsMessageQueueId msgQId_;

};
