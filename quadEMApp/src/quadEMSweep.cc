//quadEMSweep.cc

/*
    Author: Mark Rivers
    04/18/03  MLR  Created, based on ip330Sweep.cc
*/

#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#include <epicsTypes.h>
#include <epicsTime.h>
#include <iocsh.h>
#include <epicsExport.h>
#include <epicsThread.h>

#include "Message.h"

#include "fastSweep.h"
#include "quadEM.h"

class quadEMSweep : public fastSweep
{
public:
    quadEMSweep(quadEM *pquadEM, int maxPoints);
    double setMicroSecondsPerPoint(double microSeconds);
    double getMicroSecondsPerPoint();
private:
    //Callback function from quadEM
    static void callback(void*, quadEMData data); 
    quadEMData averageStore;
    quadEM *pquadEM;
    int numAverage;
    int accumulated;
};

// This C function is needed because we call it from the vxWorks shell
extern "C" int initQuadEMSweep(
    const char *quadEMName, const char *serverName, int maxPoints, int queueSize)
{
    quadEM *pQuadEM = quadEM::findModule(quadEMName);
    if (pQuadEM == NULL) {
       printf("quadEMSweep: can't find quadEM module %s\n", quadEMName);
       return(-1);
    }
    quadEMSweep *pquadEMSweep = new quadEMSweep(pQuadEM, maxPoints);
    fastSweepServer *pFastSweepServer = 
                     new fastSweepServer(serverName, pquadEMSweep, queueSize);
    if (epicsThreadCreate("quadEMSweep",
                          epicsThreadPriorityMedium, 10000,
                          (EPICSTHREADFUNC)fastSweepServer::fastServer,
                          (void*)pFastSweepServer) == NULL)
        printf("%s fastSweepServer thread create failure\n", serverName);
    return(0);
}

quadEMSweep::quadEMSweep(quadEM *pquadEM, int maxPoints) :
            fastSweep(0, 9, maxPoints), pquadEM(pquadEM)
            // There are 10 "channels" for the data: current[4], sum[2], 
            //                                       difference[2], position[2]
{
    pquadEM->registerCallback(callback, (void *)this);
}

void quadEMSweep::callback(void *v, quadEMData newData)
{
    quadEMSweep *t = (quadEMSweep *) v;
    int i;
    
    // No need to average if collecting every point
    if (t->numAverage == 1) {
       t->nextPoint(newData.array);
       return;
    }
    for (i=0; i<4; i++) t->averageStore.current[i] += newData.current[i];
    if (++t->accumulated < t->numAverage) return;
    // We have now collected the desired number of points to average
    for (i=0; i<4; i++) t->averageStore.current[i] /= t->accumulated;
    t->averageStore.computePosition();
    t->nextPoint(t->averageStore.array);
    for (i=0; i<4; i++) t->averageStore.current[i] = 0;
    t->accumulated = 0;
}

double quadEMSweep::setMicroSecondsPerPoint(double microSeconds)
{
    numAverage = (int) (microSeconds /
                        pquadEM->getMicroSecondsPerScan() + 0.5);
    if (numAverage < 1) numAverage = 1;
    accumulated = 0;
    return getMicroSecondsPerPoint();
}   

double quadEMSweep::getMicroSecondsPerPoint()
{
    // Return dwell time in microseconds
    return(pquadEM->getMicroSecondsPerScan() * numAverage);
}

static const iocshArg initSweepArg0 = { "quadEMName",iocshArgString};
static const iocshArg initSweepArg1 = { "serverName",iocshArgString};
static const iocshArg initSweepArg2 = { "maxPoints",iocshArgInt};
static const iocshArg initSweepArg3 = { "queueSize",iocshArgInt};
static const iocshArg * const initSweepArgs[4] = {&initSweepArg0,
                                                  &initSweepArg1,
                                                  &initSweepArg2,
                                                  &initSweepArg3};
static const iocshFuncDef initSweepFuncDef = {"initSweep",4,initSweepArgs};
static void initSweepCallFunc(const iocshArgBuf *args)
{
    initQuadEMSweep(args[0].sval, args[1].sval,
                    args[2].ival, args[3].ival);
}

void quadEMSweepRegister(void)
{
    iocshRegister(&initSweepFuncDef,initSweepCallFunc);
}

epicsExportRegistrar(quadEMSweepRegister);
 
