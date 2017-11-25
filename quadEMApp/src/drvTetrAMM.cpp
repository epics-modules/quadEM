/*
 * drvTetrAMM.cpp
 * 
 * Asyn driver that inherits from the drvQuadEM class to control 
 * the CaenEls TetrAMM 4-channel picoammeter
 *
 * Author: Mark Rivers
 *
 * Created July 14, 2015
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
#include <epicsTimer.h>
#include <epicsMutex.h>
#include <epicsEvent.h>
#include <epicsEndian.h>
#include <epicsMath.h>
#include <asynOctetSyncIO.h>
#include <iocsh.h>

#include <epicsExport.h>
#include "drvTetrAMM.h"

#define TetrAMM_TIMEOUT 0.05
#define MIN_VALUES_PER_READ_BINARY 5
#define MIN_VALUES_PER_READ_ASCII 500
#define MAX_VALUES_PER_READ 100000
// Size of read buffer for binary data.  The max required for data is 40 bytes (4 doubles + NaN)
// We make it twice this size for doing synchonization so it is guaranteed to contain intact NaN
#define BINARY_BUFFER_SIZE 80
// Size of read buffer for ASCII data in units of char
#define ASCII_BUFFER_SIZE 150

static const char *driverName="drvTetrAMM";
static void readThread(void *drvPvt);


/** Constructor for the drvTetrAMM class.
  * Calls the constructor for the drvQuadEM base class.
  * \param[in] portName The name of the asyn port driver to be created.
  * \param[in] QEPortName The name of the asyn communication port to the TetrAMM 
  *            created with drvAsynIPPortConfigure or drvAsynSerialPortConfigure
  * \param[in] ringBufferSize The number of samples to hold in the input ring buffer.
  *            This should be large enough to hold all the samples between reads of the
  *            device, e.g. 1 ms SampleTime and 1 second read rate = 1000 samples.
  *            If 0 then default of 2048 is used.
  */
drvTetrAMM::drvTetrAMM(const char *portName, const char *QEPortName, int ringBufferSize) 
   : drvQuadEM(portName, ringBufferSize)
  
{
    asynStatus status;
    const char *functionName = "drvTetrAMM";
    
    QEPortName_ = epicsStrDup(QEPortName);
    
    acquireStartEvent_ = epicsEventCreate(epicsEventEmpty);
    numResync_ = 0;

    // Connect to the server
    status = pasynOctetSyncIO->connect(QEPortName, 0, &pasynUserMeter_, NULL);
    if (status) {
        printf("%s::%s: error calling pasynOctetSyncIO->connect, status=%d, error=%s\n", 
               driverName, functionName, status, pasynUserMeter_->errorMessage);
        return;
    }
    
    acquiring_ = 0;
    readingActive_ = 0;
    resolution_ = 24;
    setIntegerParam(P_Model, QE_ModelTetrAMM);
    setIntegerParam(P_ValuesPerRead, 5);

    // Do everything that needs to be done when connecting to the meter initially.
    // Note that the meter could be offline when the IOC starts, so we put this in
    // the reset() function which can be done later when the meter is online.
    lock();
    getFirmwareVersion();
//    drvQuadEM::reset();
    unlock();

    /* Create the thread that reads the meter */
    status = (asynStatus)(epicsThreadCreate("drvTetrAMMTask",
                          epicsThreadPriorityMedium,
                          epicsThreadGetStackSize(epicsThreadStackMedium),
                          (EPICSTHREADFUNC)::readThread,
                          this) == NULL);
    if (status) {
        printf("%s::%s: epicsThreadCreate failure, status=%d\n", driverName, functionName, status);
        return;
    }
    callParamCallbacks();
}



static void readThread(void *drvPvt)
{
    drvTetrAMM *pPvt = (drvTetrAMM *)drvPvt;
    
    pPvt->readThread();
}

/** Sends a command to the TetrAMM and reads the response.
  * If meter is acquiring turns it off and waits for it to stop sending data 
  * \param[in] expectACK True if the meter should respond with ACK to this command 
  */ 
