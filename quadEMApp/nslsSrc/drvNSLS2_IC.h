/*
 * drvNSLS2_IC.h
 * 
 * Asyn driver that inherits from the drvQuadEM class to control the NSLS2 electrometer / XBPM
 *
 * Author: Mark Rivers, Pete Siddons
 *
 * Created May 20th, 2019
 */

#include "drvQuadEM.h"

#define MAX_FIRMWARE_LEN 64
#define MAX_RANGES 8
#define P_DACString               "QE_DAC"                 /* asynInt32,    r/w */
#define P_DACDoubleString         "QE_DAC_DOUBLE"          /* asynFloat64   r/w */
#define P_CalibrationModeString   "QE_CALIBRATION_MODE"    /* asynInt32,    r/w */
#define P_ADCOffsetString         "QE_ADC_OFFSET"          /* asynInt32,    r/w */
#define P_FullScaleString         "QE_FULL_SCALE"          /* asynFloat64   r/w */
/** Class to control the NSLS Precision Integrator */
class drvNSLS2_IC : public drvQuadEM {
public:
    drvNSLS2_IC(const char *portName, int ringBufferSize);
    ~drvNSLS2_IC();
    
    /* These are the methods we implement from asynPortDriver */
    void report(FILE *fp, int details);
                 
    /* These are the methods that are new to this class */
    virtual void exitHandler();
    /* This should be private but we call it from C so it needs to be public */
    void callbackFunc();
    void pollerThread();
//    void pollerThread();
    bool isAcquiring();

protected:
    /* These are the methods we implement from quadEM */
    virtual asynStatus writeInt32(asynUser *pasynUser, epicsInt32 value);
    /* Ivan So: add writeFloat64 so we may write to the DAC thru asynPort with QE_DOUBLE_DATA */
    virtual asynStatus writeFloat64(asynUser *pasynUser, epicsFloat64 value);
    virtual asynStatus getBounds(asynUser *pasynUser, epicsInt32 *low, epicsInt32 *high);
    virtual asynStatus setAcquire(epicsInt32 value);
    virtual asynStatus setRange(epicsInt32 value);
    virtual asynStatus setValuesPerRead(epicsInt32 value);
    virtual asynStatus setAveragingTime(epicsFloat64 value);  
    virtual asynStatus setBiasVoltage(epicsFloat64 value);
    virtual asynStatus readStatus();
    virtual asynStatus reset();
    
    int P_DAC;
    #define FIRST_NSLS2_COMMAND P_DAC
    int P_DACDouble;
    int P_CalibrationMode;
    int P_ADCOffset;
    int P_FullScale; 
private:
    /* Our data */
    double ranges_[MAX_RANGES];
    epicsFloat64 rawData_[QE_MAX_INPUTS];
    int readingsAveraged_;
    int readingActive_;
    bool calibrationMode_;
    int ADCOffset_[QE_MAX_INPUTS];
    char firmwareVersion_[MAX_FIRMWARE_LEN];
    volatile unsigned int *fpgabase_;  //mmap'd fpga registers
    epicsFloat64 scaleFactor_[MAX_RANGES];
    int memfd_;
    int intfd_;
    int trig_;
    int prevtrig_; 
    /* our functions */
    asynStatus getFirmwareVersion();
    asynStatus readMeter(int *adcbuf);
    asynStatus computeScaleFactor();
    asynStatus OpenDacs();
    asynStatus EnableIntRef(int dev);
    asynStatus setDAC(int channel, int value);
    void mmap_fpga();
    asynStatus pl_open(int *fd);
    asynStatus setIntegrationTime(epicsFloat64 value);
    asynStatus setAcquireParams();
};

