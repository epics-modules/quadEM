//quadEMSweep.cc

/*
    Author: Mark Rivers
    04/18/03  MLR  Created, based on ip330Sweep.cc
*/

#include <vxWorks.h>
#include <iv.h>
#include <stdlib.h>
#include <sysLib.h>
#include <stddef.h>
#include <string.h>
#include <stdio.h>

#include <tickLib.h>

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
static char taskname[] = "quadEMSweep";
extern "C" quadEMSweep *initQuadEMSweep(
    quadEM *pquadEM, const char *serverName, int maxPoints, int queueSize)
{
    quadEMSweep *pquadEMSweep = new quadEMSweep(pquadEM, maxPoints);
    fastSweepServer *pFastSweepServer = 
                     new fastSweepServer(serverName, pquadEMSweep, queueSize);
    int taskId = taskSpawn(taskname,100,VX_FP_TASK,4000,
        (FUNCPTR)fastSweepServer::fastServer,(int)pFastSweepServer,
        0,0,0,0,0,0,0,0,0);
    if(taskId==ERROR)
        printf("%s fastSweepServer taskSpawn Failure\n",
            pFastSweepServer->pMessageServer->getName());
    return(pquadEMSweep);
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
