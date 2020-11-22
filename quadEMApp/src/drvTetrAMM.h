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
#define P_InterlockStatusString  "TETRAMM_INTERLOCK_STATUS"             /* asynInt32,    r/w */

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
    virtual asynStatus readStatus();
    virtual asynStatus reset();
    virtual asynStatus setAcquire(epicsInt32 value);
    virtual asynStatus setAcquireMode(epicsInt32 value);
    virtual asynStatus setAveragingTime(epicsFloat64 value);
    virtual asynStatus setBiasState(epicsInt32 value);
    virtual asynStatus setBiasVoltage(epicsFloat64 value);
    virtual asynStatus setBiasInterlock(epicsInt32 value);
    virtual asynStatus setNumChannels(epicsInt32 value);
    virtual asynStatus setNumAcquire(epicsInt32 value);
    virtual asynStatus setRange(epicsInt32 value);
    virtual asynStatus setReadFormat(epicsInt32 value);
    virtual asynStatus setTriggerMode(epicsInt32 value);
    virtual asynStatus setTriggerPolarity(epicsInt32 value);
    virtual asynStatus setValuesPerRead(epicsInt32 value);
    int P_InterlockStatus;
 
private:
    /* Our data */
    asynUser *pasynUserMeter_;
    epicsEventId acquireStartEvent_;
    int readingActive_;
    int numResync_;
    char *QEPortName_;
    char firmwareVersion_[MAX_COMMAND_LEN];
    char outString_[MAX_COMMAND_LEN];
    char inString_[MAX_COMMAND_LEN];
    asynStatus sendCommand();
    asynStatus writeReadMeter();
    asynStatus setAcquireParams();
    asynStatus getFirmwareVersion();
};

