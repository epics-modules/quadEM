/*
 * drvPCR4.cpp
 * 
 * Asyn driver that inherits from the drvQuadEM class to control 
 * the SenSiC PCR4 4-channel picoammeter
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
#include "drvPCR4.h"

#define PCR4_TIMEOUT 1
#define MIN_VALUES_PER_READ_ASCII 10
#define MAX_VALUES_PER_READ 52734
// Size of read buffer for binary data.  The max required for data is 40 bytes (4 doubles + NaN)
// We make it twice this size for doing synchonization so it is guaranteed to contain intact NaN

// Size of read buffer for ASCII data in units of char
#define ASCII_BUFFER_SIZE 150

static const char *driverName="drvPCR4";
static void readThread(void *drvPvt);

static const char *ranges_v1[] = {
    "+- 50 uA"
};

static const char *ranges_v2[] = {
    "+- 50 mA",
    "+- 250 uA",
    "+- 2.5 uA",
    "+- 25 nA"
};



/** Constructor for the drvPCR4 class.
  * Calls the constructor for the drvQuadEM base class.
  * \param[in] portName The name of the asyn port driver to be created.
  * \param[in] QEPortName The name of the asyn communication port to the PCR4 
  *            created with drvAsynIPPortConfigure or drvAsynSerialPortConfigure
  * \param[in] ringBufferSize The number of samples to hold in the input ring buffer.
  *            This should be large enough to hold all the samples between reads of the
  *            device, e.g. 1 ms SampleTime and 1 second read rate = 1000 samples.
  *            If 0 then default of 2048 is used.
  */
drvPCR4::drvPCR4(const char *portName, const char *QEPortName, int ringBufferSize) 
   : drvQuadEM(portName, ringBufferSize)
  