asynStatus drvTetrAMM::sendCommand()
{
  int prevAcquiring;
  asynStatus status;
  const char *functionName="sendCommand";
  
  prevAcquiring = acquiring_;
  if (prevAcquiring) setAcquire(0);
  status = writeReadMeter();
  if (status) goto error;
  if (strcmp(inString_, "ACK") != 0) {
      asynPrint(pasynUserSelf, ASYN_TRACE_ERROR, 
          "%s::%s: error, outString=%s expected ACK, received %s\n",
          driverName, functionName, outString_, inString_);
      status = asynError;
      goto error;
  }
  if (prevAcquiring) setAcquire(1);
  error:
  return status;
}


/** Writes a string to the TetrAMM and reads the response. */
asynStatus drvTetrAMM::writeReadMeter()
{
  size_t nread;
  size_t nwrite;
  asynStatus status;
  int eomReason;
  static const char *functionName="writeReadMeter";
  
  status = pasynOctetSyncIO->writeRead(pasynUserMeter_, outString_, strlen(outString_), 
                                       inString_, sizeof(inString_), TetrAMM_TIMEOUT, 
                                       &nwrite, &nread, &eomReason);
  asynPrint(pasynUserSelf, ASYN_TRACEIO_DRIVER, 
      "%s::%s outString=\"%s\", inString=\"%s\", nwrite=%d, nread=%d, eomReason=%d, status=%d\n",
      driverName, functionName, outString_, inString_, (int)nwrite, (int)nread, eomReason, status);     
  return status;
}

inline void swapDouble(char *in)
{
    char temp;
    temp = in[0]; in[0] = in[7]; in[7] = temp;
    temp = in[1]; in[1] = in[6]; in[6] = temp;
    temp = in[2]; in[2] = in[5]; in[5] = temp;
    temp = in[3]; in[3] = in[4]; in[4] = temp;
}

/** Read thread to read the data from the electrometer when it is in continuous acquire mode.
  * Reads the data, computes the sums and positions, and does callbacks.
  */

