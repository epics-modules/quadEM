/*
 * drvNSLS_EM.h
 * 
 * Asyn driver that inherits from the drvQuadEM class to control the NSLS Precision Integrator
 *
 * Author: Mark Rivers
 *
 * Created December 4, 2015
 */

#include "drvQuadEM.h"

#define MAX_COMMAND_LEN 256
#define MAX_MODULES 16
#define MAX_IPNAME_LEN 16
#define MAX_PORTNAME_LEN 32
#define MAX_RANGES 8

typedef struct {
    int moduleID;
    char moduleIP[MAX_IPNAME_LEN];
} moduleInfo_t;

/** Class to control the NSLS Precision Integrator */
class drvNSLS_EM : public drvQuadEM {
public:
    drvNSLS_EM(const char *portName, const char *broadcastAddress, int moduleID, int ringBufferSize);
    
    /* These are the methods we implement from asynPortDriver */
    void report(FILE *fp, int details);
                 
    /* These are the methods that are new to this class */
    void readThread(void);
    virtual void exitHandler();

protected:
    /* These are the methods we implement from quadEM */
    virtual asynStatus setAcquire(epicsInt32 value);
    virtual asynStatus setPingPong(epicsInt32 value);
    virtual asynStatus setIntegrationTime(epicsFloat64 value);
    virtual asynStatus setRange(epicsInt32 value);
    virtual asynStatus setValuesPerRead(epicsInt32 value);
    virtual asynStatus readStatus();
    virtual asynStatus reset();
 
private:
    /* Our data */
    char *broadcastAddress_;
    char udpPortName_[MAX_PORTNAME_LEN];
    char tcpCommandPortName_[MAX_PORTNAME_LEN];
    char tcpDataPortName_[MAX_PORTNAME_LEN];
    asynUser *pasynUserUDP_;
    asynUser *pasynUserTCPCommand_;
    asynUser *pasynUserTCPCommandConnect_;
    asynUser *pasynUserTCPData_;
    epicsEventId acquireStartEvent_;
    int moduleID_;
    int numModules_;
    double ranges_[MAX_RANGES];
    double scaleFactor_;
    moduleInfo_t moduleInfo_[MAX_MODULES];
    int readingActive_;
    char firmwareVersion_[MAX_COMMAND_LEN];
    char ipAddress_[MAX_IPNAME_LEN];
    char outString_[MAX_COMMAND_LEN];
    char inString_[MAX_COMMAND_LEN];
    asynStatus findModule();
    asynStatus writeReadMeter();
    asynStatus getFirmwareVersion();
    asynStatus setMode();
    asynStatus computeScaleFactor();
};

