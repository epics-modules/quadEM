/*  drvAPS_EM.cpp

    Author: Mark Rivers
    Date: June 16, 2012 Mark Rivers Based on drvQuadEM.c
*/

#include <stdio.h>
#include <string.h>

#include <devLib.h>
#include <epicsTypes.h>
#include <epicsThread.h>
#include <epicsString.h>
#include <epicsMessageQueue.h>
#include <epicsPrint.h>
#include <iocsh.h>
#include <epicsExport.h>

#include "drvAPS_EM.h"

#define MAX_A24_ADDRESS  0xffffff
/* Maximum messages in epicsMessageQueue for interrupt routine */
#define MAX_MESSAGES 100

#define APS_EM_PULSE_VALUE 1024
#define APS_EM_PERIOD_VALUE 0xFFFF

/* First word of every command */
#define COMMAND1        0xa000 
/* Other commands */
#define RANGE_COMMAND   1
#define GO_COMMAND      4
#define CONV_COMMAND    5
#define PULSE_COMMAND   6
#define PERIOD_COMMAND  7
#define REBOOT_COMMAND 14

/* Conversion time in device units is limited to range 0x180 (384) to 0x2000 (8192) */
#define MIN_CONVERSION_TIME_VALUE 384
#define MAX_CONVERSION_TIME_VALUE 8192

static const char *driverName = "drvAPS_EM";

static void pollerThreadC(void *pPvt)
{
    drvAPS_EM *pdrvAPS_EM = (drvAPS_EM*)pPvt;;
    pdrvAPS_EM->pollerThread();
}

static void callbackFuncC(void *pPvt, asynUser *pasynUser, epicsUInt32 mask)
{
    drvAPS_EM *pdrvAPS_EM = (drvAPS_EM*)pPvt;;
    pdrvAPS_EM->callbackFunc(mask);
}

/** Constructor for the drvAPS_EM class.
  * Calls the constructor for the drvQuadEM base class.
  * \param[in] portName The name of the asyn port driver to be created.
  * \param[in] baseAddr A24 base address of the VME card
  * \param[in] fiberChannel Address [0-3] of the fiber channel connected to the electrometer
  * \param[in] unidigName Name of asynPort for the Ip-Unidig driver if it is being used to generate interrupts
  * \param[in] unidigChan Channel number [0-23] of the Ip-Unidig connected to the APS_EM pulse output, if Ip-Unidig is being used.
  * \param[in] unidigDrvInfo  The drvInfo field for the data callback of the ipUnidig driver. 
  *            If not specified then asynUser->reason=0 is used.
  * \param[in] ringBufferSize The number of samples to hold in the input ring buffer.
  *            This should be large enough to hold all the samples between reads of the
  *            device, e.g. 1 ms SampleTime and 1 second read rate = 1000 samples.
  *            If 0 then default of 2048 is used.
  */
