//quadEMPID.cc

/*
    Author: Mark Rivers
    04/18/03 MLR   Created, based on ip330PID.cc
*/

#include <stdio.h>
#include <string.h>

#include <epicsTypes.h>
#include <iocsh.h>
#include <epicsExport.h>
#include <epicsThread.h>
#include <epicsMessageQueue.h>

#include "Message.h"

#include "fastPID.h"
#include "quadEM.h"
#include "DAC128V.h"

class quadEMPID : public fastPID
{
public:
    double setMicroSecondsPerScan(double microSeconds);
    double getMicroSecondsPerScan();
    double readOutput();
    void writeOutput(double output);
    quadEMPID(quadEM *pQuadEM, int quadEMChannel, DAC128V *pDAC128V, 
              int DACChannel);
private:
    // Callback function from quadEM
    static void callback(void*, quadEMData data);   
    quadEM *pQuadEM;
    quadEMData averageStore;
    int quadEMChannel;  // current=0-3, sum=4,5, difference=6,7, position=8,9.
    DAC128V *pDAC128V;
    int DACChannel;
    int numAverage;
    int accumulated;
};


// These C functions are provided so that we can create and configure the
// fastPID object from the vxWorks command line, which does not understand 
// C++ syntax.
extern "C" int initQuadEMPID(const char *serverName, 
         const char *quadEMName, int quadEMChannel, const char *dacName, int DACChannel,
         int queueSize)
{
    quadEM *pQuadEM = quadEM::findModule(quadEMName);
    if (pQuadEM == NULL) {
       printf("quadEMPID: can't find quadEM module %s\n", quadEMName);
       return(-1);
    }
    DAC128V *pDAC128V = DAC128V::findModule(dacName);
    if (pDAC128V == NULL) {
       printf("quadEMPID: can't find DAC128V module %s\n", dacName);
       return(-1);
    }
    quadEMPID *pQuadEMPID = new quadEMPID(pQuadEM, quadEMChannel, pDAC128V, 
                                          DACChannel);
    fastPIDServer *pFastPIDServer = 
                          new fastPIDServer(serverName, pQuadEMPID, queueSize);
    if (epicsThreadCreate("quadEMPID",
                          epicsThreadPriorityMedium, 10000,
                          (EPICSTHREADFUNC)fastPIDServer::fastServer,
                          (void*)pFastPIDServer) == NULL)
        printf("%s fastPIDServer thread create failure\n", serverName);
    return(0);
}

quadEMPID::quadEMPID(quadEM *pQuadEM, int quadEMChannel, DAC128V *pDAC128V, 
                     int DACChannel)
: fastPID(),
  pQuadEM(pQuadEM), quadEMChannel(quadEMChannel), 
  pDAC128V(pDAC128V), DACChannel(DACChannel)
{
  pQuadEM->registerCallback(callback, (void *)this);
}

void quadEMPID::callback(void *v, quadEMData newData)
{
    quadEMPID *t = (quadEMPID *) v;
    int i;

    // No need to average if collecting every point
    if (t->numAverage == 1) {
        t->doPID((double)newData.array[t->quadEMChannel]);
       return;
    }
    for (i=0; i<4; i++) t->averageStore.current[i] += newData.current[i];
    if (++t->accumulated < t->numAverage) return;
    // We have now collected the desired number of points to average
    for (i=0; i<4; i++) t->averageStore.current[i] /= t->accumulated;
    t->averageStore.computePosition();
    t->doPID((double)t->averageStore.array[t->quadEMChannel]);
    for (i=0; i<4; i++) t->averageStore.current[i] = 0;
    t->accumulated = 0;
}

double quadEMPID::setMicroSecondsPerScan(double microSeconds)
{
    numAverage = (int) (microSeconds /
                        pQuadEM->getMicroSecondsPerScan() + 0.5);
    if (numAverage < 1) numAverage = 1;
    accumulated = 0;
    return getMicroSecondsPerScan();
}   

double quadEMPID::getMicroSecondsPerScan()
{
    return(pQuadEM->getMicroSecondsPerScan() * (numAverage));
}

double quadEMPID::readOutput()
{
    int currentDAC;
    pDAC128V->getValue(&currentDAC, DACChannel);
    return((double)currentDAC);
}

void quadEMPID::writeOutput(double output)
{
    pDAC128V->setValue((int)output, DACChannel);
}

static const iocshArg initPIDArg0 = { "serverName",iocshArgString};
static const iocshArg initPIDArg1 = { "quadEMName",iocshArgString};
static const iocshArg initPIDArg2 = { "quadEMChannel",iocshArgInt};
static const iocshArg initPIDArg3 = { "dacName",iocshArgString};
static const iocshArg initPIDArg4 = { "DACChannel",iocshArgInt};
static const iocshArg initPIDArg5 = { "queueSize",iocshArgInt};
static const iocshArg * const initPIDArgs[6] = {&initPIDArg0,
                                                &initPIDArg1,
                                                &initPIDArg2,
                                                &initPIDArg3,
                                                &initPIDArg4,
                                                &initPIDArg5};
static const iocshFuncDef initPIDFuncDef = {"initPID",6,initPIDArgs};
static void initPIDCallFunc(const iocshArgBuf *args)
{
    initQuadEMPID(args[0].sval, args[1].sval, 
                  args[2].ival, args[3].sval, 
                  args[4].ival, args[5].ival);
}

void quadEMPIDRegister(void)
{
    iocshRegister(&initPIDFuncDef,initPIDCallFunc);
}

epicsExportRegistrar(quadEMPIDRegister);


