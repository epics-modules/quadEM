/*  drvQuadEM.c

    Author: Mark Rivers
    Date: April 10, 2003, based on ip330.cc
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
#include <asynDriver.h>
#include <asynInt32.h>
#include <asynFloat64.h>
#include <asynInt32Callback.h>
#include <asynFloat64Callback.h>
#include <asynInt32ArrayCallback.h>

#include "asynQuadEM.h"
#include "asynUInt32Digital.h"
#include "asynUInt32DigitalCallback.h"

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
    asynInt32DataCallback int32DataCallback;
    asynInt32IntervalCallback int32IntervalCallback;
    asynFloat64DataCallback float64DataCallback;
    asynFloat64IntervalCallback float64IntervalCallback;
    asynInt32ArrayDataCallback int32ArrayDataCallback;
    asynInt32ArrayIntervalCallback int32ArrayIntervalCallback;
    void *pvt;
    dataType dataType;
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
    asynInterface float64;
    asynInterface int32Callback;
    asynInterface float64Callback;
    asynInterface int32ArrayCallback;
    asynInterface quadEM;
} drvQuadEMPvt;

/* These functions are used by the interfaces */
static double setScanPeriod         (void *drvPvt, asynUser *pasynUser,
                                     double seconds);
static double getScanPeriod         (void *drvPvt, asynUser *pasynUser);
static void setOffset               (void *drvPvt, asynUser *pasynUser,
                                     int channel, int offset);
static void setPingPong             (void *drvPvt, asynUser *pasynUser,
                                     int channel, int pingpong);
static void setPeriod               (void *drvPvt, asynUser *pasynUser,
                                     int period);
static void setGain                 (void *drvPvt, asynUser *pasynUser, 
                                     int gain);
static void setPulse                (void *drvPvt, asynUser *pasynUser,
                                     int pulse);
static void go                      (void *drvPvt, asynUser *pasynUser);
static asynStatus readInt32         (void *drvPvt, asynUser *pasynUser,
                                     epicsInt32 *value);
static asynStatus writeInt32        (void *drvPvt, asynUser *pasynUser,
                                     epicsInt32 value);
static asynStatus readFloat64       (void *drvPvt, asynUser *pasynUser,
                                     epicsFloat64 *value);
static asynStatus writeFloat64      (void *drvPvt, asynUser *pasynUser,
                                     epicsFloat64 value);
static asynStatus registerInt32Callbacks (void *drvPvt, asynUser *pasynUser,
                                     asynInt32DataCallback dataCallback,
                                     asynInt32IntervalCallback intervalCallback,
                                     void *pvt);
static asynStatus registerFloat64Callbacks  (void *drvPvt, asynUser *pasynUser,
                                   asynFloat64DataCallback dataCallback,
                                   asynFloat64IntervalCallback intervalCallback,
                                   void *pvt);
static asynStatus registerInt32ArrayCallbacks(void *drvPvt, asynUser *pasynUser,
                               asynInt32ArrayDataCallback dataCallback,
                               asynInt32ArrayIntervalCallback intervalCallback,
                               void *pvt);
static void report                  (void *drvPvt, FILE *fp, int details);
static asynStatus connect           (void *drvPvt, asynUser *pasynUser);
static asynStatus disconnect        (void *drvPvt, asynUser *pasynUser);

/* These are private functions, not used in any interfaces */
static quadEMData readData          (void *drvPvt, asynUser *pasynUser);
static void write                   (void *drvPvt, asynUser *pasynUser,
                                     int command, int value);
static void read                    (void *drvPvt, asynUser *pasynUser,
                                     int *raw);
static asynStatus registerCallbacks (void *drvPvt, asynUser *pasynUser,
                                     void *dataCallback, void *intervalCallback,
                                     void *pvt, dataType dataType);
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
    readInt32
};

static const asynFloat64 drvQuadEMFloat64 = {
    writeFloat64,
    readFloat64
};

static const asynInt32Callback drvQuadEMInt32Callback = {
    setScanPeriod,
    getScanPeriod,
    registerInt32Callbacks
};

static const asynInt32ArrayCallback drvQuadEMInt32ArrayCallback = {
    setScanPeriod,
    getScanPeriod,
    registerInt32ArrayCallbacks
};

static const asynFloat64Callback drvQuadEMFloat64Callback = {
    setScanPeriod,
    getScanPeriod,
    registerFloat64Callbacks
};