drvAPS_EM::drvAPS_EM(const char *portName, unsigned short *baseAddr, int fiberChannel,
                     const char *unidigName, int unidigChan, char *unidigDrvInfo, int ringBufferSize)
   : drvQuadEM(portName, 0, ringBufferSize),
    unidigChan_(unidigChan),
    pUInt32DigitalPvt_(NULL),
    pUInt32RegistrarPvt_(NULL)
{
    asynInterface *pasynInterface;
    asynDrvUser *pdrvUser;
    unsigned long probeVal;
    epicsUInt32 mask;
    asynStatus status;
    static const char *functionName = "drvAPS_EM";
    
    readingsAveraged_ = 0;

    if ((unidigName != 0) && (strlen(unidigName) != 0) && (strcmp(unidigName, "0") != 0)) {
        /* Create asynUser */
        pUInt32DAsynUser_ = pasynManager->createAsynUser(0, 0);

        /* Connect to device */
        status = pasynManager->connectDevice(pUInt32DAsynUser_, 
                                             unidigName, unidigChan_);
        if (status != asynSuccess) {
            errlogPrintf("initQuadEM: connectDevice failed for ipUnidig\n");
            goto error;
        }

        /* Get the asynUInt32DigitalCallback interface */
        pasynInterface = pasynManager->findInterface(pUInt32DAsynUser_, 
                                                   asynUInt32DigitalType, 1);
        if (!pasynInterface) {
            errlogPrintf("%s:%s:, find asynUInt32Digital interface failed\n", driverName, functionName);
            goto error;
        }
        pUInt32Digital_ = (asynUInt32Digital *)pasynInterface->pinterface;
        pUInt32DigitalPvt_ = pasynInterface->drvPvt;
        
        /* If the drvInfo is specified then call drvUserCreate, else assume asynUser->reason=0 */
        if (unidigDrvInfo) {
            pasynInterface = pasynManager->findInterface(pUInt32DAsynUser_, 
                                                         asynDrvUserType, 1);
            if (!pasynInterface) {
                errlogPrintf("%s:%s:, find asynDrvUser interface failed\n", driverName, functionName);
            goto error;
            }
            pdrvUser = (asynDrvUser *)pasynInterface->pinterface;
            status = pdrvUser->create(pasynInterface->drvPvt, pUInt32DAsynUser_, unidigDrvInfo, NULL, 0);
            if (status != asynSuccess) {
                errlogPrintf("%s:%s: drvUser->create failed for ipUnidig\n", driverName, functionName);
                goto error;
            }
        } else {
            pUInt32DAsynUser_->reason = 0;
        }
    }
 
    if ((fiberChannel >= 4) || (fiberChannel < 0)) {
        errlogPrintf("%s:%s:: Invalid channel # %d \n", driverName, functionName, fiberChannel);
        goto error;
    }

    if (baseAddr >= (unsigned short *)MAX_A24_ADDRESS) {
        errlogPrintf("%s:%s:: Invalid Module Address %p \n", driverName, functionName, baseAddr);
        goto error;
    }

    /* The channel # goes in bits 5 and 6 */
    baseAddr = (unsigned short *)((int)baseAddr | ((fiberChannel << 5) & 0x60));
    if (devRegisterAddress("APS_EM", atVMEA24, (int)baseAddr, 16, 
                           (volatile void**)&baseAddress_) != 0) {
        baseAddress_ = NULL;
        errlogPrintf("%s:%s: A24 Address map failed\n", driverName, functionName);
        goto error;
    }

    if (devReadProbe(4, (char *)baseAddress_, (char *)&probeVal) != 0 ) {
        errlogPrintf("%s:%s:: devReadProbe failed for address %p\n", 
                     driverName, functionName, baseAddress_);
        baseAddress_ = NULL;
        goto error;
    }

    if (pUInt32DigitalPvt_ == NULL) {
        if (epicsThreadCreate("APS_EMPoller",
                              epicsThreadPriorityMedium,
                              epicsThreadGetStackSize(epicsThreadStackMedium),
                              (EPICSTHREADFUNC)pollerThreadC,
                              this) == NULL) {
            errlogPrintf("%s:%s: quadEMPoller epicsThreadCreate failure\n", driverName, functionName);
        goto error;
        }
    }
    else {
        /* Make sure interrupts are enabled on the falling edge of the 
         * quadEM output pulse */
        pUInt32Digital_->getInterrupt(pUInt32DigitalPvt_, 
                                     pUInt32DAsynUser_, &mask,
                                     interruptOnOneToZero);
        mask |= 1 << unidigChan_;
        pUInt32Digital_->setInterrupt(pUInt32DigitalPvt_, 
                                    pUInt32DAsynUser_, mask,
                                    interruptOnOneToZero);
    }
    
    /* Set the model */
    setIntegerParam(P_Model, QE_ModelAPS_EM);
    
    /* Set the range, ping-pong and integration time to reasonable defaults */
    setRange(0);
    setPingPong(0);
    setIntegrationTime(0.001); 

    /* Send the initial settings to the board to get it talking to the 
     * electometer. These settings will be overridden by the database values 
     * when the database initializes */
    reset();
    
    /* Calling readStatus() will compute the sampleTime, which must be done before iocInit
     * or fast feedback won't work. */
    readStatus();
    
    error:
    return;
}


