/*  drvQuadEM.c

    Author: Mark Rivers
    Date: April 10, 2003, based on quadEM.cc
          June 28, 2003 MLR Converted to R3.14.2
          July 7, 2004  MLR Converted from MPF to asyn, and from C++ to C
*/

#include <stdio.h>
#include <string.h>

#include <devLib.h>
#include <cantProceed.h>
#include <epicsTypes.h>
#include <epicsThread.h>
#include <epicsString.h>
#include <epicsMessageQueue.h>
#include <epicsPrint.h>
#include <iocsh.h>
#include <epicsExport.h>

#include "asynDriver.h"
#include "asynInt32.h"
#include "asynFloat64.h"
#include "asynInt32Array.h"
#include "asynUInt32Digital.h"
#include "asynDrvUser.h"

#include "drvQuadEM.h"

typedef struct {
    quadEMCommand command;
    char *commandString;
} quadEMCommandStruct;

static quadEMCommandStruct quadEMCommands[MAX_QUADEM_COMMANDS] = {
    {quadEMCurrent,    "DATA"},
    {quadEMOffset,     "OFFSET"},
    {quadEMGain,       "GAIN"},
    {quadEMPeriod,     "PERIOD"},
    {quadEMPingPong,   "PING_PONG"},
    {quadEMPulse,      "PULSE"},
    {quadEMGo,         "GO"},
    {quadEMScanPeriod, "SCAN_PERIOD"}
};

#define MAX_A24_ADDRESS  0xffffff
#define MAX_RAW 8
/* Maximum messages in epicsMessageQueue for interrupt routine */
#define MAX_MESSAGES 100

/* First word of every command */
#define COMMAND1        0xa000 
/* Other commands */
#define RANGE_COMMAND   1
#define GO_COMMAND      4
#define CONV_COMMAND    5
#define PULSE_COMMAND   6
#define PERIOD_COMMAND  7

typedef enum{typeInt32, typeFloat64, typeInt32Array} dataType;

typedef struct {
    int raw[8];
    int current[4];
    int offset[4];
    int pingpong[4];
    int sum[2];
    int difference[2];
    int position[2];
    int array[10];
} quadEMData;

typedef struct {
    char *portName;
    unsigned short *baseAddress;
    asynUser *pasynUser;
    quadEMData data;
    epicsMessageQueueId intMsgQId;
    double actualSecondsPerScan;
    asynUInt32Digital *uint32Digital;
    void *uint32DigitalPvt;
    asynUser *puint32DAsynUser;
    asynInterface common;
    asynInterface int32;
    void *int32InterruptPvt;
    asynInterface float64;
    void *float64InterruptPvt;
    asynInterface int32Array;
    void *int32ArrayInterruptPvt;
    asynInterface drvUser;
} drvQuadEMPvt;

/* These functions are used by the interfaces */
static asynStatus readInt32         (void *drvPvt, asynUser *pasynUser,
                                     epicsInt32 *value);
static asynStatus writeInt32        (void *drvPvt, asynUser *pasynUser,
                                     epicsInt32 value);
static asynStatus getBounds         (void *drvPvt, asynUser *pasynUser,
                                     epicsInt32 *low, epicsInt32 *high);
static asynStatus readFloat64       (void *drvPvt, asynUser *pasynUser,
                                     epicsFloat64 *value);
static asynStatus writeFloat64      (void *drvPvt, asynUser *pasynUser,
                                     epicsFloat64 value);
static asynStatus drvUserCreate(void *drvPvt, asynUser *pasynUser,
                                const char *drvInfo, const char **pptypeName,
                                size_t *psize);
static asynStatus drvUserGetType(void *drvPvt, asynUser *pasynUser,
                                 const char **pptypeName, size_t *psize);
static asynStatus drvUserDestroy(void *drvPvt, asynUser *pasynUser);
static void report                  (void *drvPvt, FILE *fp, int details);
static asynStatus connect           (void *drvPvt, asynUser *pasynUser);
static asynStatus disconnect        (void *drvPvt, asynUser *pasynUser);

/* These are private functions, not used in any interfaces */
static double setScanPeriod         (void *drvPvt, asynUser *pasynUser,
                                     double seconds);
static double getScanPeriod         (void *drvPvt, asynUser *pasynUser);
static void setPeriod               (void *drvPvt, asynUser *pasynUser,
                                     int period);
