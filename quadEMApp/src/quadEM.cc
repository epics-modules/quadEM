/*  quadEM.cc

    Author: Mark Rivers
    Date: April 10, 2003, based on ip330.cc
          June 28, 2003 MLR Converted to R3.14.2
*/

#include <stdio.h>
#include <string.h>

extern "C" {
#include <devLib.h>
}
#include <epicsTypes.h>
#include <epicsThread.h>
#include <epicsMessageQueue.h>
#include <epicsPrint.h>
#include <iocsh.h>
#include <epicsExport.h>
#include <gpHash.h>

#include "quadEM.h"
#include "ipUnidig.h"
#define MAX_A24_ADDRESS  0xffffff

/* First word of every command */
#define COMMAND1        0xa000 
/* Other commands */
#define RANGE_COMMAND   1
#define GO_COMMAND      4
#define CONV_COMMAND    5
#define PULSE_COMMAND   6
#define PERIOD_COMMAND  7

extern "C"
{
// Debug levels
// 5  = print info during initialization
// 10 = print info during write operations
// 20 = print info on each read!
#ifdef NODEBUG
#define DEBUG(l,f,v) ;
#else
#define DEBUG(l,f,v...) { if(l<quadEMDebug) epicsPrintf(f,## v); }
#endif
int quadEMDebug = 0;
epicsExportAddress(int, quadEMDebug);

static void *quadEMHash;
}

extern "C" int initQuadEM(const char *name, unsigned short *baseAddr, 
                          int fiberChannel, int microSecondsPerScan, 
                          int maxClients, const char *unidigName, int unidigChan)
{
    IpUnidig *pIpUnidig = IpUnidig::findModule(unidigName);
    if ((unidigName!=NULL) && (strlen(unidigName)>0) && (pIpUnidig==NULL)) {
       printf("quadEM: can't find IpUnidig module %s\n", unidigName);
    }
 
    new quadEM(name, baseAddr, fiberChannel, microSecondsPerScan,
               maxClients, pIpUnidig, unidigChan);
    return(0);
}

quadEM* quadEM::findModule(const char *name) 
{
    GPHENTRY *hashEntry = gphFind(quadEMHash, name, NULL);
    if (hashEntry == NULL) return (NULL);
    return((quadEM *)hashEntry->userPvt);
}

quadEM::quadEM(const char *name, unsigned short *baseAddr, 
               int fiberChannel, int microSecondsPerScan,
               int maxClients, IpUnidig *pIpUnidig, int unidigChan)
: pIpUnidig(pIpUnidig), maxClients(maxClients), numClients(0)
{
    client = (quadEMClient *)calloc(maxClients, sizeof(quadEMClient));

    intMsgQ = new epicsMessageQueue(MAX_MESSAGES, MAX_RAW*sizeof(int));
    if (epicsThreadCreate("quadEMintTask",
                           epicsThreadPriorityHigh, 10000,
                           (EPICSTHREADFUNC)quadEM::intTask,
                           (void*)this) == NULL)
       errlogPrintf("quadEMintTask epicsThreadCreate failure\n");
    if (quadEMHash == NULL) gphInitPvt(&quadEMHash, 256);
    char *temp = (char *)malloc(strlen(name)+1);
    strcpy(temp, name);
    GPHENTRY *hashEntry = gphAdd(quadEMHash, temp, NULL);
    hashEntry->userPvt = this;

    if((fiberChannel >= 4) || (fiberChannel < 0)) {
        printf("quadEM::quadEM: Invalid channel # %d \n", fiberChannel);
        return;
    }

    if(baseAddr >= (unsigned short *)MAX_A24_ADDRESS) {
        printf("quadEM::quadEM: Invalid Module Address %p \n", baseAddr);
        return;
    }

    /* The channel # goes in bits 5 and 6 */
    baseAddr = (unsigned short *)((int)baseAddr | ((fiberChannel << 5) & 0x60));
    if (devRegisterAddress("quadEM", atVMEA24, (int)baseAddr, 16, 
                           (volatile void**)&baseAddress) != 0) {
        baseAddress = NULL;
        printf("quadEM::quadEM: A24 Address map failed\n");
        return;
    }

    unsigned long probeVal;
    if (devReadProbe(4, (char *)baseAddress, (char *)&probeVal) != 0 ) {
        printf("quadEM::quadEM: devReadProbe failed for address %p\n", baseAddress);
        baseAddress = NULL;
        return;
    }
    DEBUG(5, "quadEM:quadEM: devReadProbe found board OK\n");

    // Send the initial settings to the board to get it talking to the electometer
    // These settings will be overridden by the database values when the database
    // initializes
    DEBUG(5, "quadEM::quadEM: initializing hardware ...\n");
    setGain(0);
    setPulse(1024);
    setPeriod(0xffff);
    setMicroSecondsPerScan(microSecondsPerScan);
    go();
    if (pIpUnidig == NULL) {
        DEBUG(5, "quadEM::quadEM: calling epicsThreadCreate ...\n");
        if (epicsThreadCreate("quadEMPoller",
                              epicsThreadPriorityMedium, 10000,
                              (EPICSTHREADFUNC)quadEM::poller,
                              (void*)this) == NULL)
           errlogPrintf("quadEMPoller epicsThreadCreate failure\n");
    }
    else {
        DEBUG(5, "quadEM::quadEM: calling setFallingMaskBits ...\n");
        int mask = 1<<(unidigChan);
        pIpUnidig->setFallingMaskBits(mask);
        DEBUG(5, "quadEM::quadEM: calling pIpUnidig->registerCallback ...\n");
        pIpUnidig->registerCallback(intFunc, (void *)this, mask);
    }
}