/**  This function runs as a polling task at the system clock rate if there is no interrupt driver (such as Ip-Unidig) being used */
void drvAPS_EM::pollerThread()
{
    while(1) { /* Do forever */
        if (acquiring_) callbackFunc(0);
        epicsThreadSleep(epicsThreadSleepQuantum());
    }
}

/**  Callback function which is called either from drvAPS_EM::pollerThread or from the interrupt driver when a pulse is received from the VME card.
  * Calls drvAPS_EM::readMeter and then drvQuadEM::computePositions.
  * \param[in] mask The mask of bit status in the Ip-Unidig. We can get callbacks on both high-to-low and low-to-high transitions
  * of the pulse to the digital I/O board.  We only want to use one or the other.
  * mask is 0 if this was a high-to-low transition, which is the one we use. 
  */
void drvAPS_EM::callbackFunc(epicsUInt32 mask)
{
    int input[QE_MAX_INPUTS];
    int i;
    static const char *functionName="intFunc";

    asynPrint(pasynUserSelf, ASYN_TRACE_FLOW, 
              "%s:%s: got callback, mask=%x\n", driverName, functionName, mask);
    if (mask) return;
    lock();
    /* Read the new data */
    readMeter(input);
    if (readingsAveraged_ == 0) {
        for (i=0; i<QE_MAX_INPUTS; i++) {
            rawData_[i] = 0;
        }
    }
    for (i=0; i<QE_MAX_INPUTS; i++) {
        rawData_[i] += input[i];
    }
    readingsAveraged_++;
    if (readingsAveraged_ >= valuesPerRead_) {
        for (i=0; i<QE_MAX_INPUTS; i++) {
            rawData_[i] /= readingsAveraged_;
        }
        readingsAveraged_ = 0;
        /* Compute sum, difference, and position and do callbacks */
        computePositions(rawData_);
    }
    unlock();
}

/** Starts and stops the electrometer.
  * \param[in] value 1 to start the electrometer, 0 to stop it.
  */
asynStatus drvAPS_EM::setAcquire(int value)
{
    epicsUInt32 mask;

    if (pUInt32DigitalPvt_ != NULL) {
        mask = 1 << unidigChan_;
        if (value) {
            pUInt32Digital_->registerInterruptUser(pUInt32DigitalPvt_, 
                                                   pUInt32DAsynUser_, 
                                                   callbackFuncC, this, mask,
                                                   &pUInt32RegistrarPvt_);
        } else {
            if (pUInt32RegistrarPvt_ != NULL) {
                pUInt32Digital_->cancelInterruptUser(pUInt32DigitalPvt_, 
                                                     pUInt32DAsynUser_, 
                                                     pUInt32RegistrarPvt_);
            }
        }
    }
    acquiring_ = value;
    return asynSuccess;
}

/** Sets the ping-pong setting. 
  * \param[in] value 0=ping, 1=pong, 2=average
  */
asynStatus drvAPS_EM::setPingPong(int value)
{
    pingPong_ = (PingPongValue_t)value;
    return asynSuccess;
}

/** Sets the integration time. 
  * \param[in] value The integration time in seconds.
  */
asynStatus drvAPS_EM::setIntegrationTime(double value)
{
    double microSeconds = value * 1.e6;
    int convValue;
    double integrationTime;

    /* Convert from microseconds to device units */
    convValue = (int)((microSeconds - 0.6)/1.6 + 0.5);
    if (convValue < MIN_CONVERSION_TIME_VALUE) convValue = MIN_CONVERSION_TIME_VALUE;
    if (convValue > MAX_CONVERSION_TIME_VALUE) convValue = MAX_CONVERSION_TIME_VALUE;
    integrationTime = (convValue * 1.6 + 0.6)/1.e6;
    setDoubleParam(P_IntegrationTime, integrationTime);

    return writeMeter(CONV_COMMAND, convValue);
}