static void setGain                 (void *drvPvt, asynUser *pasynUser, 
                                     int gain);
static void setPulse                (void *drvPvt, asynUser *pasynUser,
                                     int pulse);
static void go                      (void *drvPvt, asynUser *pasynUser);
static void write                   (void *drvPvt, asynUser *pasynUser,
                                     int command, int value);
static void read                    (void *drvPvt, asynUser *pasynUser,
                                     int *raw);
static void computePosition         (quadEMData *d);
static void computeCurrent          (quadEMData *d);
static void poller                  (drvQuadEMPvt *pPvt);  
                                    /* Polling routine if no interrupts */
static void intFunc                 (void *drvPvt, asynUser *pasynUser,
                                     unsigned int mask); 
                                     /* Interrupt function */
static void intTask                 (drvQuadEMPvt *pPvt);  
                                    /* Task that waits for interrupts */

static asynInt32 drvQuadEMInt32 = {
    writeInt32,
    readInt32,
    getBounds,
    NULL,
    NULL
};

static asynFloat64 drvQuadEMFloat64 = {
    writeFloat64,
    readFloat64,
    NULL,
    NULL
};

static asynFloat64 drvQuadEMInt32Array = {
    NULL,
    NULL,
    NULL,
    NULL
};

static asynDrvUser drvQuadEMDrvUser = {
    drvUserCreate,
    drvUserGetType,
    drvUserDestroy
};

/* asynCommon methods */
static struct asynCommon drvQuadEMCommon = {
    report,
    connect,
    disconnect
};


