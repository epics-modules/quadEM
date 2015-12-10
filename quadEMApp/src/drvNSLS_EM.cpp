/*
 * drvNSLS_EM.cpp
 * 
 * Asyn driver that inherits from the drvQuadEM class to control 
 * the NSLS Precision Integrator
 *
 * Author: Mark Rivers
 *
 * Created December 4, 2015
 */

#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <errno.h>
#include <math.h>

#include <epicsTypes.h>
#include <epicsTime.h>
#include <epicsThread.h>
#include <epicsString.h>
#include <epicsEvent.h>
#include <asynOctetSyncIO.h>
#include <asynCommonSyncIO.h>
#include <drvAsynIPPort.h>
#include <iocsh.h>

#include <epicsExport.h>
#include "drvNSLS_EM.h"

#define BROADCAST_TIMEOUT 0.2
#define NSLS_EM_TIMEOUT   0.1

#define COMMAND_PORT    4747
#define DATA_PORT       5757
#define BROADCAST_PORT 37747
#define MIN_INTEGRATION_TIME 400e-6
#define MAX_INTEGRATION_TIME 1.0

typedef enum {
  Phase0, 
  Phase1, 
  PhaseBoth
} PingPongValue_t;

static const char *driverName="drvNSLS_EM";
static void readThread(void *drvPvt);


/** Constructor for the drvNSLS_EM class.
  * Calls the constructor for the drvQuadEM base class.
  * \param[in] portName The name of the asyn port driver to be created.
  * \param[in] broadcastAddress The broadcast address of the network with this module
  * \param[in] moduleID The module ID of this module, set with rotary switch on module
  * \param[in] ringBufferSize The number of samples to hold in the input ring buffer.
  *            This should be large enough to hold all the samples between reads of the
  *            device, e.g. 1 ms SampleTime and 1 second read rate = 1000 samples.
  *            If 0 then default of 2048 is used.
  */
drvNSLS_EM::drvNSLS_EM(const char *portName, const char *broadcastAddress, int moduleID, int ringBufferSize) 
   : drvQuadEM(portName, 0, ringBufferSize)
  
{
    asynStatus status;
    const char *functionName = "drvNSLS_EM";
    char tempString[256];
    
    numModules_ = 0;
    moduleID_ = moduleID;
    ipAddress_[0] = 0;
    firmwareVersion_[0] = 0;
    broadcastAddress_ = epicsStrDup(broadcastAddress);
    
    acquireStartEvent_ = epicsEventCreate(epicsEventEmpty);
    
    strcpy(udpPortName_, "UDP_");
    strcat(udpPortName_, portName);
    strcpy(tcpCommandPortName_, "TCP_Command_");
    strcat(tcpCommandPortName_, portName);
    strcpy(tcpDataPortName_, "TCP_Data_");
    strcat(tcpDataPortName_, portName);
    
    strcpy(tempString, broadcastAddress_);
    strcat(tempString, ":37747 UDP*");

    status = (asynStatus)drvAsynIPPortConfigure(udpPortName_, tempString, 0, 0, 0);
    if (status) {
        asynPrint(pasynUserSelf, ASYN_TRACE_ERROR,
            "%s::%s error calling drvAsynIPPortConfigure for broadcast port=%s, IP=%s, status=%d\n", 
            driverName, functionName, udpPortName_, tempString, status);
        return;
    }

    // Connect to the broadcast port
    status = pasynOctetSyncIO->connect(udpPortName_, 0, &pasynUserUDP_, NULL);
    if (status) {
        asynPrint(pasynUserSelf, ASYN_TRACE_ERROR,
            "%s::%s error connecting to UDP port, status=%d, error=%s\n", 
            driverName, functionName, status, pasynUserUDP_->errorMessage);
        return;
    }
    
    // Find module on network
    status = findModule();
    if (status) {
        asynPrint(pasynUserSelf, ASYN_TRACE_ERROR,
            "%s::%s error finding module, running asynReport to list modules found\n", 
            driverName, functionName);
        report(stdout, 1);
        return;
    }
   
    acquiring_ = 0;
    readingActive_ = 0;
    setIntegerParam(P_Model, QE_ModelNSLS_EM);
    setIntegerParam(P_ValuesPerRead, 5);

    // Do everything that needs to be done when connecting to the meter initially.
    // Note that the meter could be offline when the IOC starts, so we put this in
    // the reset() function which can be done later when the meter is online.
//    lock();
//    drvQuadEM::reset();
//    unlock();

    /* Create the thread that reads the meter */
    status = (asynStatus)(epicsThreadCreate("drvNSLS_EMTask",
                          epicsThreadPriorityMedium,
                          epicsThreadGetStackSize(epicsThreadStackMedium),
                          (EPICSTHREADFUNC)::readThread,
                          this) == NULL);
    if (status) {
        printf("%s:%s: epicsThreadCreate failure, status=%d\n", driverName, functionName, status);
        return;
    }
    callParamCallbacks();
}