/** Sets the range. 
  * \param[in] value The range setting.
  */
asynStatus drvAPS_EM::setRange(int value)
{
    return writeMeter(RANGE_COMMAND, value);
}

/** Resets the electometer, rebooting the device, sleeping for 1 second, and then downloading all of the settings.
  */
asynStatus drvAPS_EM::reset()
{
    int range;
    double integrationTime;
    
    getIntegerParam(P_Range, &range);
    getDoubleParam(P_IntegrationTime, &integrationTime);
    writeMeter(REBOOT_COMMAND, 0);
    epicsThreadSleep(1.0);
    setRange(range);
    epicsThreadSleep(0.01);
    setPulse();
    epicsThreadSleep(0.01);
    setPeriod();
    epicsThreadSleep(0.01);
    setIntegrationTime(integrationTime);
    epicsThreadSleep(0.01);
    setGo();
    return asynSuccess;
}

/** Reads the settings back from the electrometer.
  * On the APS_EM this just computes the SampleTime based on the IntegrationTime and valuesPerRead. 
  */
asynStatus drvAPS_EM::readStatus() 
{
    double integrationTime, sampleTime, averagingTime;
    int numAverage;
    
    getDoubleParam(P_IntegrationTime, &integrationTime);
    /* If we are using the interrupts then this is the scan rate
     * except that we only get interrupts after every other cycle
     * because of ping/pong, so we multiply by 2. */
    if (pUInt32DigitalPvt_ != NULL) {
        sampleTime = 2. * integrationTime;
    } else {
        sampleTime = epicsThreadSleepQuantum();
    }
    sampleTime = sampleTime * valuesPerRead_;
    setDoubleParam(P_SampleTime, sampleTime);

    // Compute the number of values that will be accumulated in the ring buffer before averaging
    getDoubleParam(P_AveragingTime, &averagingTime);
    numAverage = (int)((averagingTime / sampleTime) + 0.5);
    setIntegerParam(P_NumAverage, numAverage);

    return asynSuccess;
}
  
/** Sends the pulse command to the electrometer. 
  */
asynStatus drvAPS_EM::setPulse()
{
    return writeMeter(PULSE_COMMAND, APS_EM_PULSE_VALUE);
}

/** Sends the period command to the electrometer. 
  */
asynStatus drvAPS_EM::setPeriod()
{
    /* For now we use a fixed period */
    return writeMeter(PERIOD_COMMAND, APS_EM_PERIOD_VALUE);
}

/** Sends the go command to the electrometer. 
  */
asynStatus drvAPS_EM::setGo()
{
    return writeMeter(GO_COMMAND, 1);
}

/** Reads the current values from the VME registers.  This function is called from drvAPS_EM::callbackFunc(). 
  */
asynStatus drvAPS_EM::readMeter(epicsInt32 raw[QE_MAX_INPUTS])
{
    int data[APS_EM_MAX_RAW];
    int i, j;
    
    data[0] = *(baseAddress_+0);
    data[1] = *(baseAddress_+2);
    data[2] = *(baseAddress_+4);
    data[3] = *(baseAddress_+6);
    /* Note that the following seems strange, but diode 4 comes before 
     * diode 3 in memory. */
    data[4] = *(baseAddress_+12);
    data[5] = *(baseAddress_+14);
    data[6] = *(baseAddress_+8);
    data[7] = *(baseAddress_+10);
    for (i=0, j=0; i<QE_MAX_INPUTS; i++, j=i*2) {
        switch(pingPong_) {
        case PingValue:
           raw[i] = data[j];
           break;
        case PongValue:
           raw[i] = data[j+1];
           break;
        case AverageValue:
           raw[i] = (data[j] + data[j+1])/2;
           break;
        }
    }
    return asynSuccess;
}

/** Sends a command to the electrometer
  * \param[in] command  The electrometer command
  * \param[in] value The command value 
  */