void drvTetrAMM::readThread(void)
{
    asynStatus status;
    size_t nRead;
    int bytesPerValue=8;
    int readFormat;
    int eomReason;
    int triggerMode;
    int nextExpectedEdge=0;
    int numTrigStarts=0;
    int numTrigEnds=0;
    int i;
    asynUser *pasynUser;
    asynInterface *pasynInterface;
    asynOctet *pasynOctet;
    void *octetPvt;
    char charData[BINARY_BUFFER_SIZE];
    epicsFloat64 *f64Data = (epicsFloat64 *)charData;
    long long *i64Data = (long long *)charData;
    unsigned char *pc;
    long long lastValue;
    char ASCIIData[ASCII_BUFFER_SIZE];
    char *inPtr;
    size_t nRequested;
    static const char *functionName = "readThread";

    /* Create an asynUser */
    pasynUser = pasynManager->createAsynUser(0, 0);
    pasynUser->timeout = TetrAMM_TIMEOUT;
    status = pasynManager->connectDevice(pasynUser, QEPortName_, 0);
    if(status!=asynSuccess) {
        asynPrint(pasynUserSelf, ASYN_TRACE_ERROR,
            "%s::%s: connectDevice failed, status=%d\n",
            driverName, functionName, status);
    }
    pasynInterface = pasynManager->findInterface(pasynUser, asynOctetType, 1);
    if(!pasynInterface) {;
        asynPrint(pasynUserSelf, ASYN_TRACE_ERROR,
            "%s::%s: findInterface failed for asynOctet, status=%d\n",
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
            acquiring_ = 1;
            numTrigEnds = 0;
            numTrigStarts = 0;
            nextExpectedEdge = 0;
            getIntegerParam(P_TriggerMode, &triggerMode);
            getIntegerParam(P_ReadFormat, &readFormat);
            readingActive_ = 1;
        }
        if (readFormat == QEReadFormatBinary) {
            nRequested = (numChannels_ + 1) * bytesPerValue;
            unlock();
            pasynManager->lockPort(pasynUser);
            status = pasynOctet->read(octetPvt, pasynUser, charData, nRequested, 
                                      &nRead, &eomReason);
            pasynManager->unlockPort(pasynUser);
            lock();

            if ((status != asynSuccess) || 
                (nRead  != nRequested)  || 
                (eomReason != ASYN_EOM_CNT)) {
                if (status == asynTimeout) {
                    asynPrint(pasynUserSelf, ASYN_TRACE_WARNING, 
                        "%s::%s: timeout reading meter\n", 
                        driverName, functionName);
                }
                else {
                    asynPrint(pasynUserSelf, ASYN_TRACE_ERROR, 
                        "%s::%s: unexpected error reading meter status=%d, nRead=%lu, eomReason=%d\n", 
                        driverName, functionName, status, (unsigned long)nRead, eomReason);
                    // We got an error reading the meter, it is probably offline.  
                    // Wait 1 second before trying again.
                    unlock();
                    epicsThreadSleep(1.0);
                    lock();
                }
                continue;
            }
            // If we are on a little-endian machine we need to swap the byte order
            if (EPICS_BYTE_ORDER == EPICS_ENDIAN_LITTLE) {
                for (i=0; i<=numChannels_; i++) swapDouble(charData + i*bytesPerValue);
            }
            lastValue = i64Data[numChannels_];
            switch(lastValue) {
                case 0xfff40002ffffffffll:
                    // This is a signalling Nan at the end of normal data
                    for (i=numChannels_; i<4; i++) f64Data[i] = 0.0;
                    computePositions(f64Data);
                   break;
                case 0xfff40000ffffffffll:
                    // This is a signalling Nan on the rising edge of a trigger
                    numTrigStarts++;
                    if (nextExpectedEdge != 0) {
                        asynPrint(pasynUserSelf, ASYN_TRACE_ERROR, 
                            "%s::%s Extra trigger start, numTrigStarts=%d, numTrigsEnds=%d\n", 
                             driverName, functionName, numTrigStarts, numTrigEnds);
                    }
                    nextExpectedEdge = 1;
                    break;
                case 0xfff40001ffffffffll:
                    // This is a signalling Nan on the falling edge of a trigger
                    numTrigEnds++;
                    if (triggerMode == QETriggerModeExtBulb) {
                        triggerCallbacks();
                    }
                    if (nextExpectedEdge != 1) {
                        asynPrint(pasynUserSelf, ASYN_TRACE_ERROR, 
                            "%s::%s Extra trigger end, numTrigStarts=%d, numTrigsEnds=%d\n", 
                             driverName, functionName, numTrigStarts, numTrigEnds);
                    }
                    nextExpectedEdge = 0;
                    break;
                case 0xfff40003ffffffffll:
                    // This is a signaling Nan when the acquistion was stopped
                    asynPrint(pasynUserSelf, ASYN_TRACEIO_DRIVER,
                            "%s::%s: seen acq done sNaN (0xfff40003ffffffffll)\n",
                            driverName, functionName);
                    break;
                default: 
                    // We have lost sync, probably due to a dropped packet.
                    // Recover sync by reading 2 times length per sample, which is enough to guarantee that
                    // it will contain a NaN unless we are losing many packets
                    asynPrint(pasynUserSelf, ASYN_TRACE_WARNING, 
                            "%s::%s: warning, lost sync, no NaN where expected; resynchronizing\n", 
                            driverName, functionName);
                    unlock();
                    pasynManager->lockPort(pasynUser);
                    nRequested = ((numChannels_ + 1) * bytesPerValue)*2;
                    status = pasynOctet->read(octetPvt, pasynUser, charData, nRequested, 
                                              &nRead, &eomReason);
                    for (i=0; i<(BINARY_BUFFER_SIZE-bytesPerValue); i++) {
                        pc = (unsigned char *)charData + i;
                        if (pc[0]==0xff && pc[1]==0xf4 && pc[2]==0x00 && pc[3]==0x02 &&
                            pc[4]==0xff && pc[5]==0xff && pc[6]==0xff && pc[7]==0xff) {
                            // We need to read (BINARY_BUFFER_SIZE - bytesPerValue - i) bytes
                            nRequested = nRequested - bytesPerValue - i;
                            status = pasynOctet->read(octetPvt, pasynUser, charData, nRequested, 
                                                      &nRead, &eomReason);
                            numResync_++;
                            asynPrint(pasynUserSelf, ASYN_TRACE_WARNING, 
                                "%s::%s: found NaN at position %d, read %d bytes\n", 
                                driverName, functionName, i, (int)nRequested);
                            break;
                         }
                    }
                    if (i == (BINARY_BUFFER_SIZE-bytesPerValue)) {
                        asynPrint(pasynUserSelf, ASYN_TRACE_ERROR, 
                            "%s::%s: ERROR, did not find NaN\n", 
                            driverName, functionName);
                        asynPrintIO(pasynUserSelf, ASYN_TRACE_ERROR, charData, nRead,
                            "%s::%s: buffer read when resynchronizing\n", 
                            driverName, functionName);
                    }
                    pasynManager->unlockPort(pasynUser);
                    lock();
                    break;
            }
        }
        else {  // ASCII format
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
                        "%s::%s: unexpected error reading meter status=%d, nRead=%lu, eomReason=%d\n", 
                        driverName, functionName, status, (unsigned long)nRead, eomReason);
                    // We got an error reading the meter, it is probably offline.  
                    // Wait 1 second before trying again.
                    unlock();
                    epicsThreadSleep(1.0);
                    lock();
                }
                continue;
            }

            if (strstr(ASCIIData, "SEQNR") != 0) {
                // This is the rising edge of a trigger
                numTrigStarts++;
                if (nextExpectedEdge != 0) {
                    asynPrint(pasynUserSelf, ASYN_TRACE_ERROR, 
                        "%s::%s Extra trigger start, numTrigStarts=%d, numTrigsEnds=%d\n", 
                         driverName, functionName, numTrigStarts, numTrigEnds);
                }
                nextExpectedEdge = 1;
            } 
            else if (strstr(ASCIIData, "EOTRG") != 0) {
                // This is the falling edge of a trigger
                numTrigEnds++;
                if (triggerMode == QETriggerModeExtBulb) {
                    triggerCallbacks();
                }
                if (nextExpectedEdge != 1) {
                    asynPrint(pasynUserSelf, ASYN_TRACE_ERROR, 
                        "%s::%s Extra trigger end, numTrigStarts=%d, numTrigsEnds=%d\n", 
                         driverName, functionName, numTrigStarts, numTrigEnds);
                }
                nextExpectedEdge = 0;
            }
            else {
                inPtr = ASCIIData;
                for (i=0; i<numChannels_; i++) {
                    f64Data[i] = strtod(inPtr, &inPtr);
                }
                for (i=numChannels_; i<4; i++) f64Data[i] = 0.0;
                computePositions(f64Data);
            }
        }
        callParamCallbacks();
    }
}