int initQuadEM(const char *portName, unsigned short *baseAddr, 
               int fiberChannel, int microSecondsPerScan, 
               const char *unidigName, int unidigChan)
{
    drvQuadEMPvt *pPvt;
    asynInterface *pasynInterface;
    unsigned long probeVal;
    epicsUInt32 mask;
    asynStatus status;
    void *registrarPvt;

    pPvt = callocMustSucceed(1, sizeof(*pPvt), "initQuadEM");
    pPvt->portName = epicsStrDup(portName);

    if ((unidigName != 0) && (strlen(unidigName) != 0) && (strcmp(unidigName, "0") != 0)) {
        /* Create asynUser */
        pPvt->puint32DAsynUser = pasynManager->createAsynUser(0, 0);

        /* Connect to device */
        status = pasynManager->connectDevice(pPvt->puint32DAsynUser, 
                                             unidigName, unidigChan);
        if (status != asynSuccess) {
            errlogPrintf("initQuadEM: connectDevice failed for uint32Digital\n");
            return -1;
        }

        /* Get the asynUInt32DigitalCallback interface */
        pasynInterface = pasynManager->findInterface(pPvt->puint32DAsynUser, 
                                                   asynUInt32DigitalType, 1);
        if (!pasynInterface) {
            errlogPrintf("initQuadEM, find asynUInt32Digital interface failed\n");
            return -1;
        }
        pPvt->uint32Digital = (asynUInt32Digital *)pasynInterface->pinterface;
        pPvt->uint32DigitalPvt = pasynInterface->drvPvt;
    }
 
    pPvt->intMsgQId = epicsMessageQueueCreate(MAX_MESSAGES, MAX_RAW*sizeof(int));
    if (epicsThreadCreate("quadEMintTask",
                           epicsThreadPriorityHigh,
                           epicsThreadGetStackSize(epicsThreadStackMedium),
                           (EPICSTHREADFUNC)intTask,
                           pPvt) == NULL)
       errlogPrintf("quadEMintTask epicsThreadCreate failure\n");

    if ((fiberChannel >= 4) || (fiberChannel < 0)) {
        errlogPrintf("initQuadEM: Invalid channel # %d \n", fiberChannel);
        return -1;
    }

    if (baseAddr >= (unsigned short *)MAX_A24_ADDRESS) {
        errlogPrintf("initQuadEM: Invalid Module Address %p \n", baseAddr);
        return -1;
    }

    /* The channel # goes in bits 5 and 6 */
    baseAddr = (unsigned short *)((int)baseAddr | ((fiberChannel << 5) & 0x60));
    if (devRegisterAddress("initQuadEM", atVMEA24, (int)baseAddr, 16, 
                           (volatile void**)&pPvt->baseAddress) != 0) {
        pPvt->baseAddress = NULL;
        errlogPrintf("initQquadEM: A24 Address map failed\n");
        return -1;
    }

    if (devReadProbe(4, (char *)pPvt->baseAddress, (char *)&probeVal) != 0 ) {
        errlogPrintf("initQuadEM: devReadProbe failed for address %p\n", 
                     pPvt->baseAddress);
        pPvt->baseAddress = NULL;
        return -1;
    }

    if (pPvt->uint32DigitalPvt == NULL) {
        if (epicsThreadCreate("quadEMPoller",
                              epicsThreadPriorityMedium,
                              epicsThreadGetStackSize(epicsThreadStackMedium),
                              (EPICSTHREADFUNC)poller,
                              pPvt) == NULL) {
            errlogPrintf("quadEMPoller epicsThreadCreate failure\n");
            return(-1);
        }
    }
    else {
        /* Make sure interrupts are enabled on the falling edge of the 
         * quadEM output pulse */
        pPvt->uint32Digital->getInterrupt(pPvt->uint32DigitalPvt, 
                                          pPvt->puint32DAsynUser, &mask,
                                          interruptOnOneToZero);
        mask |= 1 << unidigChan;
        pPvt->uint32Digital->setInterrupt(pPvt->uint32DigitalPvt, 
                                          pPvt->puint32DAsynUser, mask,
                                          interruptOnOneToZero);
        mask = 1 << unidigChan;
        pPvt->uint32Digital->registerInterruptUser(pPvt->uint32DigitalPvt, 
                                                   pPvt->puint32DAsynUser, 
                                                   intFunc, pPvt, mask,
                                                   &registrarPvt);
    }

    /* Link with higher level routines */
    pPvt->common.interfaceType = asynCommonType;
    pPvt->common.pinterface  = (void *)&drvQuadEMCommon;
    pPvt->common.drvPvt = pPvt;
    pPvt->int32.interfaceType = asynInt32Type;
    pPvt->int32.pinterface  = (void *)&drvQuadEMInt32;
    pPvt->int32.drvPvt = pPvt;
    pPvt->float64.interfaceType = asynFloat64Type;
    pPvt->float64.pinterface  = (void *)&drvQuadEMFloat64;
    pPvt->float64.drvPvt = pPvt;
    pPvt->int32Array.interfaceType = asynInt32ArrayType;
    pPvt->int32Array.pinterface  = (void *)&drvQuadEMInt32Array;
    pPvt->int32Array.drvPvt = pPvt;
    pPvt->drvUser.interfaceType = asynDrvUserType;
    pPvt->drvUser.pinterface  = (void *)&drvQuadEMDrvUser;
    pPvt->drvUser.drvPvt = pPvt;
    status = pasynManager->registerPort(portName,
                                   ASYN_MULTIDEVICE, /* cannot block */
                                   1,  /* autoconnect */
                                   0,  /* Medium priority */
                                   0); /* Default stack size */
    if (status != asynSuccess) {
        errlogPrintf("initQuadEM ERROR: Can't register port\n");
        return -1;
    }
    status = pasynManager->registerInterface(portName,&pPvt->common);
    if (status != asynSuccess) {
        errlogPrintf("initQuadEM ERROR: Can't register common.\n");
        return -1;
    }
    status = pasynInt32Base->initialize(pPvt->portName,&pPvt->int32);
    if (status != asynSuccess) {
        errlogPrintf("initQuadEM ERROR: Can't register int32\n");
        return -1;
    }
    pasynManager->registerInterruptSource(portName, &pPvt->int32,
                                          &pPvt->int32InterruptPvt);
    status = pasynFloat64Base->initialize(pPvt->portName,&pPvt->float64);
    if (status != asynSuccess) {
        errlogPrintf("initQuadEM ERROR: Can't register float64\n");
        return -1;
    }
    pasynManager->registerInterruptSource(portName, &pPvt->float64,
                                          &pPvt->float64InterruptPvt);
    status = pasynInt32ArrayBase->initialize(pPvt->portName, &pPvt->int32Array);
    if (status != asynSuccess) {
        errlogPrintf("initQuadEM ERROR: Can't register int32Array\n");
        return -1;
    }
    pasynManager->registerInterruptSource(portName, &pPvt->int32Array,
                                          &pPvt->int32ArrayInterruptPvt);
    status = pasynManager->registerInterface(pPvt->portName, &pPvt->drvUser);
    if (status != asynSuccess) {
        errlogPrintf("initQuadEM ERROR: Can't register drvUser\n");
        return -1;
    }

    /* Create asynUser for debugging */
    pPvt->pasynUser = pasynManager->createAsynUser(0, 0);

    /* Connect to device */
    status = pasynManager->connectDevice(pPvt->pasynUser, portName, 0);
    if (status != asynSuccess) {
        errlogPrintf("initQuadEM, connectDevice failed for quadEM\n");
        return -1;
    }
 
    /* Send the initial settings to the board to get it talking to the 
     * electometer. These settings will be overridden by the database values 
     * when the database initializes */
    setGain(pPvt, pPvt->pasynUser, 0);
    setPulse(pPvt, pPvt->pasynUser, 1024);
    setPeriod(pPvt, pPvt->pasynUser, 0xffff);
    setScanPeriod(pPvt, pPvt->pasynUser, microSecondsPerScan/1.e6);
    go(pPvt, pPvt->pasynUser);

    return(0);
}


