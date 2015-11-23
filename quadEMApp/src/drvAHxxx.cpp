/*
 * drvAHxxx.h
 * 
 * Asyn driver that inherits from the asynPortDriver class to control the Elettra/CAENEls 4-channel picoammeters
 * This driver supports models 401B, 501, 501C, and 501D, but only the 401B, 501 and 501D have been tested.
 *
 * Author: Mark Rivers
 *
 * Created June 3, 2012
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
#include <asynOctetSyncIO.h>
#include <iocsh.h>

#include <epicsExport.h>
#include "drvAHxxx.h"

#define AHxxx_TIMEOUT 1.0
#define MIN_INTEGRATION_TIME 0.001
#define MAX_INTEGRATION_TIME 1.0

static const char *driverName="drvAHxxx";
static void readThread(void *drvPvt);


/** Constructor for the drvAHxxx class.
  * Calls the constructor for the drvQuadEM base class.
  * \param[in] portName The name of the asyn port driver to be created.
  * \param[in] QEPortName The name of the asyn communication port to the AHxxx 
  *            created with drvAsynIPPortConfigure or drvAsynSerialPortConfigure
  * \param[in] ringBufferSize The number of samples to hold in the input ring buffer.
  *            This should be large enough to hold all the samples between reads of the
  *            device, e.g. 1 ms SampleTime and 1 second read rate = 1000 samples.
  *            If 0 then default of 2048 is used.
  * \param[in] modelName The model of electrometer.  It is too difficult to try to determine
  *            this from the firmware version number, so it must be specified. Allowed values are:
  *            "AH401B", "AH401D", "AH501", "AH501C", and "AH501D".
  */
drvAHxxx::drvAHxxx(const char *portName, const char *QEPortName, int ringBufferSize, const char *modelName) 
   : drvQuadEM(portName, 0, ringBufferSize)
  
{
    asynStatus status;
    const char *functionName = "drvAHxxx";
    
    QEPortName_ = epicsStrDup(QEPortName);
    
    acquireStartEvent_ = epicsEventCreate(epicsEventEmpty);

    // Connect to the server
    status = pasynOctetSyncIO->connect(QEPortName, 0, &pasynUserMeter_, NULL);
    if (status) {
        printf("%s:%s: error calling pasynOctetSyncIO->connect, status=%d, error=%s\n", 
               driverName, functionName, status, pasynUserMeter_->errorMessage);
        return;
    }
    
    acquiring_ = 0;
    readingActive_ = 0;
    resolution_ = 24;

    model_ = QE_ModelUnknown;
    if      (strcmp(modelName, "AH401B") == 0) model_=QE_ModelAH401B;
    else if (strcmp(modelName, "AH401D") == 0) model_=QE_ModelAH401D;
    else if (strcmp(modelName, "AH501")  == 0) model_=QE_ModelAH501;
    else if (strcmp(modelName, "AH501C") == 0) model_=QE_ModelAH501C;
    else if (strcmp(modelName, "AH501D") == 0) model_=QE_ModelAH501D;
    setIntegerParam(P_Model, model_);

    // Do everything that needs to be done when connecting to the meter initially.
    // Note that the meter could be offline when the IOC starts, so we put this in
    // the reset() function which can be done later when the meter is online.
    lock();
    reset();
    unlock();

    /* Create the thread that reads the meter */
    status = (asynStatus)(epicsThreadCreate("drvAHxxxTask",
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
    drvAHxxx *pPvt = (drvAHxxx *)drvPvt;
    
    pPvt->readThread();
}

/** Sends a command to the AHxxx and reads the response.
  * If meter is acquiring turns it off and waits for it to stop sending data 
  * \param[in] expectACK True if the meter should respond with ACK to this command 
  */ 
asynStatus drvAHxxx::sendCommand()
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
          "%s:%s: error, outString=%s expected ACK, received %s\n",
          driverName, functionName, outString_, inString_);
      status = asynError;
      goto error;
  }
  if (prevAcquiring) setAcquire(1);
  error:
  return status;
}


/** Writes a string to the AHxxx and reads the response. */
asynStatus drvAHxxx::writeReadMeter()
{
  size_t nread;
  size_t nwrite;
  asynStatus status;
  int eomReason;
  //const char *functionName="writeReadMeter";
  
  status = pasynOctetSyncIO->writeRead(pasynUserMeter_, outString_, strlen(outString_), 
                                       inString_, sizeof(inString_), AHxxx_TIMEOUT, &nwrite, &nread, &eomReason);                                      
  return status;
}

