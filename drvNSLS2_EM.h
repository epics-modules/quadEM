/*
 * drvNSLS2_EM.h
 * 
 * Asyn driver that inherits from the drvQuadEM class to control the NSLS Precision Integrator
 *
 * Author: Mark Rivers
 *
 * Created December 4, 2015
 */

#include "drvQuadEM.h"

#define MAX_COMMAND_LEN 256
#define MAX_MODULES 1
#define MAX_PORTNAME_LEN 32
#define MAX_RANGES 5

/** Class to control the NSLS Precision Integrator */
class drvNSLS2_EM : public drvQuadEM {
public:
    drvNSLS2_EM(const char *portName, int ringBufferSize);
    
    /* These are the methods we implement from asynPortDriver */
    void report(FILE *fp, int details);
                 
    /* These are the methods that are new to this class */
    virtual void exitHandler();

protected:
    /* These are the methods we implement from quadEM */
    virtual asynStatus setAcquire(epicsInt32 value);
    virtual asynStatus setRange(epicsInt32 value);
    virtual asynStatus setAveragingTime(epicsFloat64 value);  
    virtual asynStatus setValuesPerRead(epicsInt32 value);
    virtual asynStatus setBiasVoltage(epicsFloat64 value);
    virtual asynStatus readStatus();
    virtual asynStatus reset();
 
private:
    /* Our data */
    double ranges_[MAX_RANGES];
    int readingActive_;
    int firmwareVersion_;
    volatile unsigned int *fpgabase;  //mmap'd fpga registers

    /* our functions */
    asynStatus getFirmwareVersion();
    int NSLS2_EM::readMeter(int *adcbuf);
    void NSLS2_EM::mmap_fpga();
}

