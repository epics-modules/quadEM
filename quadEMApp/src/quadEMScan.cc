//quadEMScan.cc

/*
   Author: Mark Rivers
   Date:   April 11, 2003  Based on earlier ip330Scan.cc
*/

#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#include <epicsTypes.h>
#include <iocsh.h>
#include <epicsExport.h>
#include <epicsThread.h>

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


extern "C" int initQuadEMScan(
    const char *quadEMName, const char *serverName, int queueSize)
{
    quadEM *pQuadEM = quadEM::findModule(quadEMName);
    if (pQuadEM == NULL) {
       printf("quadEMScan: can't find quadEM module %s\n", quadEMName);
       return(-1);
    }
    quadEMScan *pQuadEMScan = new quadEMScan(pQuadEM);
    if(!pQuadEMScan) return(0);
    quadEMScanServer *pQuadEMScanServer = new quadEMScanServer;
    pQuadEMScanServer->pQuadEMScan = pQuadEMScan;
    pQuadEMScanServer->pMessageServer = new MessageServer(serverName,queueSize);
    if (epicsThreadCreate("quadEMScan",
                          epicsThreadPriorityMedium, 10000,
                          (EPICSTHREADFUNC)quadEMScanServer::quadEMServer,
                          (void*)pQuadEMScanServer) == NULL)
        printf("%s quadEMServer thread create failure\n", serverName);
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

static const iocshArg initScanArg0 = { "quadEMName",iocshArgString};
static const iocshArg initScanArg1 = { "serverName",iocshArgString};
static const iocshArg initScanArg2 = { "queueSize",iocshArgInt};
static const iocshArg * const initScanArgs[3] = {&initScanArg0,
                                                  &initScanArg1,
                                                  &initScanArg2};
static const iocshFuncDef initScanFuncDef = {"initScan",3,initScanArgs};
static void initScanCallFunc(const iocshArgBuf *args)
{
    initQuadEMScan(args[0].sval, args[1].sval,
                   (int) args[2].sval);
}

void quadEMScanRegister(void)
{
    iocshRegister(&initScanFuncDef,initScanCallFunc);
}

epicsExportRegistrar(quadEMScanRegister);
