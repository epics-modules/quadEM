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
#include "asynInt32Callback.h"
#include "asynFloat64Callback.h"
#include "asynInt32ArrayCallback.h"
#include "asynUInt32Digital.h"
#include "asynUInt32DigitalCallback.h"
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
    ELLNODE *next;
    ELLNODE *previous;
    void (*int32Callback)(void *drvPvt, epicsInt32 data);
    void (*float64Callback)(void *drvPvt, epicsFloat64 data);
    void (*int32ArrayCallback)(void *drvPvt, epicsInt32 *data, 
                               epicsUInt32 nelem);
    void *pvt;
    dataType dataType;
    quadEMCommand *pcommand;
    int channel;
} quadEMClient;

typedef struct {
    char *portName;
    unsigned short *baseAddress;
    asynUser *pasynUser;
    quadEMData data;
    epicsMessageQueueId intMsgQId;
    quadEMClient *client;
    double actualSecondsPerScan;
    ELLLIST clientList;
    asynUInt32DigitalCallback *uint32DCb;
    void *uint32DCbPvt;
    asynUser *puint32DCbAsynUser;
    asynInterface common;
    asynInterface int32;
    asynInterface uint32Digital;
    asynInterface float64;
    asynInterface int32Callback;
    asynInterface float64Callback;
    asynInterface int32ArrayCallback;
    asynInterface drvUser;
} drvQuadEMPvt;

/* These functions are used by the interfaces */
static asynStatus readInt32         (void *drvPvt, asynUser *pasynUser,
                                     epicsInt32 *value);
static asynStatus writeInt32        (void *drvPvt, asynUser *pasynUser,
                                     epicsInt32 value);
static asynStatus getBounds         (void *drvPvt, asynUser *pasynUser,
                                     epicsInt32 *low, epicsInt32 *high);
static asynStatus readUInt32Digital (void *drvPvt, asynUser *pasynUser,
                                     epicsUInt32 *value, epicsUInt32 mask);
static asynStatus writeUInt32Digital (void *drvPvt, asynUser *pasynUser,
                                      epicsUInt32 value, epicsUInt32 mask);
static asynStatus readFloat64       (void *drvPvt, asynUser *pasynUser,
                                     epicsFloat64 *value);
static asynStatus writeFloat64      (void *drvPvt, asynUser *pasynUser,
                                     epicsFloat64 value);
static asynStatus registerInt32Callback (void *drvPvt, asynUser *pasynUser,
                                     void (*callback)(void *drvPvt, 
                                                      epicsInt32 data),
                                     void *pvt);
static asynStatus registerFloat64Callback  (void *drvPvt, asynUser *pasynUser,
                                     void (*callback)(void *drvPvt, 
                                                      epicsFloat64 data),
                                     void *pvt);
static asynStatus registerInt32ArrayCallback(void *drvPvt, asynUser *pasynUser,
                                     void (*callback)(void *drvPvt,
                                                      epicsInt32 *data,
                                                      epicsUInt32 nelem),
                                     void *pvt);
static asynStatus cancelInt32Callback (void *drvPvt, asynUser *pasynUser,
                                     void (*callback)(void *drvPvt, 
                                                      epicsInt32 data),
                                     void *pvt);
static asynStatus cancelFloat64Callback  (void *drvPvt, asynUser *pasynUser,
                                     void (*callback)(void *drvPvt, 
                                                      epicsFloat64 data),
                                     void *pvt);
static asynStatus cancelInt32ArrayCallback(void *drvPvt, asynUser *pasynUser,
                                     void (*callback)(void *drvPvt,
                                                      epicsInt32 *data,
                                                      epicsUInt32 nelem),
                                     void *pvt);
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
static quadEMData readData          (void *drvPvt, asynUser *pasynUser);
static void write                   (void *drvPvt, asynUser *pasynUser,
                                     int command, int value);
static void read                    (void *drvPvt, asynUser *pasynUser,
                                     int *raw);
static asynStatus registerCallback  (void *drvPvt, asynUser *pasynUser,
                                     void *callback, void *pvt, 
                                     dataType dataType);
static asynStatus cancelCallback    (void *drvPvt, asynUser *pasynUser,
                                     void *callback, void *pvt, 
                                     dataType dataType);