asynStatus drvTetrAMM::getFirmwareVersion()
{
    asynStatus status;
    //static const char *functionName = "getFirmwareVersion";

    strcpy(firmwareVersion_, "Unknown");
    strcpy(outString_, "VER:?");
    setAcquire(0);
    status = writeReadMeter();
    if (status == asynSuccess) {
        strcpy(firmwareVersion_, &inString_[4]);
        setStringParam(P_Firmware, firmwareVersion_);
    }
    return status;
}


asynStatus drvTetrAMM::reset()
{
    asynStatus status;
    int i;
    int waitLoops=20;
    static const char *functionName = "reset";

    // Try to turn off meter.  This will set the output EOS which may be needed.
    setAcquire(0);
    strcpy(outString_, "HWRESET");
    status = sendCommand();
    // Wait for meter to start communicating or max of waitLoops seconds
    for (i=0; i<waitLoops; i++) {
        epicsThreadSleep(1.0);
        strcpy(outString_, "VER:?");
        status = writeReadMeter();
        if (status == asynSuccess) break;
    }
    if (i == waitLoops) {
         asynPrint(pasynUserSelf, ASYN_TRACE_ERROR,
            "%s::%s: error, no response from meter after %d seconds\n",
            driverName, functionName, waitLoops);
    }
    // Call the base class method
    status = drvQuadEM::reset();
    return status;
}

    

/** Starts and stops the electrometer.
  * \param[in] value 1 to start the electrometer, 0 to stop it.
  */
