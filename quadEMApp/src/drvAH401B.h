/*
 * drvAH401B.h
 * 
 * Asyn driver that inherits from the asynPortDriver class to control the Ellectra AH401B 4-channel picoammeter
 *
 * Author: Mark Rivers
 *
 * Created June 3, 2012
 */

#include "drvQuadEM.h"

#define MAX_COMMAND_LEN 256
#define MIN_INTEGRATION_TIME 0.001
#define MAX_INTEGRATION_TIME 1.0
#define AH401B_TIMEOUT 1.0

/** Class to control the Elettra AH401B 4-Channel Picoammeter */
class drvAH401B : public drvQuadEM {
public:
    drvAH401B(const char *portName, const char *QEPortName);
                 
    /* These are the methods that are new to this class */
    void readThread(void);

protected:
    virtual asynStatus setAcquire(epicsInt32 value);
    virtual asynStatus setPingPong(epicsInt32 value);
    virtual asynStatus setIntegrationTime(epicsFloat64 value);
    virtual asynStatus setRange(epicsInt32 value);
    virtual asynStatus setReset();
    virtual asynStatus setTrigger(epicsInt32 value);
 
private:
    /* Our data */
    asynUser *pasynUserMeter_;
    epicsEventId readDataEvent_;
    char outString_[MAX_COMMAND_LEN];
    char inString_[MAX_COMMAND_LEN];
    asynStatus writeReadMeter();
};