static void computePosition         (quadEMData *d);
static void computeCurrent          (quadEMData *d);
static void poller                  (drvQuadEMPvt *pPvt);  
                                    /* Polling routine if no interrupts */
static void intFunc                 (void *drvPvt, unsigned int mask); 
                                     /* Interrupt function */
static void intTask                 (drvQuadEMPvt *pPvt);  
                                    /* Task that waits for interrupts */

static const asynInt32 drvQuadEMInt32 = {
    writeInt32,
    readInt32,
    getBounds
};

static const asynUInt32Digital drvQuadEMUInt32Digital = {
    writeUInt32Digital,
    readUInt32Digital
};

static const asynFloat64 drvQuadEMFloat64 = {
    writeFloat64,
    readFloat64
};

static const asynInt32Callback drvQuadEMInt32Callback = {
    registerInt32Callback,
    cancelInt32Callback
};

static const asynInt32ArrayCallback drvQuadEMInt32ArrayCallback = {
    registerInt32ArrayCallback,
    cancelInt32ArrayCallback
};

static const asynFloat64Callback drvQuadEMFloat64Callback = {
    registerFloat64Callback,
    cancelFloat64Callback
};

static const asynDrvUser drvQuadEMDrvUser = {
    drvUserCreate,
    drvUserGetType,
    drvUserDestroy
};

