//quadEMScan.cc

/*
   Author: Mark Rivers
   Date:   April 11, 2003  Based on earlier ip330Scan.cc
*/

#include <vxWorks.h>
#include <iv.h>
#include <stdlib.h>
#include <stddef.h>
#include <string.h>
#include <stdio.h>

#include "Message.h"
#include "Int32Message.h"
#include "mpfType.h"

#include "quadEM.h"
#include "devQuadEM.h"

extern "C"
{
#ifdef NODEBUG
#define DEBUG(l,f,v) ;
#else
#define DEBUG(l,f,v...) { if(l<quadEMScanDebug) printf(f,## v); }
#endif
volatile int quadEMScanDebug = 0;
}


class quadEMScan
{
public:
   quadEMScan(quadEM *pQuadEM);
   void computeAverage();
   quadEMData data;
   quadEM *pQuadEM;
private:
   quadEMData averageStore;
   int accumulated;
   static void callback(void*, quadEMData data); //Callback function from quadEM
};
                                                                 
class quadEMScanServer {
public:
    quadEMScan *pQuadEMScan;
    MessageServer *pMessageServer;
    static void quadEMServer(quadEMScanServer *);
};


static char taskname[] = "quadEMScan";
extern "C" int initQuadEMScan(
    quadEM *pQuadEM, const char *serverName, int queueSize)
{
    quadEMScan *pQuadEMScan = new quadEMScan(pQuadEM);
    if(!pQuadEMScan) return(0);
    quadEMScanServer *pQuadEMScanServer = new quadEMScanServer;
    pQuadEMScanServer->pQuadEMScan = pQuadEMScan;
    pQuadEMScanServer->pMessageServer = new MessageServer(serverName,queueSize);
    int taskId = taskSpawn(taskname,100,VX_FP_TASK,4000,
        (FUNCPTR)quadEMScanServer::quadEMServer,(int)pQuadEMScanServer,
        0,0,0,0,0,0,0,0,0);
    if(taskId==ERROR)
        printf("%s quadEMServer taskSpawn Failure\n",
            pQuadEMScanServer->pMessageServer->getName());
    return(0);
}

quadEMScan::quadEMScan(quadEM *pQuadEM) 
           : data(0), pQuadEM(pQuadEM), averageStore(0), accumulated(0)
{
   pQuadEM->registerCallback(callback, (void *)this);
}

void quadEMScan::callback(void *v, quadEMData newData)
{
    quadEMScan *t = (quadEMScan *) v;
    int i;

    t->accumulated++;
    for (i=0; i<4; i++) t->averageStore.current[i] += newData.current[i];
}

void quadEMScan::computeAverage()
{
   int i;
   
   if (accumulated == 0) accumulated = 1;
   for (i=0; i<4; i++) {
      data.current[i] = averageStore.current[i] / accumulated;
      averageStore.current[i] = 0;
   }
   data.computePosition();
   accumulated = 0;
}

void quadEMScanServer::quadEMServer(quadEMScanServer *pQuadEMScanServer)
{
   while(true) {
      MessageServer *pMessageServer = pQuadEMScanServer->pMessageServer;
      quadEMScan *pQuadEMScan = pQuadEMScanServer->pQuadEMScan;
      pMessageServer->waitForMessage();
      Message *inmsg;
      while((inmsg = pMessageServer->receive())) {
         if(inmsg->getType()!=messageTypeInt32) {
            printf("%s quadEMServer got illegal message type %d\n",
               pMessageServer->getName(), inmsg->getType());
            delete inmsg;
         } else {
            Int32Message *pmessage = (Int32Message *)inmsg;
            pmessage->status = 0;
            int channel = pmessage->address;
            int command = pmessage->extra;
            int value   = pmessage->value;
            DEBUG(1, "quadEMScanServer: command=%d, channel=%d, value=%d\n",
               command, channel, value);
            switch(command) {
               case (COMPUTE_AVERAGE):
                  pQuadEMScan->computeAverage();
                  break;
               case (READ_CURRENT):
                  pmessage->value = (int32)pQuadEMScan->data.current[channel];
                  break;
               case (READ_SUM):
                  pmessage->value = (int32)pQuadEMScan->data.sum[channel];
                  break;
               case (READ_DIFFERENCE):
                  pmessage->value = (int32)pQuadEMScan->
                                                      data.difference[channel];
                  break;
               case (READ_POSITION):
                  pmessage->value = (int32)pQuadEMScan->data.position[channel];
                  break;
               case (WRITE_GAIN):
                  pQuadEMScan->pQuadEM->setGain(value);
                  break;
               case (WRITE_GO):
                  pQuadEMScan->pQuadEM->go();
                  break;
               case (WRITE_CONVERSION):
                  pQuadEMScan->pQuadEM->setMicroSecondsPerScan((double) value);
                  break;
               case (WRITE_PULSE):
                  pQuadEMScan->pQuadEM->setPulse(value);
                  break;
               case (WRITE_PERIOD):
                  pQuadEMScan->pQuadEM->setPeriod(value);
                  break;
               case (WRITE_OFFSET):
                  pQuadEMScan->pQuadEM->setOffset(channel, value);
                  break;
            }
            pMessageServer->reply(pmessage);
         }
      }
   }
}
