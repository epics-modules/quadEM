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

drvAPS_EM::drvAPS_EM(const char *portName, unsigned short *baseAddr, int fiberChannel,
                     const char *unidigName, int unidigChan, char *unidigDrvInfo)
   : drvQuadEM(portName, 0),
    unidigChan_(unidigChan),
    pUInt32RegistrarPvt_(NULL)
{
    asynInterface *pasynInterface;
    asynDrvUser *pdrvUser;
    unsigned long probeVal;
    epicsUInt32 mask;
    asynStatus status;
    static const char *functionName = "drvAPS_EM";
    
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

    /* Send the initial settings to the board to get it talking to the 
     * electometer. These settings will be overridden by the database values 
     * when the database initializes */
    setReset();
    
    error:
    return;
}


void drvAPS_EM::pollerThread()
/*  This functions runs as a polling task at the system clock rate if there is 
 *  no interrupts present */
{
    while(1) { /* Do forever */
        if (acquiring_) callbackFunc(0);
        epicsThreadSleep(epicsThreadSleepQuantum());
    }
}

void drvAPS_EM::callbackFunc(epicsUInt32 mask)
{
    int raw[QE_MAX_INPUTS];
    static const char *functionName="intFunc";

    /* We get callbacks on both high-to-low and low-to-high transitions
     * of the pulse to the digital I/O board.  We only want to use one or the other.
     * The mask parameter to this routine is 0 if this was a high-to-low 
     * transition.  Use that one. */
    asynPrint(pasynUserSelf, ASYN_TRACE_FLOW, 
              "%s:%s: got callback, mask=%x\n", driverName, functionName, mask);
    if (mask) return;
    /* Read the new data */
    readMeter(raw);
    /* Compute sum, difference, and position and do callbacks */
    computePositions(raw);
}

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

asynStatus drvAPS_EM::setPingPong(int value)
{
    pingPong_ = (PingPongValue_t)value;
    return asynSuccess;
}

asynStatus drvAPS_EM::setIntegrationTime(double seconds)
{
    double microSeconds = seconds * 1.e6;
    int convValue;
    double integrationTime;
    double sampleTime;

    /* Convert from microseconds to device units */
    convValue = (int)((microSeconds - 0.6)/1.6 + 0.5);
    if (convValue < MIN_CONVERSION_TIME_VALUE) convValue = MIN_CONVERSION_TIME_VALUE;
    if (convValue > MAX_CONVERSION_TIME_VALUE) convValue = MAX_CONVERSION_TIME_VALUE;
    integrationTime = (convValue * 1.6 + 0.6)/1.e6;
    setDoubleParam(P_IntegrationTime, integrationTime);
    /* If we are using the interrupts then this is the scan rate
     * except that we only get interrupts after every other cycle
     * because of ping/pong, so we multiply by 2. */
    if (pUInt32DigitalPvt_ != NULL) {
        sampleTime = 2. * integrationTime;
    } else {
        sampleTime = epicsThreadSleepQuantum();
    }
    setDoubleParam(P_SampleTime, sampleTime);

    return writeMeter(CONV_COMMAND, convValue);
}

asynStatus drvAPS_EM::setRange(int value)
{
    return writeMeter(RANGE_COMMAND, value);
}

asynStatus drvAPS_EM::setReset()
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

asynStatus drvAPS_EM::setTrigger(int value)
{
    // No trigger support on APS electrometer
    return asynSuccess;
}

asynStatus drvAPS_EM::getSettings() 
{
    // No settings readback on APS electrometer
    return asynSuccess;
}
  
asynStatus drvAPS_EM::setPulse()
{
    return writeMeter(PULSE_COMMAND, APS_EM_PULSE_VALUE);
}

asynStatus drvAPS_EM::setPeriod()
{
    /* For now we use a fixed period */
    return writeMeter(PERIOD_COMMAND, APS_EM_PERIOD_VALUE);
}

asynStatus drvAPS_EM::setGo()
{
    return writeMeter(GO_COMMAND, 1);
}


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

/* Report  parameters */
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
}

/* Configuration routine.  Called directly, or from the iocsh function below */
extern "C" {

/** EPICS iocsh callable function to call constructor for the drvAPS_EM class.
  * \param[in] portName The name of the asyn port driver to be created.
  * \param[in] baseAddr The A24 address of the VME card
  * \param[in] fiberChannel The fiber channel number (0-3)
  * \param[in] unidigName The IpUnidig (or other asyn digital I/O card) port driver name
  * \param[in] unidigName The IpUnidig (or other asyn digital I/O card) channel (bit) number 
  * \param[in] unidigName The IpUnidig (or other asyn digital I/O card) drvInfo string for data */ 
int drvAPS_EMConfigure(const char *portName, unsigned short *baseAddr, int fiberChannel,
                       const char *unidigName, int unidigChan, char *unidigDrvInfo)
{
    new drvAPS_EM(portName, baseAddr, fiberChannel, unidigName, unidigChan, unidigDrvInfo);
    return(asynSuccess);
}




static const iocshArg initArg0 = { "name",iocshArgString};
static const iocshArg initArg1 = { "baseAddr",iocshArgInt};
static const iocshArg initArg2 = { "fiberChannel",iocshArgInt};
static const iocshArg initArg3 = { "unidigName",iocshArgString};
static const iocshArg initArg4 = { "unidigChan",iocshArgInt};
static const iocshArg initArg5 = { "unidigDrvInfo",iocshArgString};
static const iocshArg * const initArgs[] =  {&initArg0,
                                             &initArg1,
                                             &initArg2,
                                             &initArg3,
                                             &initArg4,
                                             &initArg5};
static const iocshFuncDef initFuncDef = {"drvAPS_EMConfigure",6,initArgs};
static void initCallFunc(const iocshArgBuf *args)
{
    drvAPS_EMConfigure(args[0].sval,
                      (unsigned short*)args[1].ival, 
                      args[2].ival,
                      args[3].sval,
                      args[4].ival,
                      args[5].sval);
}

void drvAPS_EMRegister(void)
{
    iocshRegister(&initFuncDef,initCallFunc);
}

epicsExportRegistrar(drvAPS_EMRegister);
}