static void poller(drvQuadEMPvt *pPvt)
/*  This functions runs as a polling task at the system clock rate if there is 
 *  no interrupts present */
{
    while(1) { /* Do forever */
        intFunc(pPvt, pPvt->pasynUser, 0);
        epicsThreadSleep(epicsThreadSleepQuantum());
    }
}

static asynStatus drvUserCreate(void *drvPvt, asynUser *pasynUser,
                                const char *drvInfo, const char **pptypeName,
                                size_t *psize)
{
    int i;
    char *pstring;

    for (i=0; i<MAX_QUADEM_COMMANDS; i++) {
        pstring = quadEMCommands[i].commandString;
        if (epicsStrCaseCmp(drvInfo, pstring) == 0) {
            pasynUser->reason = quadEMCommands[i].command;
            if (pptypeName) *pptypeName = epicsStrDup(pstring);
            if (psize) *psize = sizeof(quadEMCommands[i].command);
            return(asynSuccess);
        }
    }
    epicsSnprintf(pasynUser->errorMessage, pasynUser->errorMessageSize,
                  "drvQuadEM::drvUserCreate unknown drvInfo string = %s",
                  drvInfo);
    return(asynError);
}

static asynStatus drvUserGetType(void *drvPvt, asynUser *pasynUser,
                                 const char **pptypeName, size_t *psize)
{
    quadEMCommand command = pasynUser->reason;

    if (pptypeName) *pptypeName = epicsStrDup(
                                      quadEMCommands[command].commandString);
    if (psize) *psize = sizeof(command);
    return(asynSuccess);
}

static asynStatus drvUserDestroy(void *drvPvt,asynUser *pasynUser)
{
    return(asynSuccess);
}


static void intFunc(void *drvPvt, asynUser *pasynUser, epicsUInt32 mask)
{
    drvQuadEMPvt *pPvt = (drvQuadEMPvt *)drvPvt;
    int raw[MAX_RAW];

    /* We get callbacks on both high-to-low and low-to-high transitions
     * of the pulse to the digital I/O board.  We only want to use one or the other.
     * The mask parameter to this routine is 0 if this was a high-to-low 
     * transition.  Use that one. */
    asynPrint(pPvt->pasynUser, ASYN_TRACE_FLOW, 
              "drvQuadEM::intFunc got callback, mask=%x\n", mask);
    if (mask) return;
    /* Read the new data */
    read(pPvt, pPvt->pasynUser, raw);
    /* Send a message to the intTask task, it handles the rest, not running 
     * at interrupt level */
    epicsMessageQueueTrySend(pPvt->intMsgQId, raw, sizeof(raw));
}

