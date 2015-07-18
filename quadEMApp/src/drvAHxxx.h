/*
 * drvAHxxx.h
 * 
 * Asyn driver that inherits from the asynPortDriver class to control the Ellectra AHxxx 4-channel picoammeter
 *
 * Author: Mark Rivers
 *
 * Created June 3, 2012
 */

#include "drvQuadEM.h"

#define MAX_COMMAND_LEN 256

/** Class to control the Elettra AHxxx 4-Channel Picoammeter */
class drvAHxxx : public drvQuadEM {
public:
    drvAHxxx(const char *portName, const char *QEPortName, int ringBufferSize, const char *modelName);
    
    /* These are the methods we implement from asynPortDriver */
    void report(FILE *fp, int details);
                 
    /* These are the methods that are new to this class */
    void readThread(void);
    virtual void exitHandler();

protected:
    /* These are the methods we implement from quadEM */
    virtual asynStatus setAcquire(epicsInt32 value);
    virtual asynStatus setRange(epicsInt32 value);
    virtual asynStatus setPingPong(epicsInt32 value);
    virtual asynStatus setIntegrationTime(epicsFloat64 value);
    virtual asynStatus setNumChannels(epicsInt32 value);
    virtual asynStatus setBiasState(epicsInt32 value);
    virtual asynStatus setBiasVoltage(epicsFloat64 value);
    virtual asynStatus setResolution(epicsInt32 value);
    virtual asynStatus readStatus();
    virtual asynStatus reset();
 
private:
    /* Our data */
    asynUser *pasynUserMeter_;
    epicsEventId acquireStartEvent_;
    int readingActive_;
    int numAcquired_;
    char *QEPortName_;
    bool AH501Series_;
    bool AH401Series_;
    char firmwareVersion_[MAX_COMMAND_LEN];
    QEModel_t model_;
    char outString_[MAX_COMMAND_LEN];
    char inString_[MAX_COMMAND_LEN];
    asynStatus sendCommand();
    asynStatus writeReadMeter();
};