/** Read thread to read the data from the electrometer when it is in continuous acquire mode.
  * Reads the data, computes the sums and positions, and does callbacks.
  */
void drvAHxxx::readThread(void)
{
    asynStatus status;
    int i, j;
    int offset;
    int value;
    size_t nRead;
    int numBytes;
    int eomReason;
    int acquireMode;
    int readFormat;
    int numAverage;
    asynUser *pasynUser;
    asynInterface *pasynInterface;
    asynOctet *pasynOctet;
    void *octetPvt;
    epicsFloat64 raw[QE_MAX_INPUTS];
    unsigned char *input=NULL;
    size_t inputSize=0;
    char ASCIIData[150];
    char *inPtr;
    size_t nRequested;
    size_t nExpected;
    static const char *functionName = "readThread";

    /* Create an asynUser */
    pasynUser = pasynManager->createAsynUser(0, 0);
    pasynUser->timeout = AHxxx_TIMEOUT;
    status = pasynManager->connectDevice(pasynUser, QEPortName_, 0);
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
            getIntegerParam(P_AcquireMode, &acquireMode);
            getIntegerParam(P_NumAverage, &numAverage);
            getIntegerParam(P_ReadFormat, &readFormat);
        }
        if (valuesPerRead_ < 1) valuesPerRead_ = 1;
        for (i=0; i<QE_MAX_INPUTS; i++) {
            raw[i] = 0;
        }

        if (readFormat == QEReadFormatBinary) {
            numBytes = 3;
            if (AH401Series_) {
                numChannels_ = 4;
            } 
            else {
                if (resolution_ == 16) numBytes = 2;
            }
            nRequested = numBytes * numChannels_ * valuesPerRead_;
            if (nRequested > inputSize) {
                if (input) free(input);
                input = (unsigned char *)malloc(nRequested);
                inputSize = nRequested;
            }
            unlock();
            pasynManager->lockPort(pasynUser);
            status = pasynOctet->read(octetPvt, pasynUser, (char *)input, nRequested, 
                                      &nRead, &eomReason);
            pasynManager->unlockPort(pasynUser);
            lock();
            if ((status != asynSuccess) || 
                (nRead  != nRequested)  || 
                (eomReason != ASYN_EOM_CNT)) {
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
            offset = 0;
            if (AH401Series_) {
                // These models are little-endian byte order
                for (i=0; i<valuesPerRead_; i++) {
                    for (j=0; j<numChannels_; j++) {
                        raw[j] += (input[offset+2]<<16) + (input[offset+1]<<8) + input[offset];
                        offset += numBytes;
                    }
                }
            }
            else {
                // These models are big-endian byte order
                if (resolution_ == 16) {
                    for (i=0; i<valuesPerRead_; i++) {
                        for (j=0; j<numChannels_; j++) {
                            value = (input[offset]<<8) + input[offset+1];
                            if (value <= 32767) {
                                value = -value;
                            } else {
                                value = 65536 - value;
                            }
                            raw[j] += value;
                            offset += numBytes;
                        }
                    }
                }
                else {
                    for (i=0; i<valuesPerRead_; i++) {
                        for (j=0; j<numChannels_; j++) {
                            value = (input[offset]<<16) + (input[offset+1]<<8) + input[offset+2];
                            if (value <= 8388607) {
                                value = -value;
                            } else {
                                value = 16777216 - value;
                            }
                            raw[j] += value;
                            offset += numBytes;
                        }
                    }
                }
            }
        }
        else {  // ASCII mode
            nRequested = sizeof(ASCIIData);
            for (i=0; i<valuesPerRead_; i++) {
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
                if (AH501Series_) {
                    nExpected = (resolution_/4)*numChannels_ + (numChannels_-1);
                    if (strstr(ASCIIData, "ACK") != 0) {
                        // The requested number of trigger samples has been received
                        break;
                    } 
                    if (nRead != nExpected) {
                        asynPrint(pasynUserSelf, ASYN_TRACE_ERROR, 
                            "%s:%s: error reading meter nRead=%lu, expected %lu, input=%s\n", 
                            driverName, functionName, (unsigned long)nRead, (unsigned long)nExpected, ASCIIData);
                        continue;
                    }
                    inPtr = ASCIIData;
                    for (j=0; j<numChannels_; j++) {
                        value = strtol(inPtr, &inPtr, 16);
                        if ((resolution_ == 24) && (value & 0x800000)) value |= ~0xffffff;
                        else if ((resolution_ == 16) && (value & 0x8000)) value |= ~0xffff;
                        raw[j] += value;
                    }
                }
                else if (AH401Series_) {
                    inPtr = ASCIIData;
                    for (j=0; j<numChannels_; j++) {
                        value = strtol(inPtr, &inPtr, 10);
                        raw[j] += value;
                    }
                }
            }  // end for valuesPerRead
        }  // end if ASCII mode

        if (valuesPerRead_ > 1) {
            for (i=0; i<numChannels_; i++) {
                raw[i] = raw[i] / valuesPerRead_;
            }
        }
        computePositions(raw);
        numAcquired_++;
        if ((acquireMode == QEAcquireModeOneShot) &&
            (numAcquired_ >= numAverage)) {
            acquiring_ = 0;
        }
    } //end while(1)
}