asynStatus drvTetrAMM::setAcquire(epicsInt32 value) 
{
    size_t nread;
    size_t nwrite;
    asynStatus status=asynSuccess;
    int eomReason;
    int readFormat;
    char response[MAX_COMMAND_LEN];
    static const char *functionName = "setAcquire";

    // Return without doing anything if value=1 and already acquiring
    if ((value == 1) && (acquiring_)) return asynSuccess;
    
    // Make sure the input EOS is set
    status = pasynOctetSyncIO->setInputEos(pasynUserMeter_, "\r\n", 2);
    if (status) {
        asynPrint(pasynUserSelf, ASYN_TRACE_ERROR,
            "%s::%s: error calling pasynOctetSyncIO->setInputEos, status=%d\n",
            driverName, functionName, status);
        return asynError;
    }

    if (value == 0) {
        // We assume that if acquiring_=0, readingActive_=0 and acquireMode=one shot 
        // that the meter stopped itself and we don't need to do anything further.  
        // This really speeds things up.
//        if ((acquiring_ == 0) && (readingActive_ == 0) && (acquireMode == QEAcquireModeOneShot)) 
//            return asynSuccess;

        // Setting this flag tells the read thread to stop
        acquiring_ = 0;
        // Wait for the read thread to stop
        while (readingActive_) {
            unlock();
            epicsThreadSleep(0.01);
            lock();
        }
        status = pasynOctetSyncIO->writeRead(pasynUserMeter_, "ACQ:OFF", strlen("ACQ:OFF"), 
            response, sizeof(response), TetrAMM_TIMEOUT, &nwrite, &nread, &eomReason);
        if ((status != asynSuccess) || (nread != 3) || (strcmp(response, "ACK") != 0)) {
            while (1) {
                // Read until the read terminated on EOS (\r\n) and last 3 characters of the response
                // are ACK or until we get a timeout.  
                // A timeout should only happen if the ACK\r\n response spanned the response array size
                status = pasynOctetSyncIO->read(pasynUserMeter_, response, sizeof(response), 
                                                TetrAMM_TIMEOUT, &nread, &eomReason);
                if ((status == asynSuccess) && (eomReason == ASYN_EOM_EOS) &&
                    (nread >= 3) && (strncmp(&response[nread-3], "ACK", 3) == 0)) break;
                if (status != asynSuccess) {
                    asynPrint(pasynUserSelf, ASYN_TRACE_ERROR, 
                        "%s::%s error waiting for ACK response, status=%d\n",
                        driverName, functionName, status);
                    break;
                }
                asynPrint(pasynUserSelf, ASYN_TRACE_WARNING, 
                  "%s::%s waiting for ACK response, nread=%d\n",
                  driverName, functionName, (int)nread);
            }
        }
        // Call the base class function in case anything needs to be done there.
        drvQuadEM::setAcquire(0);
    } else {
        // Call the base class function because it handles some common tasks.
        drvQuadEM::setAcquire(1);

        // For now we call setAcquireParams().  This seems to be necessary when sending NAQ, and only takes 20 ms.
        // It also has the effect of flusing any stale input
        setAcquireParams();
        status = pasynOctetSyncIO->write(pasynUserMeter_, "ACQ:ON", strlen("ACQ:ON"), 
                            TetrAMM_TIMEOUT, &nwrite);
        getIntegerParam(P_ReadFormat, &readFormat);
        if (readFormat == QEReadFormatBinary) {
            status = pasynOctetSyncIO->setInputEos(pasynUserMeter_, "", 0);
        }
        // Notify the read thread if acquisition status has started
        epicsEventSignal(acquireStartEvent_);
        // Wait for the read thread to start
        while (!readingActive_) {
            unlock();
            epicsThreadSleep(0.01);
            lock();
        }
    }
    if (status) {
        acquiring_ = 0;
    }
    return status;
}