static void intTask(drvQuadEMPvt *pPvt)
{
    int i;
    int raw[MAX_RAW];
    int addr;
    int reason;
    ELLLIST *pclientList;
    interruptNode *pnode;
    asynInt32Interrupt *pint32Interrupt;
    asynFloat64Interrupt *pfloat64Interrupt;
    asynInt32ArrayInterrupt *pint32ArrayInterrupt;

    while(1) {
        /* Wait for a message from interrupt routine */
        epicsMessageQueueReceive(pPvt->intMsgQId, raw, sizeof(raw));
        /* Copy raw data to private structure */
        for (i=0; i<MAX_RAW; i++) pPvt->data.raw[i] = raw[i];
        asynPrint(pPvt->pasynUser, ASYN_TRACEIO_DRIVER, 
                  "drvQuadEM::intTask, address=%p, "
                  "raw = %d %d %d %d %d %d %d %d\n", 
                  pPvt->baseAddress, 
                  raw[0], raw[1], raw[2], raw[3], raw[4], 
                  raw[5], raw[6], raw[7]);
        /* Compute sum, difference, and position */
        computeCurrent(&pPvt->data);
        computePosition(&pPvt->data);
        /* Pass int32 interrupts */
        pasynManager->interruptStart(pPvt->int32InterruptPvt, &pclientList);
        pnode = (interruptNode *)ellFirst(pclientList);
        while (pnode) {
            pint32Interrupt = pnode->drvPvt;
            addr = pint32Interrupt->addr;
            reason = pint32Interrupt->pasynUser->reason;
            if (reason == quadEMCurrent) {
                pint32Interrupt->callback(pint32Interrupt->userPvt,
                                          pint32Interrupt->pasynUser,
                                          pPvt->data.array[addr]);
            }
            pnode = (interruptNode *)ellNext(&pnode->node);
        }
        pasynManager->interruptEnd(pPvt->int32InterruptPvt);

        /* Pass float64 interrupts */
        pasynManager->interruptStart(pPvt->float64InterruptPvt, &pclientList);
        pnode = (interruptNode *)ellFirst(pclientList);
        while (pnode) {
            pfloat64Interrupt = pnode->drvPvt;
            addr = pfloat64Interrupt->addr;
            reason = pfloat64Interrupt->pasynUser->reason;
            if (reason == quadEMCurrent) {
                pfloat64Interrupt->callback(pfloat64Interrupt->userPvt,
                                            pfloat64Interrupt->pasynUser,
                                            (double)pPvt->data.array[addr]);
            }
            pnode = (interruptNode *)ellNext(&pnode->node);
        }
        pasynManager->interruptEnd(pPvt->float64InterruptPvt);

        /* Pass int32Array interrupts */
        pasynManager->interruptStart(pPvt->int32ArrayInterruptPvt, &pclientList);
        pnode = (interruptNode *)ellFirst(pclientList);
        while (pnode) {
            pint32ArrayInterrupt = pnode->drvPvt;
            reason = pint32ArrayInterrupt->pasynUser->reason;
            if (reason == quadEMCurrent) {
                pint32ArrayInterrupt->callback(pint32ArrayInterrupt->userPvt,
                                               pint32ArrayInterrupt->pasynUser,
                                               pPvt->data.array, 10);
            }
            pnode = (interruptNode *)ellNext(&pnode->node);
        }
        pasynManager->interruptEnd(pPvt->int32ArrayInterruptPvt);
    }
}

static double setScanPeriod(void *drvPvt, asynUser *pasynUser,
                            double seconds)
{
    double microSeconds = seconds * 1.e6;
    int reason;
    ELLLIST *pclientList;
    interruptNode *pnode;
    asynFloat64Interrupt *pfloat64Interrupt;

    drvQuadEMPvt *pPvt = (drvQuadEMPvt *)drvPvt;
    /* Convert from microseconds to device units */
    int convValue = (int)((microSeconds - 0.6)/1.6);

    write(pPvt, pasynUser, CONV_COMMAND, convValue);
    /* If we are using the interrupts then this is the scan rate
     * except that we only get interrupts after every other cycle
     * because of ping/pong, so we multiply by 2. */
    if (pPvt->uint32DigitalPvt != NULL) {
        microSeconds = 2. * convValue * 1.6 + 0.6;
        pPvt->actualSecondsPerScan = microSeconds/1.e6;
    } else {
        pPvt->actualSecondsPerScan = epicsThreadSleepQuantum();
    }
    /* Call the callback routines which have registered to be notified when
       the scan period changes */
    pasynManager->interruptStart(pPvt->float64InterruptPvt, &pclientList);
    pnode = (interruptNode *)ellFirst(pclientList);
    while (pnode) {
        pfloat64Interrupt = pnode->drvPvt;
        reason = pfloat64Interrupt->pasynUser->reason;
            if (reason == quadEMScanPeriod) {
                pfloat64Interrupt->callback(pfloat64Interrupt->userPvt,
                                            pfloat64Interrupt->pasynUser,
                                            pPvt->actualSecondsPerScan);
            }
            pnode = (interruptNode *)ellNext(&pnode->node);
        }
    pasynManager->interruptEnd(pPvt->float64InterruptPvt);
    return(getScanPeriod(pPvt, pasynUser));
}