void quadEM::poller(quadEM *p)
//  This functions runs as a polling task at 100Hz (max) if there is no 
//  IP-Unidig present
{
    DEBUG(5, "epicsThreadSleepQuantum=%f\n", epicsThreadSleepQuantum());
    while(1) { // Do forever
        intFunc(p, 0);
        epicsThreadSleep(epicsThreadSleepQuantum());
    }
}

quadEMData quadEM::getData()
{
    return(data);
}

int quadEM::registerCallback(quadEMCallback callback, void *pvt)
{
    if (numClients >= maxClients) {
       printf("quadEM::registerClient: too many clients\n");
       return(-1);
    }
    numClients = numClients + 1;
    client[numClients-1].pvt = pvt;
    client[numClients-1].callback = callback;
    return(0);
}
       
void quadEM::intFunc(void *v, unsigned int mask)
{
    quadEM *t = (quadEM *) v;
    int raw[MAX_RAW];

    // Read the new data
    t->read(raw);
    // Send a message to the intTask task, it handles the rest, not running at interrupt level
    t->intMsgQ->trySend(raw, sizeof(raw));
    
}

void quadEM::intTask(quadEM *p)
{
    int    i;
    int    raw[MAX_RAW];

    while(1) {
       // Wait for a message from interrupt routine
       p->intMsgQ->receive(raw, sizeof(raw));
       for (i=0; i<MAX_RAW; i++) p->data.raw[i] = raw[i];
       p->data.computeCurrent();
       p->data.computePosition();
       // Call the callback routines which have registered
       for (i = 0; i < p->numClients; i++) {
          p->client[i].callback(p->client[i].pvt, p->data);
       }
   }
}

float quadEM::setMicroSecondsPerScan(float microSeconds)
{
    /* Convert from microseconds to device units */
    int convValue = (int)((microSeconds - 0.6)/1.6);
    write(CONV_COMMAND, convValue);
    // If we are using the ipUnidig then this is the scan rate
    // except that we only get interrupts after every other cycle
    // because of ping/pong, so we multiply by 2.
    if (pIpUnidig != NULL) {
        actualMicroSecondsPerScan = 2. * convValue * 1.6 + 0.6;
    } else {
        actualMicroSecondsPerScan = 1.e6 * epicsThreadSleepQuantum();
    }
    return(getMicroSecondsPerScan());
}

// Note: we cache actualMicroSecondsPerScan for efficiency.
// It is important for getMicroSecondsPerScan to be efficient, since
// it is often called from the interrupt routines of servers which use quadEM.
float quadEM::getMicroSecondsPerScan()
{
    return(actualMicroSecondsPerScan);
}

void quadEM::setOffset(int channel, int value)
{
    data.offset[channel] = value;
}

void quadEM::setPingPong(int channel, int pingpong)
{
    data.pingpong[channel] = pingpong;
}