asynStatus drvTetrAMM::setAcquireParams()
{
    int numAverage;
    int acquireMode;
    int valuesPerRead;
    int readFormat;
    int naq;
    int ntrg;
    int range;
    int numChannels;
    int triggerMode;
    int triggerPolarity;
    double sampleTime;
    double averagingTime;
    int prevAcquiring;
    int numAcquire;
    //static const char *functionName = "setAcquireParams";

    prevAcquiring = acquiring_;
    if (prevAcquiring) setAcquire(0);

    getIntegerParam(P_Range,            &range);
    getIntegerParam(P_NumChannels,      &numChannels);
    getIntegerParam(P_TriggerMode,      &triggerMode);
    getIntegerParam(P_TriggerPolarity,  &triggerPolarity);
    getIntegerParam(P_AcquireMode,      &acquireMode);
    getIntegerParam(P_ValuesPerRead,    &valuesPerRead);
    getIntegerParam(P_ReadFormat,       &readFormat);
    getDoubleParam (P_AveragingTime,    &averagingTime);
    getIntegerParam(P_NumAcquire,       &numAcquire);

    // Compute the sample time.  This is 10 microseconds times valuesPerRead. 
    sampleTime = 10e-6 * valuesPerRead;
    setDoubleParam(P_SampleTime, sampleTime);

    // Compute the number of values that will be accumulated in the ring buffer before averaging
    if (triggerMode == QETriggerModeExtBulb) {
        numAverage = 0;
    } else {
        numAverage = (int)((averagingTime / sampleTime) + 0.5);
    }
    setIntegerParam(P_NumAverage, numAverage);

    epicsSnprintf(outString_, sizeof(outString_), "RNG:%d", range);
    writeReadMeter();

    epicsSnprintf(outString_, sizeof(outString_), "CHN:%d", numChannels);
    writeReadMeter();

    // Set the desired read format
    if (readFormat == QEReadFormatBinary) {
        strcpy(outString_, "ASCII:OFF");
    } else {
        strcpy(outString_, "ASCII:ON");
    }
    writeReadMeter();

    if (valuesPerRead > MAX_VALUES_PER_READ ) valuesPerRead = MAX_VALUES_PER_READ;
    if (readFormat == QEReadFormatBinary) {
        if (valuesPerRead < MIN_VALUES_PER_READ_BINARY) valuesPerRead = MIN_VALUES_PER_READ_BINARY;
    } else {
        if (valuesPerRead < MIN_VALUES_PER_READ_ASCII) valuesPerRead = MIN_VALUES_PER_READ_ASCII;
    }

    // Send the NRSAMP command
    sprintf(outString_, "NRSAMP:%d", valuesPerRead);
    writeReadMeter();

    // Send the TRG:OFF or TRG:ON command
    sprintf(outString_, "TRG:%s", (triggerMode == QETriggerModeFreeRun) ? "OFF" : "ON");
    writeReadMeter();

    // Send the TRGPOL:POS or TRGPOL:NEG command
    sprintf(outString_, "TRGPOL:%s", (triggerPolarity == QETriggerPolarityPositive) ? "POS" : "NEG");
    writeReadMeter();

    // Send the NAQ command
    naq = 0;
    if ((triggerMode == QETriggerModeExtTrigger) || 
       ((triggerMode == QETriggerModeFreeRun) && 
        (acquireMode == QEAcquireModeSingle))) {
        naq = numAverage;
    }        
    sprintf(outString_, "NAQ:%d", naq);
    writeReadMeter();
    
    // Send the NTRG command (NTRG command does not affect continuous mode)
    ntrg = 0;
    if (acquireMode == QEAcquireModeSingle) ntrg = 1;
    else if (acquireMode == QEAcquireModeMultiple) ntrg = numAcquire;
    sprintf(outString_, "NTRG:%d", ntrg);
    writeReadMeter();

    if (prevAcquiring) setAcquire(1);    
    return asynSuccess;
}


/** Sets the acquire mode.
  * \param[in] value Acquire mode.
  */
asynStatus drvTetrAMM::setAcquireMode(epicsInt32 value) 
{    
    return setAcquireParams();
}

/** Sets the averaging time.
  * \param[in] value Averaging time.
  */
asynStatus drvTetrAMM::setAveragingTime(epicsFloat64 value) 
{    
    return setAcquireParams();
}

/** Sets the bias state.
  * \param[in] value Bias state.
  */
asynStatus drvTetrAMM::setBiasState(epicsInt32 value) 
{
    asynStatus status;

    epicsSnprintf(outString_, sizeof(outString_), "HVS:%s", value ? "ON" : "OFF");
    status = sendCommand();
    // If turning the bias on then we also need to send the bias voltage, 
    // because it may not have been sent when the bias was off.
    if (value) {
        double biasVoltage;
        getDoubleParam(P_BiasVoltage, &biasVoltage);
        status = setBiasVoltage(biasVoltage);
    }
    return status;
}

/** Sets the bias voltage.
  * \param[in] value Bias voltage in volts.
  */
asynStatus drvTetrAMM::setBiasVoltage(epicsFloat64 value) 
{
    asynStatus status;
    int biasState;
    
    getIntegerParam(P_BiasState, &biasState);
    // If the bias state is off, don't send the voltage because the meter returns an error.
    if (biasState == 0) return asynSuccess;
    epicsSnprintf(outString_, sizeof(outString_), "HVS:%f", value);
    status = sendCommand();
    return status;
}