asynStatus drvAHxxx::reset()
{
    asynStatus status;
    static const char *functionName = "reset";

    setAcquire(0);    
    strcpy(firmwareVersion_, "Unknown");
    strcpy(outString_, "VER ?");
    status = writeReadMeter();
    strcpy(firmwareVersion_, inString_);
    setStringParam(P_Firmware, firmwareVersion_);
    // If the model is not already known from the constructor attempt to determine it
    // from the firmwareVersion_.
    if (model_ == QE_ModelUnknown) {
      if (strstr(firmwareVersion_,   "PicoNew") != 0) model_=QE_ModelAH401B;
      else if (strstr(firmwareVersion_, "401D") != 0) model_=QE_ModelAH401D;
      else if (strstr(firmwareVersion_, "501 ") != 0) model_=QE_ModelAH501;
      else if (strstr(firmwareVersion_, "501C") != 0) model_=QE_ModelAH501C;
      else if (strstr(firmwareVersion_, "501D") != 0) model_=QE_ModelAH501D;
      else {
          asynPrint(pasynUserSelf, ASYN_TRACE_ERROR, 
              "%s:%s: unknown firmware version = %s\n", 
              driverName, functionName, firmwareVersion_);
          return asynError;
      }
      setIntegerParam(P_Model, model_);
    }
    if ((model_ == QE_ModelAH401B) || (model_ == QE_ModelAH401D)) {
        AH401Series_ = true;
        AH501Series_ = false;
    } else {
        AH501Series_ = true;
        AH401Series_ = false;
    }
    // Call the base class method
    status = drvQuadEM::reset();
    return status;
}

    

/** Starts and stops the electrometer.
  * \param[in] value 1 to start the electrometer, 0 to stop it.
  */