void quadEM::setGain(int value)
{
    write(RANGE_COMMAND, value);
}

void quadEM::setPeriod(int period)
{
    // For now we use a fixed period
    write(PERIOD_COMMAND, 0xffff);
}

void quadEM::go()
{
    write(GO_COMMAND, 1);
}

void quadEM::setPulse(int value)
{
    write(PULSE_COMMAND, value);
}

void quadEM::read(int *raw)
{

   raw[0] = *(baseAddress+0);
   raw[1] = *(baseAddress+2);
   raw[2] = *(baseAddress+4);
   raw[3] = *(baseAddress+6);
   // Note that the following seems strange, but diode 4 comes before diode 3 in
   // memory.
   raw[4] = *(baseAddress+12);
   raw[5] = *(baseAddress+14);
   raw[6] = *(baseAddress+8);
   raw[7] = *(baseAddress+10);
}

void quadEM::write(int command, int value)
{
   *(baseAddress+12) = (unsigned short)COMMAND1;
   *(baseAddress+8)  = (unsigned short)command;
   *(baseAddress+4)  = (unsigned short)value;
   *(baseAddress+16) = (unsigned short)0;
   DEBUG(10, "quadEM::write, address=%p, command=%x value=%x\n", baseAddress, command, value);
}


// These are the class definitions for the quadEMData class
// Constructor with no arguments
quadEMData::quadEMData()
{ 
    int i;
    for (i=0; i<MAX_RAW; i++) {
        raw[i] = 0;
    }
    computeCurrent();
    computePosition();
}

// Constructor with 1 argument
quadEMData::quadEMData(int value)
{ 
    int i;
    for (i=0; i<MAX_RAW; i++) {
        raw[i] = value;
    }
    computeCurrent();
    computePosition();
}

void quadEMData::computeCurrent()
{
    int i, j;

    for (i=0, j=0; i<4; i++, j=i*2) {
        switch(pingpong[i]) {
        case 0:
           current[i] = raw[j];
           break;
        case 1:
           current[i] = raw[j+1];
           break;
        case 2:
           current[i] = (raw[j] + raw[j+1])/2;
           break;
        }
        current[i] = current[i] - offset[i];
        DEBUG(20, "raw[%d]=%d, current[%d]=%d\n", i, raw[i], i, current[i]);
    }
}

void quadEMData::computePosition()
{
    int i;

    sum[0]  = current[0] + current[2];
    difference[0] = current[2] - current[0];
    if (sum[0] == 0) sum[0] = 1;
    position[0]  = (QUAD_EM_POS_SCALE * difference[0]) / sum[0];
    sum[1]  = current[1] + current[3];
    difference[1] = current[3] - current[1];
    if (sum[1] == 0) sum[1] = 1;
    position[1]  = (QUAD_EM_POS_SCALE * difference[1]) / sum[1];
    //Copy the data to an int array, since this is needed by many clients
    for (i=0; i<4; i++) array[i] = current[i];
    for (i=0; i<2; i++) {
       array[i+4] = sum[i];
       array[i+6] = difference[i];
       array[i+8] = position[i];
    }
}

static const iocshArg initArg0 = { "name",iocshArgString};
static const iocshArg initArg1 = { "baseAddr",iocshArgInt};
static const iocshArg initArg2 = { "fiberChannel",iocshArgInt};
static const iocshArg initArg3 = { "microSecondsPerScan",iocshArgInt};
static const iocshArg initArg4 = { "maxClients",iocshArgInt};
static const iocshArg initArg5 = { "unidigName",iocshArgString};
static const iocshArg initArg6 = { "unidigChan",iocshArgInt};
static const iocshArg * const initArgs[7] = {&initArg0,
                                             &initArg1,
                                             &initArg2,
                                             &initArg3,
                                             &initArg4,
                                             &initArg5,
                                             &initArg6};
static const iocshFuncDef initFuncDef = {"init",7,initArgs};
static void initCallFunc(const iocshArgBuf *args)
{
    initQuadEM(args[0].sval,
               (unsigned short*)args[1].ival, 
               args[2].ival,
               args[3].ival,
               args[4].ival,
               args[5].sval,
               args[6].ival);
}

void quadEMRegister(void)
{
    iocshRegister(&initFuncDef,initCallFunc);
}

epicsExportRegistrar(quadEMRegister);