static void readThread(void *drvPvt)
{
    drvNSLS_EM *pPvt = (drvNSLS_EM *)drvPvt;
    
    pPvt->readThread();
}

asynStatus drvNSLS_EM::findModule()
{
    size_t nwrite;
    size_t nread;
    epicsTimeStamp start;
    epicsTimeStamp now;
    epicsFloat64 deltaTime;
    int status;
    int eomReason;
    char buffer[1024];
    char *ptr;
    int count;
    char tempString[256];
    int i;
    static const char *functionName="findModules";

    status = pasynOctetSyncIO->write(pasynUserUDP_, "i\n", 2, 1.0, &nwrite);
    epicsTimeGetCurrent(&start);

    while (1) {
        epicsTimeGetCurrent(&now);
        deltaTime = epicsTimeDiffInSeconds(&now, &start);
        if (deltaTime > BROADCAST_TIMEOUT) break;
        status = pasynOctetSyncIO->read(pasynUserUDP_, buffer, sizeof(buffer), 0.01, &nread, &eomReason);
        if ((status == asynTimeout) && (nread > 0)) {
            ptr = buffer;
            while (1) {
                ptr = strstr(ptr, "id:");
                if (!ptr) break;
                sscanf(ptr, "id: %d%n", &moduleInfo_[numModules_].moduleID, &count);
                ptr += count;
                sscanf(ptr, " ip: %s%n", moduleInfo_[numModules_].moduleIP, &count);
                ptr += count;
                numModules_++;
            }
        } else if (status != asynTimeout) {
            return asynError;
        }
    }

    // See if the specified module was found
    for (i=0; i<numModules_; i++) {
        if (moduleInfo_[i].moduleID == moduleID_) break;
    }
    
    if (i == numModules_) {
        asynPrint(pasynUserSelf, ASYN_TRACE_ERROR, 
            "%s:%s: cannot find requested module %d on network\n", 
            driverName, functionName, moduleID_);
        return asynError;
    }
    
    // Create TCP command port
    epicsSnprintf(tempString, sizeof(tempString), "%s:%d", moduleInfo_[i].moduleIP, COMMAND_PORT);
    // Set noAutoConnect, we will handle connecion management
    status = drvAsynIPPortConfigure(tcpCommandPortName_, tempString, 0, 1, 0);
    if (status) {
        asynPrint(pasynUserSelf, ASYN_TRACE_ERROR,
            "%s:%s: error calling drvAsyIPPortConfigure for TCP port %s, IP=%s, status=%d\n", 
            driverName, functionName, tcpCommandPortName_, moduleInfo_[i].moduleIP, status);
        return asynError;
    }

    // Connect to TCP command port
    status = pasynOctetSyncIO->connect(tcpCommandPortName_, 0, &pasynUserTCPCommand_, NULL);
    if (status) {
        printf("%s:%s: error calling pasynOctetSyncIO->connect for TCP port, status=%d, error=%s\n", 
               driverName, functionName, status, pasynUserTCPCommand_->errorMessage);
        return asynError;
    }
    pasynOctetSyncIO->setInputEos(pasynUserTCPCommand_, "\r\n", 2);
    pasynOctetSyncIO->setOutputEos(pasynUserTCPCommand_, "\r", 1);
    status = pasynCommonSyncIO->connect(tcpCommandPortName_, 0, &pasynUserTCPCommandConnect_, NULL);
    if (status) {
        printf("%s:%s: error calling pasynCommonSyncIO->connect forTCP port, status=%d, error=%s\n", 
               driverName, functionName, status, pasynUserTCPCommand_->errorMessage);
        return asynError;
    }

    // Create TCP data port
    epicsSnprintf(tempString, sizeof(tempString), "%s:%d", moduleInfo_[i].moduleIP, DATA_PORT);
    status = drvAsynIPPortConfigure(tcpDataPortName_, tempString, 0, 0, 0);
    if (status) {
        asynPrint(pasynUserSelf, ASYN_TRACE_ERROR,
            "%s:%s: error calling drvAsyIPPortConfigure for TCP port %s, IP=%s, status=%d\n", 
            driverName, functionName, tcpDataPortName_, moduleInfo_[i].moduleIP, status);
        return asynError;
    }

    // Connect to TCP data port
    status = pasynOctetSyncIO->connect(tcpDataPortName_, 0, &pasynUserTCPData_, NULL);
    if (status) {
        printf("%s:%s: error connecting to TCP port, status=%d, error=%s\n", 
               driverName, functionName, status, pasynUserTCPData_->errorMessage);
        return asynError;
    }
    pasynOctetSyncIO->setInputEos(pasynUserTCPData_, "\n", 1);

    return asynSuccess;
}