/* asynCommon methods */
static const struct asynCommon drvQuadEMCommon = {
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

    pPvt = callocMustSucceed(1, sizeof(*pPvt), "initQuadEM");
    pPvt->portName = epicsStrDup(portName);
    ellInit(&pPvt->clientList);

    if ((unidigName != 0) && (strlen(unidigName) != 0)) {
        /* Create asynUser */
        pPvt->puint32DCbAsynUser = pasynManager->createAsynUser(0, 0);

        /* Connect to device */
        status = pasynManager->connectDevice(pPvt->puint32DCbAsynUser, 
                                             unidigName, unidigChan);
        if (status != asynSuccess) {
            errlogPrintf("initQuadEM, connectDevice failed for digitalCallback\n");
            return -1;
        }

        /* Get the asynUInt32DigitalCallback interface */
        pasynInterface = pasynManager->findInterface(pPvt->puint32DCbAsynUser, 
                                                   asynUInt32DigitalCallbackType, 1);
        if (!pasynInterface) {
            errlogPrintf("initQuadEM, find asynUInt32DigitalCallback interface failed\n");
            return -1;
        }
        pPvt->uint32DCb = (asynUInt32DigitalCallback *)pasynInterface->pinterface;
        pPvt->uint32DCbPvt = pasynInterface->drvPvt;
    }
 
    pPvt->intMsgQId = epicsMessageQueueCreate(MAX_MESSAGES, MAX_RAW*sizeof(int));
    if (epicsThreadCreate("quadEMintTask",
                           epicsThreadPriorityHigh, 10000,
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

    /* Send the initial settings to the board to get it talking to the 
     * electometer. These settings will be overridden by the database values 
     * when the database initializes */
    setGain(pPvt, pPvt->pasynUser, 0);
    setPulse(pPvt, pPvt->pasynUser, 1024);
    setPeriod(pPvt, pPvt->pasynUser, 0xffff);
    setScanPeriod(pPvt, pPvt->pasynUser, microSecondsPerScan/1.e6);
    go(pPvt, pPvt->pasynUser);
    if (pPvt->uint32DCbPvt == NULL) {
        if (epicsThreadCreate("quadEMPoller",
                              epicsThreadPriorityMedium, 10000,
                              (EPICSTHREADFUNC)poller,
                              pPvt) == NULL) {
            errlogPrintf("quadEMPoller epicsThreadCreate failure\n");
            return(-1);
        }
    }
    else {
        /* Make sure interrupts are enabled on the falling edge of the 
         * quadEM output pulse */
        pPvt->uint32DCb->getInterrupt(pPvt->uint32DCbPvt, 
                                      pPvt->puint32DCbAsynUser, &mask,
                                      interruptOnOneToZero);
        mask |= 1 << unidigChan;
        pPvt->uint32DCb->setInterrupt(pPvt->uint32DCbPvt, 
                                      pPvt->puint32DCbAsynUser, mask,
                                      interruptOnOneToZero);
        mask = 1 << unidigChan;
        pPvt->uint32DCb->registerCallback(pPvt->uint32DCbPvt, 
                                          pPvt->puint32DCbAsynUser, intFunc, 
                                          mask, pPvt);
    }

    /* Link with higher level routines */
    pPvt->common.interfaceType = asynCommonType;
    pPvt->common.pinterface  = (void *)&drvQuadEMCommon;
    pPvt->common.drvPvt = pPvt;
    pPvt->int32.interfaceType = asynInt32Type;
    pPvt->int32.pinterface  = (void *)&drvQuadEMInt32;
    pPvt->int32.drvPvt = pPvt;
    pPvt->uint32Digital.interfaceType = asynUInt32DigitalType;
    pPvt->uint32Digital.pinterface  = (void *)&drvQuadEMUInt32Digital;
    pPvt->uint32Digital.drvPvt = pPvt;
    pPvt->float64.interfaceType = asynFloat64Type;
    pPvt->float64.pinterface  = (void *)&drvQuadEMFloat64;
    pPvt->float64.drvPvt = pPvt;
    pPvt->int32Callback.interfaceType = asynInt32CallbackType;
    pPvt->int32Callback.pinterface  = (void *)&drvQuadEMInt32Callback;
    pPvt->int32Callback.drvPvt = pPvt;
    pPvt->float64Callback.interfaceType = asynFloat64CallbackType;
    pPvt->float64Callback.pinterface  = (void *)&drvQuadEMFloat64Callback;
    pPvt->float64Callback.drvPvt = pPvt;
    pPvt->int32ArrayCallback.interfaceType = asynInt32ArrayCallbackType;
    pPvt->int32ArrayCallback.pinterface  = (void *)&drvQuadEMInt32ArrayCallback;
    pPvt->int32ArrayCallback.drvPvt = pPvt;
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
    status = pasynManager->registerInterface(pPvt->portName,&pPvt->int32);
    if (status != asynSuccess) {
        errlogPrintf("initQuadEM ERROR: Can't register int32\n");
        return -1;
    }
    status = pasynManager->registerInterface(pPvt->portName,&pPvt->uint32Digital);
    if (status != asynSuccess) {
        errlogPrintf("initQuadEM ERROR: Can't register uint32Digital\n");
        return -1;
    }
    status = pasynManager->registerInterface(pPvt->portName,&pPvt->float64);
    if (status != asynSuccess) {
        errlogPrintf("initQuadEM ERROR: Can't register float64\n");
        return -1;
    }
    status = pasynManager->registerInterface(pPvt->portName,&pPvt->int32Callback);
    if (status != asynSuccess) {
        errlogPrintf("initQuadEM ERROR: Can't register int32Callback\n");
        return -1;
    }
    status = pasynManager->registerInterface(pPvt->portName,&pPvt->float64Callback);
    if (status != asynSuccess) {
        errlogPrintf("initQuadEM ERROR: Can't register float64Callback\n");
        return -1;
    }
    status = pasynManager->registerInterface(pPvt->portName,
                                             &pPvt->int32ArrayCallback);
    if (status != asynSuccess) {
        errlogPrintf("initQuadEM ERROR: Can't register int32ArrayCallback\n");
        return -1;
    }
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
 
    return(0);
}


static void poller(drvQuadEMPvt *pPvt)
/*  This functions runs as a polling task at the system clock rate if there is 
 *  no interrupts present */
{
    while(1) { /* Do forever */
        intFunc(pPvt, 0);
        epicsThreadSleep(epicsThreadSleepQuantum());
    }
}

static quadEMData readData(void *drvPvt, asynUser *pasynUser)
{
    drvQuadEMPvt *pPvt = (drvQuadEMPvt *)drvPvt;

    return(pPvt->data);
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
            pasynUser->drvUser = &quadEMCommands[i].command;
            if (pptypeName) *pptypeName = epicsStrDup(pstring);
            if (psize) *psize = sizeof(quadEMCommands[i].command);
            return(asynSuccess);
        }
    }
    asynPrint(pasynUser, ASYN_TRACE_ERROR,
              "drvQuadEM::drvUserCreate unknown drvInfo string = %s\n",
              drvInfo);
    return(asynError);
}

