//quadEMPID.cc

/*
    Author: Mark Rivers
    04/18/03 MLR   Created, based on ip330PID.cc
*/

#include <vxWorks.h>
#include <iv.h>
#include <tickLib.h>
#include <stdio.h>
#include <string.h>

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
static char taskname[] = "quadPID";
extern "C" quadEMPID *initQuadEMPID(const char *serverName, 
         quadEM *pQuadEM, int quadEMChannel, DAC128V *pDAC128V, int DACChannel,
         int queueSize)
{
    quadEMPID *pQuadEMPID = new quadEMPID(pQuadEM, quadEMChannel, pDAC128V, 
                                          DACChannel);
    fastPIDServer *pFastPIDServer = 
                          new fastPIDServer(serverName, pQuadEMPID, queueSize);
    int taskId = taskSpawn(taskname,100,VX_FP_TASK,4000,
        (FUNCPTR)fastPIDServer::fastServer,(int)pFastPIDServer,
        0,0,0,0,0,0,0,0,0);
    if (taskId==ERROR)
        printf("%s fastPIDServer taskSpawn Failure\n",
            pFastPIDServer->pMessageServer->getName());
    return(pQuadEMPID);
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