/* Note: we cache actualSecondsPerScan for efficiency.
 * It is important for getScanPeriod to be efficient, since
 * it is often called from the interrupt routines of servers which use 
 * quadEM. */ 
static double getScanPeriod(void *drvPvt, asynUser *pasynUser)
{
    drvQuadEMPvt *pPvt = (drvQuadEMPvt *)drvPvt;

    return(pPvt->actualSecondsPerScan);
}


static void setGain(void *drvPvt, asynUser *pasynUser, int value)
{
    drvQuadEMPvt *pPvt = (drvQuadEMPvt *)drvPvt;

    write(pPvt, pasynUser, RANGE_COMMAND, value);
}

static void setPeriod(void *drvPvt, asynUser *pasynUser, int period)
{
    drvQuadEMPvt *pPvt = (drvQuadEMPvt *)drvPvt;

    /* For now we use a fixed period */
    write(pPvt, pasynUser, PERIOD_COMMAND, 0xffff);
}

static void go(void *drvPvt, asynUser *pasynUser)
{
    drvQuadEMPvt *pPvt = (drvQuadEMPvt *)drvPvt;

    write(pPvt, pasynUser, GO_COMMAND, 1);
}

static void setPulse(void *drvPvt, asynUser *pasynUser, int value)
{
    drvQuadEMPvt *pPvt = (drvQuadEMPvt *)drvPvt;

    write(pPvt, pasynUser, PULSE_COMMAND, value);
}

static asynStatus readInt32(void *drvPvt, asynUser *pasynUser, 
                            epicsInt32 *value)
{
    drvQuadEMPvt *pPvt = (drvQuadEMPvt *)drvPvt;
    int channel;
    quadEMCommand command = pasynUser->reason;

    pasynManager->getAddr(pasynUser, &channel);
    switch(command) {
    case quadEMCurrent:
        *value = pPvt->data.array[channel];
        break;
    default:
        epicsSnprintf(pasynUser->errorMessage, pasynUser->errorMessageSize,
                      "readInt32, illegal command=%d", command);
        return(asynError);
        break;
    }
    return(asynSuccess);
}

static asynStatus writeInt32(void *drvPvt, asynUser *pasynUser, 
                             epicsInt32 value)
{
    drvQuadEMPvt *pPvt = (drvQuadEMPvt *)drvPvt;
    int channel;
    quadEMCommand command = pasynUser->reason;

    pasynManager->getAddr(pasynUser, &channel);
    if ((command == quadEMOffset) || (command == quadEMPingPong)) {
        if ((channel < 0) || (channel > 4)) {
            epicsSnprintf(pasynUser->errorMessage, pasynUser->errorMessageSize,
                          "drvQuadEM::writeInt32, channel must be 0-3, =%d",
                          channel);
            return(asynError);
        }
    } else {
        if (channel > 0 ) {
            epicsSnprintf(pasynUser->errorMessage, pasynUser->errorMessageSize,
                          "drvQuadEM::writeInt32, channel must be 0, =%d",
                          channel);
            return(asynError);
        }
    }
    
    switch(command) {
    case quadEMOffset:
        pPvt->data.offset[channel] = value;
        break;
    case quadEMPeriod:
        setPeriod(drvPvt, pasynUser, value);
        break;
    case quadEMPulse:
        setPulse(drvPvt, pasynUser, value);
        break;
    case quadEMGo:
        go(drvPvt, pasynUser);
        break;
    case quadEMPingPong:
        pPvt->data.pingpong[channel] = value;
        break;
    case quadEMGain:
        setGain(drvPvt, pasynUser, value);
        break;
    default:
        epicsSnprintf(pasynUser->errorMessage, pasynUser->errorMessageSize,
                      "writeInt32, illegal command=%d", command);
        return(asynError);
        break;
    }
    return(asynSuccess);
}