asynStatus drvAHxxx::setAcquire(epicsInt32 value) 
{
    size_t nread;
    size_t nwrite;
    asynStatus status=asynSuccess, readStatus;
    int eomReason;
    int triggerMode;
    int numAverage;
    int numAcquire;
    int readFormat;
    int acquireMode;
    char dummyIn[MAX_COMMAND_LEN];
    static const char *functionName = "setAcquire";
    
    getIntegerParam(P_TriggerMode, &triggerMode);
    getIntegerParam(P_AcquireMode, &acquireMode);
    getIntegerParam(P_NumAverage,  &numAverage);
    getIntegerParam(P_ReadFormat,  &readFormat);

    // Make sure the input EOS is set
    status = pasynOctetSyncIO->setInputEos(pasynUserMeter_, "\r\n", 2);
    if (status) {
        asynPrint(pasynUserSelf, ASYN_TRACE_ERROR,
            "%s:%s: error calling pasynOctetSyncIO->setInputEos, status=%d\n",
            driverName, functionName, status);
        return asynError;
    }

    if (value == 0) {
        // We assume that if acquiring_=0, readingActive_=0 and acquireMode=one shot that the meter stopped itself
        // and we don't need to do anything further.  This really speeds things up.
        if ((acquiring_ == 0) && (readingActive_ == 0) && (acquireMode == QEAcquireModeOneShot)) 
            return asynSuccess;

        // Setting this flag tells the read thread to stop
        acquiring_ = 0;
        // Wait for the read thread to stop
        while (readingActive_) {
            unlock();
            epicsThreadSleep(0.1);
            lock();
        }
        while (1) {
            // Send stop command for both types of meters since initially we don't know which type we are talking to
            status = pasynOctetSyncIO->writeRead(pasynUserMeter_, "ACQ OFF", strlen("ACQ OFF"), 
                                             dummyIn, MAX_COMMAND_LEN, AHxxx_TIMEOUT, &nwrite, &nread, &eomReason);
            status = pasynOctetSyncIO->writeRead(pasynUserMeter_, "S", strlen("S"), 
                                             dummyIn, MAX_COMMAND_LEN, AHxxx_TIMEOUT, &nwrite, &nread, &eomReason);
            // Also send TRG OFF because we don't know what mode the meter might be in when we start
            status = pasynOctetSyncIO->writeRead(pasynUserMeter_, "TRG OFF", strlen("TRG OFF"), 
                                             dummyIn, MAX_COMMAND_LEN, AHxxx_TIMEOUT, &nwrite, &nread, &eomReason);
            if (status) {
                asynPrint(pasynUserSelf, ASYN_TRACE_ERROR,
                    "%s:%s: error calling pasynOctetSyncIO->writeRead, status=%d\n",
                    driverName, functionName, status);
                break;
            }
            // Now do flush and read with short timeout to flush any responses
            nread = 0;
            readStatus = pasynOctetSyncIO->flush(pasynUserMeter_);
            readStatus = pasynOctetSyncIO->read(pasynUserMeter_, dummyIn, MAX_COMMAND_LEN, .5, &nread, &eomReason);
            if ((readStatus == asynTimeout) && (nread == 0)) break;
        }
    } else {
        // Put the device in the appropriate mode
        if (readFormat == QEReadFormatBinary) {
            strcpy(outString_, "BIN ON");
        } else {
            strcpy(outString_, "BIN OFF");
        }
        writeReadMeter();
        
        // If we are in one-shot mode then send NAQ to request specific number of samples
        if (acquireMode == QEAcquireModeOneShot) {
            numAcquire = numAverage;
        } else {
            numAcquire = 0;
        }
        sprintf(outString_, "NAQ %d", numAcquire);
        writeReadMeter();

        // If we are in external trigger mode then send the TRG ON command
        if (triggerMode == QETriggerModeExtTrigger) {    
            status = pasynOctetSyncIO->writeRead(pasynUserMeter_, "TRG ON", strlen("TRG ON"), 
                        dummyIn, MAX_COMMAND_LEN, AHxxx_TIMEOUT, &nwrite, &nread, &eomReason);
        }
        // If not in external trigger mode send the ACQ ON command
        // The AH401 series sends an ACK after ACQ ON, AH501 series don't
        else if (AH401Series_) {
            status = pasynOctetSyncIO->writeRead(pasynUserMeter_, "ACQ ON", strlen("ACQ ON"), 
                        dummyIn, MAX_COMMAND_LEN, AHxxx_TIMEOUT, &nwrite, &nread, &eomReason);
        }
        else {
            status = pasynOctetSyncIO->write(pasynUserMeter_, "ACQ ON", strlen("ACQ ON"), 
                        AHxxx_TIMEOUT, &nwrite);
        }
        if (readFormat == QEReadFormatBinary) {
            status = pasynOctetSyncIO->setInputEos(pasynUserMeter_, "", 0);
        }
        // Notify the read thread if acquisition status has started
        epicsEventSignal(acquireStartEvent_);
        acquiring_ = 1;
    }
    if (status) {
        acquiring_ = 0;
    }
    return status;
}

 /** Sets the ping-pong setting. 
  * \param[in] value 0: use both ping and pong (HLF OFF), value=1: use just ping (HLF ON) 
  */
asynStatus drvAHxxx::setPingPong(epicsInt32 value) 
{
    asynStatus status;
    
    if (AH501Series_) return asynSuccess;
    epicsSnprintf(outString_, sizeof(outString_), "HLF %s", value ? "OFF" : "ON");
    status = sendCommand();
    return status;
}

/** Sets the integration time. 
  * \param[in] value The integration time in seconds [0.001 - 1.000]
  */
asynStatus drvAHxxx::setIntegrationTime(epicsFloat64 value) 
{
    asynStatus status;
    
    if (AH501Series_) return asynSuccess;
    /* Make sure the update time is valid. If not change it and put back in parameter library */
    if (value < MIN_INTEGRATION_TIME) {
        value = MIN_INTEGRATION_TIME;
        setDoubleParam(P_IntegrationTime, value);
    }
    epicsSnprintf(outString_, sizeof(outString_), "ITM %d", (int)(value * 10000));
    status = sendCommand();
    return status;
}

/** Sets the range 
  * \param[in] value The desired range.
  */