static asynStatus drvUserGetType(void *drvPvt, asynUser *pasynUser,
                                 const char **pptypeName, size_t *psize)
{
    quadEMCommand *pcommand = pasynUser->drvUser;

    if (pcommand) *pptypeName = epicsStrDup(
                                    quadEMCommands[*pcommand].commandString);
    if (psize) *psize = sizeof(*pcommand);
    return(asynSuccess);
}

static asynStatus drvUserDestroy(void *drvPvt,asynUser *pasynUser)
{
    return(asynSuccess);
}


static asynStatus registerInt32Callback(void *drvPvt, asynUser *pasynUser,
                                        void (*callback)(void *drvPvt,
                                                          epicsInt32 data),
                                        void *pvt)
{
    return(registerCallback(drvPvt, pasynUser, callback, pvt, typeInt32));
}

static asynStatus registerFloat64Callback(void *drvPvt, asynUser *pasynUser,
                                          void (*callback)(void *drvPvt,
                                                           epicsFloat64 data),
                                          void *pvt)
{
    return(registerCallback(drvPvt, pasynUser, callback, pvt, typeFloat64));
}

static asynStatus registerInt32ArrayCallback(void *drvPvt, asynUser *pasynUser,
                                             void (*callback)(void *drvPvt,
                                                              epicsInt32 *data,
                                                             epicsUInt32 nelem),
                                             void *pvt)
{
    return(registerCallback(drvPvt, pasynUser, callback, pvt, typeInt32Array));
}

static asynStatus registerCallback(void *drvPvt, asynUser *pasynUser, 
                                   void *callback, void *pvt, dataType dataType)
{
    drvQuadEMPvt *pPvt = (drvQuadEMPvt *)drvPvt;
    quadEMClient *pClient = callocMustSucceed(1, sizeof(*pClient), 
                                              "drvQuadEM::registerCallback");

    pClient->dataType = dataType;
    pClient->pcommand = pasynUser->drvUser;
    pasynManager->getAddr(pasynUser, &pClient->channel);
    switch(dataType) {
    case typeInt32:
        pClient->int32Callback = callback;
        break;
    case typeFloat64:
        pClient->float64Callback = callback;
        break;
    case typeInt32Array:
        pClient->int32ArrayCallback = callback;
        break;
    }
    pClient->pvt = pvt;
    ellAdd(&pPvt->clientList, (ELLNODE *)pClient);
    return(asynSuccess);
}
       
static void intFunc(void *drvPvt, epicsUInt32 mask)
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

static asynStatus cancelInt32Callback(void *drvPvt, asynUser *pasynUser,
                                      void (*callback)(void *drvPvt,
                                                       epicsInt32 data),
                                      void *pvt)
{
    return(cancelCallback(drvPvt, pasynUser, callback, pvt, typeInt32));
}

static asynStatus cancelFloat64Callback(void *drvPvt, asynUser *pasynUser,
                                        void (*callback)(void *drvPvt,
                                                         epicsFloat64 data),
                                        void *pvt)
{
    return(cancelCallback(drvPvt, pasynUser, callback, pvt, typeFloat64));
}

static asynStatus cancelInt32ArrayCallback(void *drvPvt, asynUser *pasynUser,
                                           void (*callback)(void *drvpvt,
                                                            epicsInt32 *data,
                                                            epicsUInt32 nelem),
                                           void *pvt)
{
    return(cancelCallback(drvPvt, pasynUser, callback, pvt, typeInt32Array));
}