/** Writes a string to the NSLS_EM and reads the response. */
asynStatus drvNSLS_EM::writeReadMeter()
{
  size_t nread;
  size_t nwrite;
  asynStatus status=asynSuccess;
//  char tempString[16];
  int eomReason;
  static const char *functionName="writeReadMeter";

  // The meter has a strange behavior.  Commands that take no arguments succeed on the first write/read
  // but commands that take arguments fail on the first write read, must do it again.
  pasynCommonSyncIO->connectDevice(pasynUserTCPCommandConnect_);
  status = pasynOctetSyncIO->writeRead(pasynUserTCPCommand_, outString_, strlen(outString_), 
                                       inString_, sizeof(inString_), NSLS_EM_TIMEOUT, 
                                       &nwrite, &nread, &eomReason);
  if (strlen(outString_) > 1) {
      status = pasynOctetSyncIO->writeRead(pasynUserTCPCommand_, outString_, strlen(outString_), 
                                          inString_, sizeof(inString_), NSLS_EM_TIMEOUT, 
                                          &nwrite, &nread, &eomReason);
  }
  if (status) {
      asynPrint(pasynUserSelf, ASYN_TRACE_ERROR, 
          "%s:%s: error calling writeRead, outString=%s status=%d, nread=%d, eomReason=%d, inString=%s\n",
          driverName, functionName, outString_, status, (int)nread, eomReason, inString_);
  }
  else if (strncmp(inString_, "OK>", 3) != 0) {
      asynPrint(pasynUserSelf, ASYN_TRACE_ERROR, 
          "%s:%s: error, outString=%s expected OK>, received %s\n",
          driverName, functionName, outString_, inString_);
      status = asynError;
  }
  pasynCommonSyncIO->disconnectDevice(pasynUserTCPCommandConnect_);
  
  return status;
}

/** Read thread to read the data from the electrometer when it is in continuous acquire mode.
  * Reads the data, computes the sums and positions, and does callbacks.
  */