asynStatus drvAPS_EM::writeMeter(int command, int value)
{
    static const char *functionName = "writeMeter";
    
    *(baseAddress_+12) = (unsigned short)COMMAND1;
    *(baseAddress_+8)  = (unsigned short)command;
    *(baseAddress_+4)  = (unsigned short)value;
    *(baseAddress_+16) = (unsigned short)0;
    asynPrint(pasynUserSelf, ASYN_TRACEIO_DRIVER, 
              "%s:%s:, address=%p, command=%x value=%x\n", 
              driverName, functionName, baseAddress_, command, value);
    return asynSuccess;
}

/* asynCommon routines */

/** Report  parameters 
  * \param[in] fp The file pointer to write to
  * \param[in] details The level of detail requested
  */
void drvAPS_EM::report(FILE *fp, int details)
{
    fprintf(fp, "Port: %s, address %p\n", portName, baseAddress_);
    if (details >= 1) {
        if (pUInt32DigitalPvt_) {
           fprintf(fp, "    Using digital I/O interrupts\n");
        } else {
           fprintf(fp, "    Not using interrupts, scan time=%f\n", epicsThreadSleepQuantum());
        }
    }
    drvQuadEM::report(fp, details);
}

/* Configuration routine.  Called directly, or from the iocsh function below */
extern "C" {

/** EPICS iocsh callable function to call constructor for the drvAPS_EM class.
  * \param[in] portName The name of the asyn port driver to be created.
  * \param[in] baseAddr A24 base address of the VME card
  * \param[in] fiberChannel Address [0-3] of the fiber channel connected to the electrometer
  * \param[in] unidigName Name of asynPort for the Ip-Unidig driver if it is being used to generate interrupts
  * \param[in] unidigChan Channel number [0-23] of the Ip-Unidig connected to the APS_EM pulse output, if Ip-Unidig is being used.
  * \param[in] unidigDrvInfo  The drvInfo field for the data callback of the ipUnidig driver. 
  *            If not specified then asynUser->reason=0 is used.
  * \param[in] ringBufferSize The number of samples to hold in the input ring buffer.
  *            This should be large enough to hold all the samples between reads of the
  *            device, e.g. 1 ms SampleTime and 1 second read rate = 1000 samples.
  *            If 0 then default of 2048 is used.
  */ 
int drvAPS_EMConfigure(const char *portName, unsigned short *baseAddr, int fiberChannel,
                       const char *unidigName, int unidigChan, char *unidigDrvInfo, int ringBufferSize)
{
    new drvAPS_EM(portName, baseAddr, fiberChannel, unidigName, unidigChan, unidigDrvInfo, ringBufferSize);
    return(asynSuccess);
}


static const iocshArg initArg0 = { "name",iocshArgString};
static const iocshArg initArg1 = { "baseAddr",iocshArgInt};
static const iocshArg initArg2 = { "fiberChannel",iocshArgInt};
static const iocshArg initArg3 = { "unidigName",iocshArgString};
static const iocshArg initArg4 = { "unidigChan",iocshArgInt};
static const iocshArg initArg5 = { "unidigDrvInfo",iocshArgString};
static const iocshArg initArg6 = { "ring buffer size",iocshArgInt};
static const iocshArg * const initArgs[] =  {&initArg0,
                                             &initArg1,
                                             &initArg2,
                                             &initArg3,
                                             &initArg4,
                                             &initArg5,
                                             &initArg6};
static const iocshFuncDef initFuncDef = {"drvAPS_EMConfigure",7,initArgs};
static void initCallFunc(const iocshArgBuf *args)
{
    drvAPS_EMConfigure(args[0].sval,
                      (unsigned short*)args[1].ival, 
                      args[2].ival,
                      args[3].sval,
                      args[4].ival,
                      args[5].sval,
                      args[6].ival);
}

void drvAPS_EMRegister(void)
{
    iocshRegister(&initFuncDef,initCallFunc);
}

epicsExportRegistrar(drvAPS_EMRegister);
}

