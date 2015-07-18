/*
 * drvTetrAMM.h
 * 
 * Asyn driver that inherits from the drvQuadEM class to control the CaenEls TetrAMM 4-channel picoammeter
 *
 * Author: Mark Rivers
 *
 * Created July 14, 2015
 */

#include "drvQuadEM.h"

#define MAX_COMMAND_LEN 256

/** Class to control the CaenEls TetrAMM 4-Channel Picoammeter */
class drvTetrAMM : public drvQuadEM {
public:
    drvTetrAMM(const char *portName, const char *QEPortName, int ringBufferSize);
    
    /* These are the methods we implement from asynPortDriver */
    void report(FILE *fp, int details);
                 
    /* These are the methods that are new to this class */
    void readThread(void);
    virtual void exitHandler();

protected:
    /* These are the methods we implement from quadEM */
    virtual asynStatus setAcquire(epicsInt32 value);
    virtual asynStatus setRange(epicsInt32 value);
    virtual asynStatus setNumChannels(epicsInt32 value);
    virtual asynStatus setValuesPerRead(epicsInt32 value);
    virtual asynStatus setBiasState(epicsInt32 value);
    virtual asynStatus setBiasVoltage(epicsFloat64 value);
    virtual asynStatus setBiasInterlock(epicsInt32 value);
    virtual asynStatus readStatus();
    virtual asynStatus reset();
 
private:
    /* Our data */
    asynUser *pasynUserMeter_;
    epicsEventId acquireStartEvent_;
    int readingActive_;
    int numAcquired_;
    char *QEPortName_;
    char firmwareVersion_[MAX_COMMAND_LEN];
    char outString_[MAX_COMMAND_LEN];
    char inString_[MAX_COMMAND_LEN];
    asynStatus sendCommand();
    asynStatus writeReadMeter();
    asynStatus getFirmwareVersion();
};