/** Sets the bias interlock.
  * \param[in] value Bias interlock off (0) or on (1).
  */
asynStatus drvTetrAMM::setBiasInterlock(epicsInt32 value) 
{
    asynStatus status;
    
    epicsSnprintf(outString_, sizeof(outString_), "INTERLOCK:%s",  value ? "ON" : "OFF");
    status = sendCommand();
    return status;
}

/** Sets the number of channels.
  * \param[in] value Number of channels to measure (1, 2, or 4).
  */
asynStatus drvTetrAMM::setNumChannels(epicsInt32 value) 
{    
    return setAcquireParams();
}

/** Sets the number of triggers.
  * \param[in] value Number of triggers. 
  */
asynStatus drvTetrAMM::setNumAcquire(epicsInt32 value) 
{    
    return setAcquireParams();
}

/** Sets the range 
  * \param[in] value The desired range.
  */
asynStatus drvTetrAMM::setRange(epicsInt32 value) 
{
    return setAcquireParams();
}

/** Sets the read format
  * \param[in] value Read format (QEReadFormatBinary or QEReadFormatASCII).
  */
asynStatus drvTetrAMM::setReadFormat(epicsInt32 value) 
{    
    return setAcquireParams();
}

/** Sets the trigger mode
  * \param[in] value 0 = internal,
  *                  1 = external trigger (with predefined nr of samples)
  *                  2 = external gate.
  */
asynStatus drvTetrAMM::setTriggerMode(epicsInt32 value)
{
    return setAcquireParams();
}

/** Sets the trigger polarity
  * \param[in] value 0 = rising edge,
  *                  1 = falling edge
  */
asynStatus drvTetrAMM::setTriggerPolarity(epicsInt32 value)
{
    return setAcquireParams();
}

/** Sets the values per read.
  * \param[in] value Values per read. Minimum depends on number of channels.
  */
asynStatus drvTetrAMM::setValuesPerRead(epicsInt32 value) 
{    
    return setAcquireParams();
}

/** Reads all the settings back from the electrometer.
  */
asynStatus drvTetrAMM::readStatus() 
{
    // Reads the values of all the meter parameters, sets them in the parameter library
    int range, numChannels, numAverage, valuesPerRead, triggerMode;
    double biasVoltage, voltageReadback, currentReadback, temperature;
    int prevAcquiring;
    double sampleTime=0., averagingTime;
    static const char *functionName = "getStatus";
    
    prevAcquiring = acquiring_;
    if (prevAcquiring) setAcquire(0);

    strcpy(outString_, "RNG:?");
    writeReadMeter();
    // Note: the TetrAMM can return 4 ranges, but we only support a single range so we
    // only parse a single character in the response
    if (sscanf(inString_, "RNG:%1d", &range) != 1) goto error;
    setIntegerParam(P_Range, range);
    
    strcpy(outString_, "CHN:?");
    writeReadMeter();
    if (sscanf(inString_, "CHN:%d", &numChannels) != 1) goto error;
    setIntegerParam(P_NumChannels, numChannels);
    numChannels_ = numChannels;

    strcpy(outString_, "NRSAMP:?");
    writeReadMeter();
    if (sscanf(inString_, "NRSAMP:%d", &valuesPerRead) != 1) goto error;
    setIntegerParam(P_ValuesPerRead, valuesPerRead);

    // Compute the sample time.  This is 10 microseconds times valuesPerRead. 
    sampleTime = 10e-6 * valuesPerRead;
    setDoubleParam(P_SampleTime, sampleTime);

    strcpy(outString_, "HVS:?");
    writeReadMeter();
    if (strncmp(inString_, "HVS:OFF", sizeof(inString_)) == 0) {
        setIntegerParam(P_HVSReadback, 0);
    } else {
        setIntegerParam(P_HVSReadback, 1);
        if (sscanf(inString_, "HVS:%lf", &biasVoltage) != 1) goto error;
        setDoubleParam(P_BiasVoltage, biasVoltage);
    }

    strcpy(outString_, "HVV:?");
    writeReadMeter();
    if (sscanf(inString_, "HVV:%lf", &voltageReadback) != 1) goto error;
    setDoubleParam(P_HVVReadback, voltageReadback);

    strcpy(outString_, "HVI:?");
    writeReadMeter();
    if (sscanf(inString_, "HVI:%lf", &currentReadback) != 1) goto error;
    setDoubleParam(P_HVIReadback, currentReadback);

    strcpy(outString_, "TEMP:?");
    writeReadMeter();
    if (sscanf(inString_, "TEMP:%lf", &temperature) != 1) goto error;
    setDoubleParam(P_Temperature, temperature);

    // Compute the number of values that will be accumulated in the ring buffer before averaging
    getDoubleParam(P_AveragingTime, &averagingTime);
    getIntegerParam(P_TriggerMode, &triggerMode);
    if (triggerMode == QETriggerModeExtBulb) {
        numAverage = 0;
    } else {
        numAverage = (int)((averagingTime / sampleTime) + 0.5);
    }
    setIntegerParam(P_NumAverage, numAverage);

    if (prevAcquiring) setAcquire(1);
    
    return asynSuccess;

    error:
    asynPrint(pasynUserSelf, ASYN_TRACE_ERROR,
        "%s::%s: error, outString=%s, inString=%s\n",
        driverName, functionName, outString_, inString_);
    return asynError;
}