asynStatus drvAHxxx::setRange(epicsInt32 value) 
{
    asynStatus status;
    
    epicsSnprintf(outString_, sizeof(outString_), "RNG %d", value);
    status = sendCommand();
    return status;
}

/** Sets the number of channels.
  * \param[in] value Number of channels to measure (1, 2, or 4).
  */
asynStatus drvAHxxx::setNumChannels(epicsInt32 value) 
{
    asynStatus status;
    
    if (AH401Series_) return asynSuccess;
    epicsSnprintf(outString_, sizeof(outString_), "CHN %d", value);
    status = sendCommand();
    return status;
}

/** Sets the bias state.
  * \param[in] value Bias state.
  */
asynStatus drvAHxxx::setBiasState(epicsInt32 value) 
{
    asynStatus status;
    
    if (AH401Series_ || (model_ == QE_ModelAH501)) return asynSuccess;
    epicsSnprintf(outString_, sizeof(outString_), "HVS %s", value ? "ON" : "OFF");
    status = sendCommand();
    return status;
}

/** Sets the bias voltage.
  * \param[in] value Bias voltage in volts.
  */
asynStatus drvAHxxx::setBiasVoltage(epicsFloat64 value) 
{
    asynStatus status;
    
    if (AH401Series_ || (model_ == QE_ModelAH501)) return asynSuccess;
    epicsSnprintf(outString_, sizeof(outString_), "HVS %f", value);
    status = sendCommand();
    return status;
}

/** Sets the resolution.
  * \param[in] value Resolution in bits (16 or 24).
  */
asynStatus drvAHxxx::setResolution(epicsInt32 value) 
{
    asynStatus status;
    
    if (AH401Series_) return asynSuccess;
    epicsSnprintf(outString_, sizeof(outString_), "RES %d", value);
    status = sendCommand();
    return status;
}

/** Reads all the settings back from the electrometer.
  */
asynStatus drvAHxxx::readStatus() 
{
    // Reads the values of all the meter parameters, sets them in the parameter library
    int range, resolution, pingPong, numChannels, biasState, numAverage;
    double integrationTime, biasVoltage;
    int readFormat;
    int prevAcquiring;
    double sampleTime=0., averagingTime;
    static const char *functionName = "getStatus";
    
    getIntegerParam(P_ReadFormat,  &readFormat);
    prevAcquiring = acquiring_;
    if (prevAcquiring) setAcquire(0);

    strcpy(outString_, "RNG ?");
    writeReadMeter();
    // Note: the AH401D returns 2 ranges, but we only support a single range so we
    // only parse a single character in the response
    if (sscanf(inString_, "RNG %1d", &range) != 1) goto error;
    setIntegerParam(P_Range, range);
    
    if (AH401Series_) {
        strcpy(outString_, "HLF ?");
        writeReadMeter();
        if (strcmp("HLF ON", inString_) == 0) pingPong = 0;
        else if (strcmp("HLF OFF", inString_) == 0) pingPong = 1;
        else goto error;
        setIntegerParam(P_PingPong, pingPong);

        strcpy(outString_, "ITM ?");
        writeReadMeter();
        if (sscanf(inString_, "ITM %lf", &integrationTime) != 1) goto error;
        integrationTime = integrationTime/10000.;
        setDoubleParam(P_IntegrationTime, integrationTime);
        sampleTime = pingPong ? integrationTime : integrationTime*2.;
    }
    if (AH501Series_) {
        strcpy(outString_, "CHN ?");
        writeReadMeter();
        if (sscanf(inString_, "CHN %d", &numChannels) != 1) goto error;
        setIntegerParam(P_NumChannels, numChannels);
        numChannels_ = numChannels;

        strcpy(outString_, "RES ?");
        writeReadMeter();
        if (sscanf(inString_, "RES %d", &resolution) != 1) goto error;
        setIntegerParam(P_Resolution, resolution);
        resolution_ = resolution;

        // Compute the sample time.  This is a function of the resolution and number of channels
        sampleTime = 38.4e-6 * numChannels;
        if (resolution == 24) sampleTime = sampleTime * 2.;
        // Compute the sample time.  This is a function of the resolution and number of channels
        if (readFormat == QEReadFormatBinary) {
            sampleTime = 38.4e-6 * numChannels;
            if (resolution == 24) sampleTime = sampleTime * 2.0;
        }
        else {
            switch (resolution) {
                case 16:
                    switch (numChannels) {
	                      case 1:
                            sampleTime = 384e-6;
                            break;
	                      case 2:
	                          sampleTime = 806.4e-6;
                            break;
                        case 4:
	                          sampleTime = 1.6128e-3;
                            break;
                    }
                    break;
                case 24:
                    switch (numChannels) {
	                      case 1:
                            sampleTime = 499.2e-6;
                            break;
	                      case 2:
	                          sampleTime = 998.4e-6;
                            break;
                        case 4:
	                          sampleTime = 1.9968e-3;
                            break;
                    }
                    break;
            }
        }
    }
    if ((model_ == QE_ModelAH501C) || (model_ == QE_ModelAH501D)) {
        strcpy(outString_, "HVS ?");
        writeReadMeter();
        if (strcmp("HVS OFF", inString_) == 0) {
            biasState = 0;
        }
        else {
            biasState = 1;
            if (sscanf(inString_, "HVS %lf", &biasVoltage) != 1) goto error;
        }
        setIntegerParam(P_BiasState, biasState);
        if (biasState) setDoubleParam(P_BiasVoltage, biasVoltage);
    }
    
    // The sample times computed above don't include valuesPerRead
    sampleTime = sampleTime * valuesPerRead_;
    setDoubleParam(P_SampleTime, sampleTime);

    // Compute the number of values that will be accumulated in the ring buffer before averaging
    getDoubleParam(P_AveragingTime, &averagingTime);
    numAverage = (int)((averagingTime / sampleTime) + 0.5);
    setIntegerParam(P_NumAverage, numAverage);

    if (prevAcquiring) setAcquire(1);
    
    return asynSuccess;

    error:
    asynPrint(pasynUserSelf, ASYN_TRACE_ERROR,
        "%s:%s: error, outString=%s, inString=%s\n",
        driverName, functionName, outString_, inString_);
    return asynError;
}