void drvNSLS_EM::readThread(void)
{
    asynStatus status;
    size_t nRead;
    int eomReason;
    int acquireMode;
    int numAverage;
    int valuesPerRead;
    int pingPong;
    int i;
    asynUser *pasynUser;
    asynInterface *pasynInterface;
    asynOctet *pasynOctet;
    void *octetPvt;
    int phase;
    epicsInt32 raw[4];
    epicsFloat64 data[4];
    char ASCIIData[150];
    size_t nRequested;
    static const char *functionName = "readThread";

    /* Create an asynUser */
    pasynUser = pasynManager->createAsynUser(0, 0);
    pasynUser->timeout = NSLS_EM_TIMEOUT;
    status = pasynManager->connectDevice(pasynUser, tcpDataPortName_, 0);
    if(status!=asynSuccess) {
        asynPrint(pasynUserSelf, ASYN_TRACE_ERROR,
            "%s:%s: connectDevice failed, status=%d\n",
            driverName, functionName, status);
    }
    pasynInterface = pasynManager->findInterface(pasynUser, asynOctetType, 1);
    if(!pasynInterface) {;
        asynPrint(pasynUserSelf, ASYN_TRACE_ERROR,
            "%s:%s: findInterface failed for asynOctet, status=%d\n",
            driverName, functionName, status);
    }
    pasynOctet = (asynOctet *)pasynInterface->pinterface;
    octetPvt = pasynInterface->drvPvt;
    
    /* Loop forever */
    lock();
    while (1) {
        if (acquiring_ == 0) {
            readingActive_ = 0;
            unlock();
            (void)epicsEventWait(acquireStartEvent_);
            lock();
            readingActive_ = 1;
            numAcquired_ = 0;
            getIntegerParam(P_AcquireMode,   &acquireMode);
            getIntegerParam(P_NumAverage,    &numAverage);
            getIntegerParam(P_PingPong,      &pingPong);
            getIntegerParam(P_ValuesPerRead, &valuesPerRead);
            status = pasynOctet->flush(octetPvt, pasynUser);
        }
        nRequested = sizeof(ASCIIData);
        unlock();
        pasynManager->lockPort(pasynUser);
        status = pasynOctet->read(octetPvt, pasynUser, ASCIIData, nRequested, 
                                  &nRead, &eomReason);
        pasynManager->unlockPort(pasynUser);
        lock();

        if ((status != asynSuccess) || 
            (eomReason != ASYN_EOM_EOS)) {
            if (status != asynTimeout) {
                asynPrint(pasynUserSelf, ASYN_TRACE_ERROR, 
                    "%s:%s: unexpected error reading meter status=%d, nRead=%lu, eomReason=%d\n", 
                    driverName, functionName, status, (unsigned long)nRead, eomReason);
                // We got an error reading the meter, it is probably offline.  
                // Wait 1 second before trying again.
                unlock();
                epicsThreadSleep(1.0);
                lock();
            }
            continue;
        }

        if (strstr(ASCIIData, ":")) {
            sscanf(ASCIIData, "%d: %d %d %d %d", &phase, &raw[0], &raw[1], &raw[2], &raw[3]);
        } else {
            sscanf(ASCIIData, "%d %d %d %d", &raw[0], &raw[1], &raw[2], &raw[3]);
        }
        if (((phase == 0) && (pingPong == Phase0)) ||
            ((phase == 1) && (pingPong == Phase1)) ||
            (pingPong == PhaseBoth)                ||
            (valuesPerRead > 1)) {
            for (i=0; i<4; i++) {
                data[i] = (double)raw[i]/valuesPerRead;
            }         
            computePositions(data);
            numAcquired_++;
            if ((acquireMode == QEAcquireModeOneShot) &&
                (numAcquired_ >= numAverage)) {
                strcpy(outString_, "m 1");
                writeReadMeter();
                acquiring_ = 0;
            }
            if (acquireMode == QEAcquireModeContinuous) {
                triggerCallbacks();
            }
        }
    }
}

/** Starts and stops the electrometer.
  * \param[in] value 1 to start the electrometer, 0 to stop it.
  */