{
    asynStatus status;
    const char *functionName = "drvPCR4";
    
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
    setIntegerParam(P_Model, QE_ModelPCR4);
    setIntegerParam(P_ValuesPerRead, 5);

    // Do everything that needs to be done when connecting to the meter initially.
    // Note that the meter could be offline when the IOC starts, so we put this in
    // the reset() function which can be done later when the meter is online.
    lock();
    getFirmwareVersion();
//    drvQuadEM::reset();
    unlock();

    /* Create the thread that reads the meter */
    status = (asynStatus)(epicsThreadCreate("drvPCR4Task",
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
    drvPCR4 *pPvt = (drvPCR4 *)drvPvt;
    
    pPvt->readThread();
}

/** Sends a command to the PCR4 and reads the response.
  * If meter is acquiring turns it off and waits for it to stop sending data 
  * \param[in] expectACK True if the meter should respond with ACK to this command 
  */ 
asynStatus drvPCR4::sendCommand()
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


/** Writes a string to the PCR4 and reads the response. */
asynStatus drvPCR4::writeReadMeter()
{
  size_t nread;
  size_t nwrite;
  asynStatus status;
  int eomReason;
  static const char *functionName="writeReadMeter";
  
  status = pasynOctetSyncIO->writeRead(pasynUserMeter_, outString_, strlen(outString_), 
                                       inString_, sizeof(inString_), PCR4_TIMEOUT, 
                                       &nwrite, &nread, &eomReason);
  asynPrint(pasynUserSelf, ASYN_TRACEIO_DRIVER, 
      "%s::%s outString=\"%s\", inString=\"%s\", nwrite=%d, nread=%d, eomReason=%d, status=%d\n",
      driverName, functionName, outString_, inString_, (int)nwrite, (int)nread, eomReason, status);     
  return status;
}

/** Read thread to read the data from the electrometer when it is in continuous acquire mode.
  * Reads the data, computes the sums and positions, and does callbacks.
  */

void drvPCR4::readThread(void)
{
    asynStatus status;
    size_t nRead;
    // int bytesPerValue=8;
    // int readFormat;
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
    // char charData[BINARY_BUFFER_SIZE];
    // epicsFloat64 *f64Data = (epicsFloat64 *)charData;
    // unsigned long long *i64Data = (unsigned long long *)charData;
    // unsigned char *pc;
    // unsigned long long lastValue;
    char ASCIIData[ASCII_BUFFER_SIZE];
    epicsFloat64 *f64Data = (epicsFloat64 *)ASCIIData;
    char *inPtr;
    size_t nRequested;
    static const char *functionName = "readThread";

    /* Create an asynUser */
    pasynUser = pasynManager->createAsynUser(0, 0);
    pasynUser->timeout = PCR4_TIMEOUT;
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
            readingActive_ = 1;
        }
        // ASCII format
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
	
        if (strstr(ASCIIData, "TRGEVENTON") != 0) {
            // This is the rising edge of a trigger
            numTrigStarts++;
            if (nextExpectedEdge != 0) {
                asynPrint(pasynUserSelf, ASYN_TRACE_ERROR, 
                    "%s::%s Extra trigger start, numTrigStarts=%d, numTrigsEnds=%d\n", 
                     driverName, functionName, numTrigStarts, numTrigEnds);
            }
            nextExpectedEdge = 1;
        } 
        else if (strstr(ASCIIData, "TRGEVENTOFF") != 0) {
            // This is the falling edge of a trigger
            numTrigEnds++;
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
        callParamCallbacks();
    }
}

asynStatus drvPCR4::getFirmwareVersion()
{
    asynStatus status;
    //static const char *functionName = "getFirmwareVersion";

    strcpy(firmwareVersion_, "Unknown");
    strcpy(outString_, "VERSION:?");
    setAcquire(0);
    status = writeReadMeter();
    if (status == asynSuccess) {
        strcpy(firmwareVersion_, &inString_[8]);
        setStringParam(P_Firmware, firmwareVersion_);
        // Get the revision number "PCR4vx"
        char* pos = strstr(inString_, "PCR4v");
        versionNumber_ = atoi(pos + 5);
    }
    return status;
}


asynStatus drvPCR4::reset()
{
    asynStatus status;
    int i;
    int waitLoops=20;
    static const char *functionName = "reset";

    // Try to turn off meter.  This will set the output EOS which may be needed.
    setAcquire(0);
    strcpy(outString_, "RESET");
    status = sendCommand();
    // Wait for meter to start communicating or max of waitLoops seconds
    for (i=0; i<waitLoops; i++) {
        epicsThreadSleep(1.0);
        strcpy(outString_, "VERSION:?");
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
asynStatus drvPCR4::setAcquire(epicsInt32 value) 
{
    size_t nread;
    size_t nwrite;
    asynStatus status=asynSuccess;
    int eomReason;
    // int readFormat;
    char response[MAX_COMMAND_LEN];
    static const char *functionName = "setAcquire";
    int triggerMode;

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

	// STOP Trigger if active
	status = pasynOctetSyncIO->writeRead(pasynUserMeter_, "TRIGGER:STOP", strlen("TRIGGER:STOP"),
        response, sizeof(response), PCR4_TIMEOUT, &nwrite, &nread, &eomReason);
	
	// STOP Acquisition
	status = pasynOctetSyncIO->writeRead(pasynUserMeter_, "ACQC:STOP", strlen("ACQC:STOP"),
        response, sizeof(response), PCR4_TIMEOUT, &nwrite, &nread, &eomReason);
	if ((status != asynSuccess) || (nread != 3) || (strcmp(response, "ACK") != 0)) {
        while (1) {
               	// Read until the read terminated on EOS (\r\n) and last 3 characters of the response
               	// are ACK or until we get a timeout.
               	// A timeout should only happen if the ACK\r\n response spanned the response array si>
             	status = pasynOctetSyncIO->read(pasynUserMeter_, response, sizeof(response),
                                            PCR4_TIMEOUT, &nread, &eomReason);
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
	
	
	getIntegerParam(P_TriggerMode,&triggerMode);
    /*
	// Send the TRG:OFF or TRG:ON command
	if (triggerMode == 1){
    		status = pasynOctetSyncIO->writeRead(pasynUserMeter_, "TRIGGER:START", strlen("TRIGGER:START"),
        	response, sizeof(response), PCR4_TIMEOUT, &nwrite, &nread, &eomReason);
		asynPrint(pasynUserSelf, ASYN_TRACE_WARNING,
                "%s::%s TRIGGERMODE=1, nread=%d\n",
                driverName, functionName, (int)nread);
	}
	*/

        // Call the base class function in case anything needs to be done there.
        drvQuadEM::setAcquire(0);
    } else {
        // Call the base class function because it handles some common tasks.
        drvQuadEM::setAcquire(1);

        // It also has the effect of flusing any stale input
        setAcquireParams();
        getIntegerParam(P_TriggerMode,&triggerMode);
	if (triggerMode == 0) {
        	status = pasynOctetSyncIO->write(pasynUserMeter_, "ACQC:START", strlen("ACQC:START"), 
                            PCR4_TIMEOUT, &nwrite);
	}
        
        // Notify the read thread if acquisition status has started
        epicsEventSignal(acquireStartEvent_);
	acquiring_ = 1;

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


asynStatus drvPCR4::setAcquireParams()
{
    int numAverage;
    int acquireMode;
    int valuesPerRead;
    // int readFormat;
    // int naq;
    // int ntrg;
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
    
    getIntegerParam(P_Range,            &range);
    getIntegerParam(P_NumChannels,      &numChannels);
    getIntegerParam(P_TriggerMode,      &triggerMode);
    getIntegerParam(P_TriggerPolarity,  &triggerPolarity);
    getIntegerParam(P_AcquireMode,      &acquireMode);
    getIntegerParam(P_ValuesPerRead,    &valuesPerRead);
    getDoubleParam (P_AveragingTime,    &averagingTime);
    getIntegerParam(P_NumAcquire,       &numAcquire);

    if ((prevAcquiring) || (triggerMode)) setAcquire(0);

    // Compute the sample time.  This is 10 microseconds times valuesPerRead. 
    sampleTime = 19e-6 * valuesPerRead;
    setDoubleParam(P_SampleTime, sampleTime);

    // Compute the number of values that will be accumulated in the ring buffer before averaging
    numAverage = (int)((averagingTime / sampleTime) + 0.5);
    setIntegerParam(P_NumAverage, numAverage);

    epicsSnprintf(outString_, sizeof(outString_), "SETRANGE:%d", range);
    writeReadMeter();

    epicsSnprintf(outString_, sizeof(outString_), "SETCHANNELS:%d", numChannels);
    writeReadMeter();

    if (valuesPerRead > MAX_VALUES_PER_READ ) valuesPerRead = MAX_VALUES_PER_READ;
    if (valuesPerRead < MIN_VALUES_PER_READ_ASCII) valuesPerRead = MIN_VALUES_PER_READ_ASCII;

    // Send the NRSAMP command
    sprintf(outString_, "SPR:%d", valuesPerRead);
    writeReadMeter();

    // Send the TRGPOL:POS or TRGPOL:NEG command
    sprintf(outString_, "SETTRIGGER:%s", (triggerPolarity == QETriggerPolarityPositive) ? "RIS" : "FALL");
    writeReadMeter();

    // Send the TRG:OFF or TRG:ON command
    sprintf(outString_, "TRIGGER:%s", (triggerMode == 0) ? "STOP" : "START");
    writeReadMeter();
    if (triggerMode == 0) prevAcquiring = 0;
    
    if (prevAcquiring) setAcquire(1);

    return asynSuccess;
}


/** Sets the acquire mode.
  * \param[in] value Acquire mode.
  */
asynStatus drvPCR4::setAcquireMode(epicsInt32 value) 
{    
    return setAcquireParams();
}

/** Sets the averaging time.
  * \param[in] value Averaging time.
  */
asynStatus drvPCR4::setAveragingTime(epicsFloat64 value) 
{    
    return setAcquireParams();
}

/** Sets the bias state.
  * \param[in] value Bias state.
  */
asynStatus drvPCR4::setBiasState(epicsInt32 value) 
{
    asynStatus status;

    epicsSnprintf(outString_, sizeof(outString_), "BIAS:%s", value ? "ON" : "OFF");
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
asynStatus drvPCR4::setBiasVoltage(epicsFloat64 value) 
{
    asynStatus status;
    int biasState;
    
    getIntegerParam(P_BiasState, &biasState);
    // If the bias state is off, don't send the voltage because the meter returns an error.
    if (biasState == 0) return asynSuccess;
    epicsSnprintf(outString_, sizeof(outString_), "SETBIAS:%f", value);
    status = sendCommand();
    return status;
}

/** Sets the number of channels.
  * \param[in] value Number of channels to measure (1, 2, or 4).
  */
asynStatus drvPCR4::setNumChannels(epicsInt32 value) 
{    
    return setAcquireParams();
}

/** Sets the number of triggers.
  * \param[in] value Number of triggers. 
  */
asynStatus drvPCR4::setNumAcquire(epicsInt32 value) 
{    
    return setAcquireParams();
}

/** Sets the range 
  * \param[in] value The desired range.
  */
asynStatus drvPCR4::setRange(epicsInt32 value) 
{
    return setAcquireParams();
}

/** Sets the trigger mode
  * \param[in] value 0 = internal,
  *                  1 = external trigger (with predefined nr of samples)
  *                  2 = external gate.
  */
asynStatus drvPCR4::setTriggerMode(epicsInt32 value)
{
    return setAcquireParams();
}

/** Sets the trigger polarity
  * \param[in] value 0 = rising edge,
  *                  1 = falling edge
  */
asynStatus drvPCR4::setTriggerPolarity(epicsInt32 value)
{
    return setAcquireParams();
}

/** Sets the values per read.
  * \param[in] value Values per read. Minimum depends on number of channels.
  */
asynStatus drvPCR4::setValuesPerRead(epicsInt32 value) 
{    
    return setAcquireParams();
}

/** Reads all the settings back from the electrometer.
  */
asynStatus drvPCR4::readStatus() 
{
    // Reads the values of all the meter parameters, sets them in the parameter library
    int range, numChannels, numAverage, valuesPerRead, triggerMode;
    double biasVoltage;
    // unsigned long int unitStatus;
    int prevAcquiring;
    double sampleTime=0., averagingTime;
    static const char *functionName = "getStatus";
    
	
    getIntegerParam(P_TriggerMode, &triggerMode);
    prevAcquiring = acquiring_;
    if ((prevAcquiring) || (triggerMode)) setAcquire(0);

    strcpy(outString_, "RANGE:?");
    writeReadMeter();
    // Note: the PCR4 can return 4 ranges, but we only support a single range so we
    // only parse a single character in the response
    if (sscanf(inString_, "RANGE:%1d", &range) != 1) goto error;
    setIntegerParam(P_Range, range);
    
    strcpy(outString_, "CHANNELS:?");
    writeReadMeter();
    if (sscanf(inString_, "CHANNELS:%d", &numChannels) != 1) goto error;
    setIntegerParam(P_NumChannels, numChannels);
    numChannels_ = numChannels;

    strcpy(outString_, "SPR:?");
    writeReadMeter();
    if (sscanf(inString_, "SPR:%d", &valuesPerRead) != 1) goto error;
    setIntegerParam(P_ValuesPerRead, valuesPerRead);

    // Compute the sample time. 
    sampleTime = 19e-6 * valuesPerRead;
    setDoubleParam(P_SampleTime, sampleTime);

    strcpy(outString_, "BIASSTATUS:?");
    writeReadMeter();
    if (strncmp(inString_, "BIASSTATUS:OFF", 14) == 0) {
        setIntegerParam(P_HVSReadback, 0);
    } else {
        setIntegerParam(P_HVSReadback, 1);
        if (sscanf(inString_, "BIASSTATUS:%lf", &biasVoltage) != 1) goto error;
        setDoubleParam(P_BiasVoltage, biasVoltage);
    }

    // Compute the number of values that will be accumulated in the ring buffer before averaging
    getDoubleParam(P_AveragingTime, &averagingTime);
    if (triggerMode == QETriggerModeExtBulb) {
        numAverage = 0;
    } else {
        numAverage = (int)((averagingTime / sampleTime) + 0.5);
    }
    setIntegerParam(P_NumAverage, numAverage);

    if ((prevAcquiring) || (triggerMode)) setAcquire(1);
    
    return asynSuccess;

    error:
    asynPrint(pasynUserSelf, ASYN_TRACE_ERROR,
        "%s::%s: error, outString=%s, inString=%s\n",
        driverName, functionName, outString_, inString_);
    return asynError;
}

/** Exit handler.  Turns off acquire so we don't waste network bandwidth when the IOC stops */
void drvPCR4::exitHandler()
{
    lock();
    setAcquire(0);
    unlock();
}

/** Called when asyn clients call pasynEnum->read().
  * The base class implementation simply prints an error message.
  * Derived classes may reimplement this function if required.
  * \param[in] pasynUser pasynUser structure that encodes the reason and address.
  * \param[in] strings Array of string pointers.
  * \param[in] values Array of values
  * \param[in] severities Array of severities
  * \param[in] nElements Size of value array
  * \param[out] nIn Number of elements actually returned */
asynStatus drvPCR4::readEnum(asynUser *pasynUser,
        char *strings[], int values[], int severities[], size_t nElements, size_t *nIn)
{
    int function = pasynUser->reason;
    const char *functionName = "readEnum";

    if (function == P_Range)
    {
        const char **ranges = NULL;
        size_t numRanges = 0;
        switch (versionNumber_) {
            case 1:
                ranges = ranges_v1;
                numRanges = sizeof(ranges_v1)/sizeof(char*);
                break;
            case 2:
                ranges = ranges_v2;
                numRanges = sizeof(ranges_v2)/sizeof(char*);
                break;
            default:
                *nIn = 0;
                return asynError;
        }

        size_t i = 0;
        for (i=0; ((i<numRanges) && (i<nElements)); i++)
        {
            if (strings[i]) free(strings[i]);
            strings[i] = epicsStrDup(ranges[i]);
            asynPrint(this->pasynUserSelf, ASYN_TRACE_FLOW,
                    "%s:%s: Reading pass energy of %s\n",
                    driverName, functionName, strings[i]);
            values[i] = (int)i;
            severities[i] = 0;
        }
        *nIn = i;
    }
    else
    {
        *nIn = 0;
        return asynError;
    }
    return asynSuccess;
}

/** Report  parameters 
  * \param[in] fp The file pointer to write to
  * \param[in] details The level of detail requested
  */
void drvPCR4::report(FILE *fp, int details)
{
    int prevAcquiring = 0;

    fprintf(fp, "%s: port=%s, IP port=%s, firmware version=%s, numResync=%d\n",
            driverName, portName, QEPortName_, firmwareVersion_, numResync_);
    if (details > 0) {
        prevAcquiring = acquiring_;
        setAcquire(0);
        sprintf(outString_, "%s", "NETCONFIG");
        writeReadMeter();
        fprintf(fp, "NETCONFIG response from meter:\n");
        fprintf(fp, "%s\n", inString_);
    }   
    drvQuadEM::report(fp, details);
    if (prevAcquiring) setAcquire(1);
}


/* Configuration routine.  Called directly, or from the iocsh function below */

extern "C" {

/** EPICS iocsh callable function to call constructor for the drvPCR4 class.
  * \param[in] portName The name of the asyn port driver to be created.
  * \param[in] QEPortName The name of the asyn communication port to the PCR4 
  *            created with drvAsynIPPortConfigure or drvAsynSerialPortConfigure.
  * \param[in] ringBufferSize The number of samples to hold in the input ring buffer.
  *            This should be large enough to hold all the samples between reads of the
  *            device, e.g. 1 ms SampleTime and 1 second read rate = 1000 samples.
  *            If 0 then default of 2048 is used.
  */
int drvPCR4Configure(const char *portName, const char *QEPortName, int ringBufferSize)
{
    new drvPCR4(portName, QEPortName, ringBufferSize);
    return(asynSuccess);
}


/* EPICS iocsh shell commands */

static const iocshArg initArg0 = { "portName",iocshArgString};
static const iocshArg initArg1 = { "QEPortName",iocshArgString};
static const iocshArg initArg2 = { "ring buffer size",iocshArgInt};
static const iocshArg * const initArgs[] = {&initArg0,
                                            &initArg1,
                                            &initArg2};
static const iocshFuncDef initFuncDef = {"drvPCR4Configure",3,initArgs};
static void initCallFunc(const iocshArgBuf *args)
{
    drvPCR4Configure(args[0].sval, args[1].sval, args[2].ival);
}

void drvPCR4Register(void)
{
    iocshRegister(&initFuncDef,initCallFunc);
}

epicsExportRegistrar(drvPCR4Register);

}