static asynStatus cancelCallback(void *drvPvt, asynUser *pasynUser,
                                 void *callback, void *pvt, dataType dataType)
{
    drvQuadEMPvt *pPvt = (drvQuadEMPvt *)drvPvt;
    void *clbk=NULL;
    quadEMClient *pClient;

    pClient = (quadEMClient *)ellFirst(&pPvt->clientList);
    while(pClient) {
        switch(dataType) {
        case typeInt32:
            clbk = pClient->int32Callback;
            break;
        case typeFloat64:
            clbk = pClient->float64Callback;
            break;
        case typeInt32Array:
            clbk = pClient->int32ArrayCallback;
            break;
        }
        if ((pClient->dataType == dataType) &&
            (pClient->pvt == pvt) &&
            (pClient->pcommand == pasynUser->drvUser) &&
            (clbk == callback)) {
            ellDelete(&pPvt->clientList, (ELLNODE *)pClient);
        }
        pClient = (quadEMClient *)ellNext(pClient);
    }
    return(asynSuccess);
}

static void intTask(drvQuadEMPvt *pPvt)
{
    int    i;
    int    raw[MAX_RAW];
    quadEMClient *pClient;

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
       /* Call the callback routines which have registered */
       pClient = (quadEMClient *)ellFirst(&pPvt->clientList);
       while(pClient) {
           if ((pClient->pcommand == NULL) ||
               (*pClient->pcommand == quadEMCurrent)) {
               switch(pClient->dataType) {
               case typeInt32:
                   if (pClient->int32Callback)
                       pClient->int32Callback(pClient->pvt, 
                                              pPvt->data.array[pClient->channel]);
                   break;
               case typeFloat64:
                   if (pClient->float64Callback) 
                       pClient->float64Callback(pClient->pvt, 
                                        (double)pPvt->data.array[pClient->channel]);
                   break;
               case typeInt32Array:
                   if (pClient->int32ArrayCallback) 
                       pClient->int32ArrayCallback(pClient->pvt, 
                                                   pPvt->data.array,
                                                   10);
                   break;
               }
           }
           pClient = (quadEMClient *)ellNext(pClient);
       }
   }
}