asynStatus drvNSLS_EM::setAcquire(epicsInt32 value) 
{
    //static const char *functionName = "setAcquire";

    // Return without doing anything if value=1 and already acquiring
    if ((value == 1) && (acquiring_)) return asynSuccess;
    
    if (value == 0) {
        // Setting this flag tells the read thread to stop
        acquiring_ = 0;
        // Wait for the read thread to stop
        while (readingActive_) {
            unlock();
            epicsThreadSleep(0.01);
            lock();
        }
        strcpy(outString_, "m 1");
        writeReadMeter();
    } else {
        setMode();
        // Notify the read thread if acquisition status has started
        epicsEventSignal(acquireStartEvent_);
        acquiring_ = 1;
    }
    return asynSuccess;
}

/** Set the acquisition mode
  */
asynStatus drvNSLS_EM::setMode()
{
    int pingPong;
    int mode;

    getIntegerParam(P_PingPong, &pingPong);
    mode = 0;
    if (pingPong != PhaseBoth) mode |= 0x80;
    // Send the mode command
    sprintf(outString_, "m %d", mode);
    writeReadMeter();
    return asynSuccess;
}

/** Sets the integration time. 
  * \param[in] value The integration time in seconds [0.001 - 1.000]
  */
asynStatus drvNSLS_EM::setIntegrationTime(epicsFloat64 value) 
{
    asynStatus status;
    
    /* Make sure the integration time is valid. If not change it and put back in parameter library */
    if (value < MIN_INTEGRATION_TIME) {
        value = MIN_INTEGRATION_TIME;
        setDoubleParam(P_IntegrationTime, value);
    }
    epicsSnprintf(outString_, sizeof(outString_), "p %d", (int)(value * 1e6));
    status = writeReadMeter();
    return status;
}

/** Sets the range 
  * \param[in] value The desired range.
  */
asynStatus drvNSLS_EM::setRange(epicsInt32 value) 
{
    asynStatus status;
    
    epicsSnprintf(outString_, sizeof(outString_), "r %d", value);
    status = writeReadMeter();
    return status;
}


/** Sets the values per read.
  * \param[in] value Values per read. Minimum depends on number of channels.
  */
asynStatus drvNSLS_EM::setValuesPerRead(epicsInt32 value) 
{
    asynStatus status;
    
    epicsSnprintf(outString_, sizeof(outString_), "n %d", value);
    status = writeReadMeter();
    return status;
}

 /** Sets the ping-pong setting. 
  * \param[in] value 0: use both ping and pong (HLF OFF), value=1: use just ping (HLF ON) 
  */
asynStatus drvNSLS_EM::setPingPong(epicsInt32 value) 
{
    return setMode();
}


/** Reads all the settings back from the electrometer.
  */
asynStatus drvNSLS_EM::readStatus() 
{
    // Reads the values of all the meter parameters, sets them in the parameter library
    int mode, valuesPerRead, range;
    double period;
    int numAverage;
    int pingPong;
    int numItems;
    double sampleTime, averagingTime;
    static const char *functionName = "getStatus";
    
    strcpy(outString_, "s");
    writeReadMeter();
    numItems = sscanf(inString_, "OK> \"ip: %s id: %d, ver: %s { m = %d, n = %d, r = %d, p = %lf }\"",
        ipAddress_, &moduleID_, firmwareVersion_, &mode, &valuesPerRead, &range, &period);
    if (numItems != 7) {  
        asynPrint(pasynUserSelf, ASYN_TRACE_ERROR, 
            "%s::%s errror, numItems=%d, ipAddress=%s, moduleID=%d, firmwareVersion=%s, mode=%d, "
            "valuesPerRead=%d, range=%d, period=%f\n",
            driverName, functionName, numItems, ipAddress_, moduleID_, firmwareVersion_, mode, 
            valuesPerRead, range, period);
        return asynError;
    }
    // The ipAddress has a trailing comma
    ipAddress_[strlen(ipAddress_)-1] = 0;
    setIntegerParam(P_Range, range);
    setIntegerParam(P_ValuesPerRead, valuesPerRead);
    period = period / 1.e6;
    setDoubleParam(P_IntegrationTime, period);
    sampleTime = period*valuesPerRead;
    getIntegerParam(P_PingPong, &pingPong);
    if (pingPong != PhaseBoth) sampleTime *= 2.0;
    setDoubleParam(P_SampleTime, sampleTime);
    setStringParam(P_Firmware, firmwareVersion_);

    // Compute the number of values that will be accumulated in the ring buffer before averaging
    getDoubleParam(P_AveragingTime, &averagingTime);
    numAverage = (int)((averagingTime / sampleTime) + 0.5);
    setIntegerParam(P_NumAverage, numAverage);

    return asynSuccess;
}