static asynStatus getBounds(void *drvPvt, asynUser *pasynUser,
                            epicsInt32 *low, epicsInt32 *high)
{
    /* This needs work.  The range depends on the signal, which we 
       could figure out */
    *low = 0;
    *high = 65535;
    return(0);
}

static asynStatus readFloat64(void *drvPvt, asynUser *pasynUser, 
                              epicsFloat64 *value)
{
    drvQuadEMPvt *pPvt = (drvQuadEMPvt *)drvPvt;
    int channel;
    quadEMCommand command = pasynUser->reason;

    pasynManager->getAddr(pasynUser, &channel);
    switch(command) {
    case quadEMCurrent:
        *value = (double)pPvt->data.array[channel];
        break;
    case quadEMScanPeriod:
        *value = getScanPeriod(drvPvt, pasynUser);
        break;
    default:
        epicsSnprintf(pasynUser->errorMessage, pasynUser->errorMessageSize,
                      "readFloat64, illegal command=%d", command);
        return(asynError);
        break;
    }
    return(asynSuccess);
}

static asynStatus writeFloat64(void *drvPvt, asynUser *pasynUser, 
                               epicsFloat64 value)
{
    quadEMCommand command = pasynUser->reason;

    switch(command) {
    case quadEMScanPeriod:
        setScanPeriod(drvPvt, pasynUser, value);
        break;
    default:
        epicsSnprintf(pasynUser->errorMessage, pasynUser->errorMessageSize,
                      "writeFloat64, illegal command=%d", command);
        return(asynError);
        break;
    }
    return(asynSuccess);
}

static void read(void *drvPvt, asynUser *pasynUser, int *raw)
{
    /* Note, this is called at interrupt level, so it cannot do any prints */
    drvQuadEMPvt *pPvt = (drvQuadEMPvt *)drvPvt;

    raw[0] = *(pPvt->baseAddress+0);
    raw[1] = *(pPvt->baseAddress+2);
    raw[2] = *(pPvt->baseAddress+4);
    raw[3] = *(pPvt->baseAddress+6);
    /* Note that the following seems strange, but diode 4 comes before 
     * diode 3 in memory. */
    raw[4] = *(pPvt->baseAddress+12);
    raw[5] = *(pPvt->baseAddress+14);
    raw[6] = *(pPvt->baseAddress+8);
    raw[7] = *(pPvt->baseAddress+10);
}

static void write(void *drvPvt, asynUser *pasynUser, int command, int value)
{
    drvQuadEMPvt *pPvt = (drvQuadEMPvt *)drvPvt;

   *(pPvt->baseAddress+12) = (unsigned short)COMMAND1;
   *(pPvt->baseAddress+8)  = (unsigned short)command;
   *(pPvt->baseAddress+4)  = (unsigned short)value;
   *(pPvt->baseAddress+16) = (unsigned short)0;
   asynPrint(pasynUser, ASYN_TRACEIO_DRIVER, 
             "drvQuadEM::write, address=%p, command=%x value=%x\n", 
             pPvt->baseAddress, command, value);
}


static void computeCurrent(quadEMData *d)
{
    int i, j;

    for (i=0, j=0; i<4; i++, j=i*2) {
        switch(d->pingpong[i]) {
        case 0:
           d->current[i] = d->raw[j];
           break;
        case 1:
           d->current[i] = d->raw[j+1];
           break;
        case 2:
           d->current[i] = (d->raw[j] + d->raw[j+1])/2;
           break;
        }
        d->current[i] = d->current[i] - d->offset[i];
    }
}

static void computePosition(quadEMData *d)
{
    int i;

    d->sum[0]  = d->current[0] + d->current[2];
    d->difference[0] = d->current[2] - d->current[0];
    if (d->sum[0] == 0) d->sum[0] = 1;
    d->position[0]  = (QUAD_EM_POS_SCALE * d->difference[0]) / d->sum[0];
    d->sum[1]  = d->current[1] + d->current[3];
    d->difference[1] = d->current[3] - d->current[1];
    if (d->sum[1] == 0) d->sum[1] = 1;
    d->position[1]  = (QUAD_EM_POS_SCALE * d->difference[1]) / d->sum[1];
    /* Copy the data to an int array, since this is needed by many clients */
    for (i=0; i<4; i++) d->array[i] = d->current[i];
    for (i=0; i<2; i++) {
       d->array[i+4] = d->sum[i];
       d->array[i+6] = d->difference[i];
       d->array[i+8] = d->position[i];
    }
}