/** Exit handler.  Turns off acquire so we don't waste network bandwidth when the IOC stops */
void drvAHxxx::exitHandler()
{
    lock();
    setAcquire(0);
    unlock();
}

/** Report  parameters 
  * \param[in] fp The file pointer to write to
  * \param[in] details The level of detail requested
  */
void drvAHxxx::report(FILE *fp, int details)
{
    fprintf(fp, "%s: port=%s, IP port=%s, firmware version=%s\n",
            driverName, portName, QEPortName_, firmwareVersion_);
    drvQuadEM::report(fp, details);
}


/* Configuration routine.  Called directly, or from the iocsh function below */

extern "C" {

/** EPICS iocsh callable function to call constructor for the drvAHxxx class.
  * \param[in] portName The name of the asyn port driver to be created.
  * \param[in] QEPortName The name of the asyn communication port to the AHxxx 
  *            created with drvAsynIPPortConfigure or drvAsynSerialPortConfigure.
  * \param[in] ringBufferSize The number of samples to hold in the input ring buffer.
  *            This should be large enough to hold all the samples between reads of the
  *            device, e.g. 1 ms SampleTime and 1 second read rate = 1000 samples.
  *            If 0 then default of 2048 is used.
  * \param[in] modelName The model of electrometer.  It is too difficult to try to determine
  *            this from the firmware version number, so it must be specified. Allowed values are:
  *            "AH401B", "AH401D", "AH501", "AH501C", and "AH501D".
  */
int drvAHxxxConfigure(const char *portName, const char *QEPortName, int ringBufferSize, const char *modelName)
{
    new drvAHxxx(portName, QEPortName, ringBufferSize, modelName);
    return(asynSuccess);
}


/* EPICS iocsh shell commands */

static const iocshArg initArg0 = { "portName",iocshArgString};
static const iocshArg initArg1 = { "QEPortName",iocshArgString};
static const iocshArg initArg2 = { "ring buffer size",iocshArgInt};
static const iocshArg initArg3 = { "model name",iocshArgString};
static const iocshArg * const initArgs[] = {&initArg0,
                                            &initArg1,
                                            &initArg2,
                                            &initArg3};
static const iocshFuncDef initFuncDef = {"drvAHxxxConfigure",4,initArgs};
static void initCallFunc(const iocshArgBuf *args)
{
    drvAHxxxConfigure(args[0].sval, args[1].sval, args[2].ival, args[3].sval);
}

void drvAHxxxRegister(void)
{
    iocshRegister(&initFuncDef,initCallFunc);
}

epicsExportRegistrar(drvAHxxxRegister);

}