asynStatus drvNSLS_EM::reset()
{
    asynStatus status;
    //static const char *functionName = "reset";

    // Call the base class method
    status = drvQuadEM::reset();
    return status;
}


/** Exit handler.  Turns off acquire so we don't waste network bandwidth when the IOC stops */
void drvNSLS_EM::exitHandler()
{
    lock();
    setAcquire(0);
    unlock();
}

/** Report  parameters 
  * \param[in] fp The file pointer to write to
  * \param[in] details The level of detail requested
  */
void drvNSLS_EM::report(FILE *fp, int details)
{
    int i;
    
    fprintf(fp, "%s: port=%s\n", driverName, portName);
    if (details > 0) {
        fprintf(fp, "  IP address:       %s\n", ipAddress_);
        fprintf(fp, "  Module ID:        %d\n", moduleID_);
        fprintf(fp, "  Firmware version: %s\n", firmwareVersion_);
    }
    fprintf(fp, "  Number of modules found on network=%d\n", numModules_);
    for (i=0; i<numModules_; i++) {
        fprintf(fp, "    Module %d\n", i);
        fprintf(fp, "      Module ID:  %d\n", moduleInfo_[i].moduleID);
        fprintf(fp, "      IP address: %s\n", moduleInfo_[i].moduleIP);
    }
    drvQuadEM::report(fp, details);
}


/* Configuration routine.  Called directly, or from the iocsh function below */

extern "C" {

/** EPICS iocsh callable function to call constructor for the drvNSLS_EM class.
  * \param[in] portName The name of the asyn port driver to be created.
  * \param[in] broadcastAddress The broadcast address of the network with this module
  * \param[in] moduleID The module ID of this module, set with rotary switch on module
  * \param[in] ringBufferSize The number of samples to hold in the input ring buffer.
  *            This should be large enough to hold all the samples between reads of the
  *            device, e.g. 1 ms SampleTime and 1 second read rate = 1000 samples.
  *            If 0 then default of 2048 is used.
  */
int drvNSLS_EMConfigure(const char *portName, const char *broadcastAddress, int moduleID, int ringBufferSize)
{
    new drvNSLS_EM(portName, broadcastAddress, moduleID, ringBufferSize);
    return(asynSuccess);
}


/* EPICS iocsh shell commands */

static const iocshArg initArg0 = { "portName", iocshArgString};
static const iocshArg initArg1 = { "broadcast address", iocshArgString};
static const iocshArg initArg2 = { "module ID", iocshArgInt};
static const iocshArg initArg3 = { "ring buffer size",iocshArgInt};
static const iocshArg * const initArgs[] = {&initArg0,
                                            &initArg1,
                                            &initArg2,
                                            &initArg3};
static const iocshFuncDef initFuncDef = {"drvNSLS_EMConfigure",4,initArgs};
static void initCallFunc(const iocshArgBuf *args)
{
    drvNSLS_EMConfigure(args[0].sval, args[1].sval, args[2].ival, args[3].ival);
}

void drvNSLS_EMRegister(void)
{
    iocshRegister(&initFuncDef,initCallFunc);
}

epicsExportRegistrar(drvNSLS_EMRegister);

}