/* asynCommon routines */

/* Report  parameters */
static void report(void *drvPvt, FILE *fp, int details)
{
    drvQuadEMPvt *pPvt = (drvQuadEMPvt *)drvPvt;
    interruptNode *pnode;
    ELLLIST *pclientList;

    fprintf(fp, "Port: %s, address %p\n", pPvt->portName, pPvt->baseAddress);
    if (details >= 1) {
        if (pPvt->uint32DigitalPvt) {
           fprintf(fp, "    Using digital I/O interrupts\n");
        } else {
           fprintf(fp, "    Not using interrupts, scan time=%f\n",
                   pPvt->actualSecondsPerScan);
        }
        /* Report int32 interrupts */
        pasynManager->interruptStart(pPvt->int32InterruptPvt, &pclientList);
        pnode = (interruptNode *)ellFirst(pclientList);
        while (pnode) {
            asynInt32Interrupt *pint32Interrupt = pnode->drvPvt;
            fprintf(fp, "    int32 callback client address=%p, addr=%d, reason=%d\n",
                    pint32Interrupt->callback, pint32Interrupt->addr,
                    pint32Interrupt->pasynUser->reason);
            pnode = (interruptNode *)ellNext(&pnode->node);
        }
        pasynManager->interruptEnd(pPvt->int32InterruptPvt);

        /* Report float64 interrupts */
        pasynManager->interruptStart(pPvt->float64InterruptPvt, &pclientList);
        pnode = (interruptNode *)ellFirst(pclientList);
        while (pnode) {
            asynFloat64Interrupt *pfloat64Interrupt = pnode->drvPvt;
            fprintf(fp, "    float64 callback client address=%p, addr=%d, reason=%d\n",
                    pfloat64Interrupt->callback, pfloat64Interrupt->addr,
                    pfloat64Interrupt->pasynUser->reason);
            pnode = (interruptNode *)ellNext(&pnode->node);
        }
        pasynManager->interruptEnd(pPvt->float64InterruptPvt);

        /* Report int32Array interrupts */
        pasynManager->interruptStart(pPvt->int32ArrayInterruptPvt, &pclientList);
        pnode = (interruptNode *)ellFirst(pclientList);
        while (pnode) {
            asynInt32ArrayInterrupt *pint32ArrayInterrupt = pnode->drvPvt;
            fprintf(fp, "    int32Array callback client address=%p, reason=%d\n",
                    pint32ArrayInterrupt->callback, 
                    pint32ArrayInterrupt->pasynUser->reason);
            pnode = (interruptNode *)ellNext(&pnode->node);
        }
        pasynManager->interruptEnd(pPvt->int32ArrayInterruptPvt);
    }
}

/* Connect */
static asynStatus connect(void *drvPvt, asynUser *pasynUser)
{
    pasynManager->exceptionConnect(pasynUser);
    return(asynSuccess);
}

/* Disconnect */
static asynStatus disconnect(void *drvPvt, asynUser *pasynUser)
{
    pasynManager->exceptionDisconnect(pasynUser);
    return(asynSuccess);
}


static const iocshArg initArg0 = { "name",iocshArgString};
static const iocshArg initArg1 = { "baseAddr",iocshArgInt};
static const iocshArg initArg2 = { "fiberChannel",iocshArgInt};
static const iocshArg initArg3 = { "microSecondsPerScan",iocshArgInt};
static const iocshArg initArg4 = { "unidigName",iocshArgString};
static const iocshArg initArg5 = { "unidigChan",iocshArgInt};
static const iocshArg * const initArgs[6] = {&initArg0,
                                             &initArg1,
                                             &initArg2,
                                             &initArg3,
                                             &initArg4,
                                             &initArg5};
static const iocshFuncDef initFuncDef = {"initQuadEM",6,initArgs};
static void initCallFunc(const iocshArgBuf *args)
{
    initQuadEM(args[0].sval,
               (unsigned short*)args[1].ival, 
               args[2].ival,
               args[3].ival,
               args[4].sval,
               args[5].ival);
}

void quadEMRegister(void)
{
    iocshRegister(&initFuncDef,initCallFunc);
}

epicsExportRegistrar(quadEMRegister);