static const asynQuadEM drvQuadEM = {
    setOffset,
    setPingPong,
    setPeriod,
    setGain,
    setPulse,
    go
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
        pPvt->uint32DCb->getInterruptMask(pPvt->uint32DCbPvt, 
                                          pPvt->puint32DCbAsynUser, &mask,
                                          interruptOnOneToZero);
        mask |= 1 << unidigChan;
        pPvt->uint32DCb->setInterruptMask(pPvt->uint32DCbPvt, 
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
    pPvt->quadEM.interfaceType = asynQuadEMType;
    pPvt->quadEM.pinterface  = (void *)&drvQuadEM;
    pPvt->quadEM.drvPvt = pPvt;
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
    status = pasynManager->registerInterface(pPvt->portName,&pPvt->quadEM);
    if (status != asynSuccess) {
        errlogPrintf("initQuadEM ERROR: Can't register quadEM.\n");
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


static asynStatus registerInt32Callbacks(void *drvPvt, asynUser *pasynUser,
                                    asynInt32DataCallback dataCallback,
                                    asynInt32IntervalCallback intervalCallback, 
                                    void *pvt)
{
    return(registerCallbacks(drvPvt, pasynUser, dataCallback, intervalCallback,
                             pvt, typeInt32));
}

static asynStatus registerFloat64Callbacks(void *drvPvt, asynUser *pasynUser,
                                   asynFloat64DataCallback dataCallback,
                                   asynFloat64IntervalCallback intervalCallback,
                                   void *pvt)
{
    return(registerCallbacks(drvPvt, pasynUser, dataCallback, intervalCallback,
                             pvt, typeFloat64));
}

static asynStatus registerInt32ArrayCallbacks(void *drvPvt, asynUser *pasynUser,
                                    asynInt32ArrayDataCallback dataCallback,
                                    asynInt32ArrayIntervalCallback 
                                        intervalCallback, 
                                    void *pvt)
{
    return(registerCallbacks(drvPvt, pasynUser, dataCallback, intervalCallback,
                             pvt, typeInt32Array));
}

static asynStatus registerCallbacks(void *drvPvt, asynUser *pasynUser, 
                                    void *dataCallback, void *intervalCallback,
                                    void *pvt, dataType dataType)
{
    drvQuadEMPvt *pPvt = (drvQuadEMPvt *)drvPvt;
    quadEMClient *pClient = callocMustSucceed(1, sizeof(*pClient), 
                                              "drvQuadEM::registerCallback");

    pClient->dataType = dataType;
    pasynManager->getAddr(pasynUser, &pClient->channel);
    switch(dataType) {
    case typeInt32:
        pClient->int32DataCallback = dataCallback;
        pClient->int32IntervalCallback = intervalCallback;
        break;
    case typeFloat64:
        pClient->float64DataCallback = dataCallback;
        pClient->float64IntervalCallback = intervalCallback;
        break;
    case typeInt32Array:
        pClient->int32ArrayDataCallback = dataCallback;
        pClient->int32ArrayIntervalCallback = intervalCallback;
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
           switch(pClient->dataType) {
           case typeInt32:
               if (pClient->int32DataCallback)
                   pClient->int32DataCallback(pClient->pvt, 
                                       pPvt->data.array[pClient->channel]);
               break;
           case typeFloat64:
               if (pClient->float64DataCallback) 
                   pClient->float64DataCallback(pClient->pvt, 
                                    (double)pPvt->data.array[pClient->channel]);
               break;
           case typeInt32Array:
               if (pClient->int32ArrayDataCallback) 
                   pClient->int32ArrayDataCallback(pClient->pvt, 
                                           pPvt->data.array);
               break;
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
        switch(pClient->dataType) {
        case typeInt32:
            if (pClient->int32IntervalCallback)
                pClient->int32IntervalCallback(pClient->pvt,
                                               pPvt->actualSecondsPerScan);
            break;
        case typeFloat64:
            if (pClient->float64IntervalCallback)
                pClient->float64IntervalCallback(pClient->pvt,
                                               pPvt->actualSecondsPerScan);
            break;
        case typeInt32Array:
            if (pClient->int32ArrayIntervalCallback)
                pClient->int32ArrayIntervalCallback(pClient->pvt,
                                               pPvt->actualSecondsPerScan);
            break;
        }
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

static void setOffset(void *drvPvt, asynUser *pasynUser, int channel, int value)
{
    drvQuadEMPvt *pPvt = (drvQuadEMPvt *)drvPvt;

    pPvt->data.offset[channel] = value;
}

static void setPingPong(void *drvPvt, asynUser *pasynUser, int channel, 
                        int pingpong)
{
    drvQuadEMPvt *pPvt = (drvQuadEMPvt *)drvPvt;

    pPvt->data.pingpong[channel] = pingpong;
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

    pasynManager->getAddr(pasynUser, &channel);
    *value = pPvt->data.array[channel];
    return(asynSuccess);
}

static asynStatus writeInt32(void *drvPvt, asynUser *pasynUser, 
                             epicsInt32 value)
{
    asynPrint(pasynUser, ASYN_TRACE_ERROR,
              "drvQuadEM::writeInt32, illegal write operation\n");
    return(asynError);
}

static asynStatus readFloat64(void *drvPvt, asynUser *pasynUser, 
                              epicsFloat64 *value)
{
    drvQuadEMPvt *pPvt = (drvQuadEMPvt *)drvPvt;
    int channel;

    pasynManager->getAddr(pasynUser, &channel);
    *value = (double)pPvt->data.array[channel];
    return(asynSuccess);
}

static asynStatus writeFloat64(void *drvPvt, asynUser *pasynUser, 
                               epicsFloat64 value)
{
    asynPrint(pasynUser, ASYN_TRACE_ERROR,
              "drvQuadEM::writeFloat64, illegal write operation\n");
    return(asynError);
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