static double setScanPeriod(void *drvPvt, asynUser *pasynUser,
                            double seconds)
{
    double microSeconds = seconds * 1.e6;
    quadEMClient *pClient;

    drvQuadEMPvt *pPvt = (drvQuadEMPvt *)drvPvt;
    /* Convert from microseconds to device units */
    int convValue = (int)((microSeconds - 0.6)/1.6);

    write(pPvt, pasynUser, CONV_COMMAND, convValue);
    /* If we are using the interrupts then this is the scan rate
     * except that we only get interrupts after every other cycle
     * because of ping/pong, so we multiply by 2. */
    if (pPvt->uint32DCbPvt != NULL) {
        microSeconds = 2. * convValue * 1.6 + 0.6;
        pPvt->actualSecondsPerScan = microSeconds/1.e6;
    } else {
        pPvt->actualSecondsPerScan = epicsThreadSleepQuantum();
    }
    /* Call the callback routines which have registered to be notified when
       the scan period changes */
    pClient = (quadEMClient *)ellFirst(&pPvt->clientList);
    while(pClient) {
        if ((pClient->dataType == typeFloat64) &&
            (pClient->pcommand) &&
            (*pClient->pcommand == quadEMScanPeriod) &&
            (pClient->float64Callback))
            pClient->float64Callback(pClient->pvt, pPvt->actualSecondsPerScan);
        pClient = (quadEMClient *)ellNext(pClient);
    }
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
    quadEMCommand *pcommand = (quadEMCommand *)pasynUser->drvUser;
    quadEMCommand command = quadEMCurrent;

    pasynManager->getAddr(pasynUser, &channel);
    if (pcommand) command = *pcommand;
    switch(command) {
    case quadEMCurrent:
        *value = pPvt->data.array[channel];
        break;
    default:
        asynPrint(pasynUser, ASYN_TRACE_ERROR, 
                  "drvQuadEM::readInt32, illegal command=%d\n", command);
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
    quadEMCommand *pcommand = (quadEMCommand *)pasynUser->drvUser;
    quadEMCommand command = quadEMCurrent;

    pasynManager->getAddr(pasynUser, &channel);
    if (pcommand) command = *pcommand;
    if (command == quadEMOffset) {
        if ((channel < 0) || (channel > 4)) {
            asynPrint(pasynUser, ASYN_TRACE_ERROR,
                      "drvQuadEM::writeInt32, channel must be 0-3, =%d\n",
                      channel);
            return(asynError);
        }
    } else {
        if (channel > 0 ) {
            asynPrint(pasynUser, ASYN_TRACE_ERROR,
                      "drvQuadEM::writeInt32, channel must be 0, =%d\n",
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
    default:
        asynPrint(pasynUser, ASYN_TRACE_ERROR, 
                  "drvQuadEM::writeInt32, illegal command=%d\n", 
                  command);
        return(asynError);
        break;
    }
    return(asynSuccess);
}

static asynStatus readUInt32Digital(void *drvPvt, asynUser *pasynUser, 
                                    epicsUInt32 *value, epicsUInt32 mask)
{
    asynPrint(pasynUser, ASYN_TRACE_ERROR, 
              "drvQuadEM::readUInt32Digital, illegal command\n");
    return(asynError);
}


static asynStatus writeUInt32Digital(void *drvPvt, asynUser *pasynUser,
                                     epicsUInt32 value, epicsUInt32 mask)
{
    drvQuadEMPvt *pPvt = (drvQuadEMPvt *)drvPvt;
    int channel;
    quadEMCommand *pcommand = (quadEMCommand *)pasynUser->drvUser;
    quadEMCommand command = quadEMCurrent;

    pasynManager->getAddr(pasynUser, &channel);
    if (pcommand) command = *pcommand;
    if (command == quadEMPingPong) {
        if ((channel < 0) || (channel > 4)) {
            asynPrint(pasynUser, ASYN_TRACE_ERROR,
                      "drvQuadEM::writeUInt32Digital, channel must be 0-3, =%d\n",
                      channel);
            return(asynError);
        }
    } else {
        if (channel > 0 ) {
            asynPrint(pasynUser, ASYN_TRACE_ERROR,
                      "drvQuadEM::writeUInt32Digital, channel must be 0, =%d\n",
                      channel);
            return(asynError);
        }
    }

    switch(command) {
    case quadEMPingPong:
        pPvt->data.pingpong[channel] = value;
        break;
    case quadEMGain:
        setGain(drvPvt, pasynUser, value);
        break;
    default:
        asynPrint(pasynUser, ASYN_TRACE_ERROR,
                  "drvQuadEM::writeUInt32Digital, illegal command=%d\n",
                  command);
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
    quadEMCommand *pcommand = (quadEMCommand *)pasynUser->drvUser;
    quadEMCommand command = quadEMCurrent;

    pasynManager->getAddr(pasynUser, &channel);
    if (pcommand) command = *pcommand;
    switch(command) {
    case quadEMCurrent:
        *value = (double)pPvt->data.array[channel];
        break;
    case quadEMScanPeriod:
        *value = getScanPeriod(drvPvt, pasynUser);
        break;
    default:
        asynPrint(pasynUser, ASYN_TRACE_ERROR, 
                  "drvQuadEM::readFloat64, illegal command=%d\n", command);
        return(asynError);
        break;
    }
    return(asynSuccess);
}

static asynStatus writeFloat64(void *drvPvt, asynUser *pasynUser, 
                               epicsFloat64 value)
{
    quadEMCommand *pcommand = (quadEMCommand *)pasynUser->drvUser;
    quadEMCommand command = quadEMCurrent;

    if (pcommand) command = *pcommand;
    switch(command) {
    case quadEMScanPeriod:
        setScanPeriod(drvPvt, pasynUser, value);
        break;
    default:
        asynPrint(pasynUser, ASYN_TRACE_ERROR, 
                  "drvQuadEM::writeFloat64, illegal command=%d\n", command);
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


static void quadEMDataInit(quadEMData *d, int value)
{
    int i;
    for (i=0; i<MAX_RAW; i++) {
        d->raw[i] = value;
    }
    computeCurrent(d);
    computePosition(d);
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

    fprintf(fp, "Port: %s, address %p\n", pPvt->portName, pPvt->baseAddress);
    if (details >= 1) {
        if (pPvt->uint32DCbPvt) {
           fprintf(fp, "  Using digital I/O interrupts\n");
        } else {
           fprintf(fp, "  Not using interrupts, scan time=%f\n",
                   pPvt->actualSecondsPerScan);
        }
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
static const iocshFuncDef initFuncDef = {"init",6,initArgs};
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