/** Exit handler.  Turns off acquire so we don't waste network bandwidth when the IOC stops */
void drvTetrAMM::exitHandler()
{
    lock();
    setAcquire(0);
    unlock();
}

/** Report  parameters 
  * \param[in] fp The file pointer to write to
  * \param[in] details The level of detail requested
  */
void drvTetrAMM::report(FILE *fp, int details)
{
    int prevAcquiring = 0;

    fprintf(fp, "%s: port=%s, IP port=%s, firmware version=%s, numResync=%d\n",
            driverName, portName, QEPortName_, firmwareVersion_, numResync_);
    if (details > 0) {
        prevAcquiring = acquiring_;
        setAcquire(0);
        sprintf(outString_, "%s", "IFCONFIG");
        writeReadMeter();
        fprintf(fp, "IFCONFIG response from meter:\n");
        fprintf(fp, "%s\n", inString_);
        sprintf(outString_, "%s", "IFCONFIG:LINK");
        writeReadMeter();
        fprintf(fp, "IFCONFIG:LINK: response from meter:\n");
        fprintf(fp, "%s\n", inString_);
        sprintf(outString_, "%s", "IFCONFIG:ICMP");
        writeReadMeter();
        fprintf(fp, "IFCONFIG:ICMP: response from meter:\n");
        fprintf(fp, "%s\n", inString_);
    }   
    drvQuadEM::report(fp, details);
    if (prevAcquiring) setAcquire(1);
}


/* Configuration routine.  Called directly, or from the iocsh function below */

extern "C" {

/** EPICS iocsh callable function to call constructor for the drvTetrAMM class.
  * \param[in] portName The name of the asyn port driver to be created.
  * \param[in] QEPortName The name of the asyn communication port to the TetrAMM 
  *            created with drvAsynIPPortConfigure or drvAsynSerialPortConfigure.
  * \param[in] ringBufferSize The number of samples to hold in the input ring buffer.
  *            This should be large enough to hold all the samples between reads of the
  *            device, e.g. 1 ms SampleTime and 1 second read rate = 1000 samples.
  *            If 0 then default of 2048 is used.
  */
int drvTetrAMMConfigure(const char *portName, const char *QEPortName, int ringBufferSize)
{
    new drvTetrAMM(portName, QEPortName, ringBufferSize);
    return(asynSuccess);
}


/* EPICS iocsh shell commands */

static const iocshArg initArg0 = { "portName",iocshArgString};
static const iocshArg initArg1 = { "QEPortName",iocshArgString};
static const iocshArg initArg2 = { "ring buffer size",iocshArgInt};
static const iocshArg * const initArgs[] = {&initArg0,
                                            &initArg1,
                                            &initArg2};
static const iocshFuncDef initFuncDef = {"drvTetrAMMConfigure",3,initArgs};
static void initCallFunc(const iocshArgBuf *args)
{
    drvTetrAMMConfigure(args[0].sval, args[1].sval, args[2].ival);
}

void drvTetrAMMRegister(void)
{
    iocshRegister(&initFuncDef,initCallFunc);
}

epicsExportRegistrar(drvTetrAMMRegister);

}

