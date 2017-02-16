/*
 * drvNSLS2_EM.h
 * 
 * Asyn driver that inherits from the drvQuadEM class to control the NSLS2 electrometer / XBPM
 *
 * Author: Mark Rivers, Pete Siddons
 *
 * Created January 18th, 2016
 */

#include "drvQuadEM.h"

#define MAX_FIRMWARE_LEN 64
#define MAX_RANGES 5
#define P_DACString               "QE_DAC"                 /* asynInt32,    r/w */
#define P_CalibrationModeString   "QE_CALIBRATION_MODE"    /* asynInt32,    r/w */
#define P_ADCOffsetString         "QE_ADC_OFFSET"          /* asynInt32,    r/w */

/** Class to control the NSLS Precision Integrator */
class drvNSLS2_EM : public drvQuadEM {
public:
    drvNSLS2_EM(const char *portName, int moduleID, int ringBufferSize);
    ~drvNSLS2_EM();
    
    /* These are the methods we implement from asynPortDriver */
    void report(FILE *fp, int details);
                 
    /* These are the methods that are new to this class */
    virtual void exitHandler();
    /* This should be private but we call it from C so it needs to be public */
    void callbackFunc();
    bool isAcquiring();

protected:
    /* These are the methods we implement from quadEM */
    virtual asynStatus writeInt32(asynUser *pasynUser, epicsInt32 value);
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
    int P_CalibrationMode;
    int P_ADCOffset;
 
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
    epicsFloat64 scaleFactor_[QE_MAX_INPUTS][MAX_RANGES];
    int memfd_;
    int intfd_;

    /* our functions */
    asynStatus getFirmwareVersion();
    asynStatus readMeter(int *adcbuf);
    asynStatus setDAC(int channel, int value);
    void mmap_fpga();
    asynStatus pl_open(int *fd);
    asynStatus setAcquireParams();
};

