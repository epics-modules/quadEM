/*
 * drvAPS_EM.h
 * 
 * Asyn driver that inherits from the asynPortDriver class to control the APS 4-channel picoammeter
 *
 * Author: Mark Rivers
 *
 * Created June 3, 2012
 */

#include "drvQuadEM.h"

#define APS_EM_MAX_RAW 8

typedef enum {
  PingValue, 
  PongValue, 
  AverageValue
} PingPongValue_t;


/** Class to control the Elettra APS_EM 4-Channel Picoammeter */
class drvAPS_EM : public drvQuadEM {
public:
    drvAPS_EM(const char *portName, unsigned short *baseAddr, int fiberChannel,
              const char *unidigName, int unidigChan, char *unidigDrvInfo, int ringBufferSize);              
    /* These are the methods that are new to this class */
    void pollerThread();                   /* Polling routine if no interrupts */
    void callbackFunc(unsigned int mask);  /* Callback function */

protected:
    virtual asynStatus setAcquire(epicsInt32 value);
    virtual asynStatus setPingPong(epicsInt32 value);
    virtual asynStatus setIntegrationTime(epicsFloat64 value);
    virtual asynStatus setRange(epicsInt32 value);
    virtual asynStatus readStatus();
    virtual void report(FILE *fp, int details);
    virtual asynStatus reset();
 
private:
    /* Our data */
    asynUInt32Digital *pUInt32Digital_;
    int unidigChan_;
    void *pUInt32DigitalPvt_;
    asynUser *pUInt32DAsynUser_;
    void *pUInt32RegistrarPvt_;
    PingPongValue_t pingPong_;
    epicsFloat64 rawData_[QE_MAX_INPUTS];
    int readingsAveraged_;
    volatile unsigned short *baseAddress_;
    asynStatus setPulse();
    asynStatus setPeriod();
    asynStatus setGo();
    asynStatus writeMeter(int command, int value);
    asynStatus readMeter(epicsInt32 raw[QE_MAX_INPUTS]);
};

