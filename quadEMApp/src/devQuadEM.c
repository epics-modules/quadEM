/* devQuadEM.c

    Author: Mark Rivers
    Created:  April 21, 2003, based on devAiIp330Scan.cc
    July 7, 2004 MLR Converted from MPF and C++ to asyn and C
*/

/**********************************************************************/
/** Brief Description of device support                              **/
/**                                                                  **/
/** This device support does I/O to the APS Quad electrometer.       **/
/**                                                                  **/
/** Record type  Signal  Parm Field                                  **/
/**                                                                  **/
/**    ai        0-3    "@$(PORT)" Current                           **/
/**    ai        4-5    "@$(PORT)" Sum                               **/
/**    ai        6-7    "@$(PORT)" Difference                        **/
/**    ai        8-9    "@$(PORT)" Position                          **/
/**    ao        0      "@$(PORT) CONVERSION"                        **/
/**    ao        0      "@$(PORT) OFFSET"                            **/
/**    mbbo      0      "@$(PORT) GAIN"                              **/
/**    mbbo      0-3    "@$(PORT) PINGPONG"                          **/
/**                                                                  **/
/** Signal is 0-9 for the ai input records.                          **/
/** Signal is specified by the electrometer (0,1,2,3) for            **/
/** OFFSET or PINGPONG, and is always 0 for CONVERSION and GAIN.     **/
/**********************************************************************/

#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#include <dbAccess.h>
#include <dbDefs.h>
#include <link.h>
#include <dbCommon.h>
#include <aiRecord.h>
#include <aoRecord.h>
#include <mbboRecord.h>
#include <recSup.h>
#include <devSup.h>
#include <dbScan.h>
#include <cantProceed.h>
#include <epicsPrint.h>
#include <epicsMutex.h>
#include <epicsExport.h>
#include <asynDriver.h>
#include <asynEpicsUtils.h>
#include <asynInt32Callback.h>

#include <recGbl.h>
#include <alarm.h>

#include "asynQuadEM.h"


typedef enum {recTypeAi, recTypeAo, recTypeMbbo} recType;

typedef enum {
    READ_CURRENT,
    READ_SUM,
    READ_DIFFERENCE,
    READ_POSITION,
    WRITE_GAIN,
    WRITE_GO,
    WRITE_CONVERSION,
    WRITE_PULSE,
    WRITE_PERIOD,
    WRITE_OFFSET,
    COMPUTE_AVERAGE,
    PING_PONG
} devQuadEMCommand;

typedef struct devQuadEMPvt {
    asynUser   *pasynUser;
    asynInt32Callback *int32Callback;
    void       *int32CallbackPvt;
    asynQuadEM *pasynQuadEM;
    void       *quadEMPvt;
    epicsMutexId mutexId;
    int        channel;
    int        command;
    recType    recType;
    double     sum;
    int        numAverage;
} devQuadEMPvt;

typedef struct dsetQuadEM {
    long       number;
    DEVSUPFUN  report;
    DEVSUPFUN  init;
    DEVSUPFUN  init_record;
    DEVSUPFUN  get_ioint_info;
    DEVSUPFUN  io;
    DEVSUPFUN  convert;
} dsetQuadEM;

static long initCommon(dbCommon *pr, DBLINK *plink, userCallback callback,
                       recType rt, char **up);
static long queueRequest(dbCommon *pr);

static long initAi(aiRecord *pr);
static void callbackAi(asynUser *pasynUser);
static void dataCallbackAi(void *drvPvt, epicsInt32 value);
static long convertAi(aiRecord *pai, int pass);
static long initAo(aoRecord *pr);
static void callbackAo(asynUser *pasynUser);
static long convertAo(aoRecord *pao, int pass);
static long initMbbo(mbboRecord *pr);
static void callbackMbbo(asynUser *pasynUser);

dsetQuadEM devAiQuadEM = {6, 0, 0, initAi, 0, queueRequest, convertAi};
dsetQuadEM devAoQuadEM = {6, 0, 0, initAo, 0, queueRequest, convertAo};
dsetQuadEM devMbboQuadEM = {6, 0, 0, initMbbo, 0, queueRequest, 0};

epicsExportAddress(dset, devAiQuadEM);
epicsExportAddress(dset, devAoQuadEM);
epicsExportAddress(dset, devMbboQuadEM);


static long initCommon(dbCommon *pr, DBLINK *plink, userCallback callback,
                       recType rt, char **up)
{
    char *port;
    devQuadEMPvt *pPvt;
    asynUser *pasynUser=NULL;
    asynInterface *pasynInterface;
    asynStatus status;

    pPvt = callocMustSucceed(1, sizeof(devQuadEMPvt), 
                             "devQuadEM::initCommon");
    pr->dpvt = pPvt;
    pPvt->recType = rt;
    pPvt->mutexId = epicsMutexCreate();
    pasynUser = pasynManager->createAsynUser(callback, 0);
    pPvt->pasynUser = pasynUser;
    pasynUser->userPvt = pr;

    status = pasynEpicsUtils->parseLink(pasynUser, plink, 
                                        &port, &pPvt->channel, up);
    if (status != asynSuccess) {
        errlogPrintf("devQuadEM::initCommon %s bad link %s\n", 
                     pr->name, pasynUser->errorMessage);
        goto bad;
    }

    status = pasynManager->connectDevice(pasynUser, port, pPvt->channel);
    if (status != asynSuccess) {
        asynPrint(pasynUser, ASYN_TRACE_ERROR,
                  "devQuadEM::initCommon, error in connectDevice %s\n",
                  pasynUser->errorMessage);
        goto bad;
    }
    pasynInterface = pasynManager->findInterface(pasynUser, asynQuadEMType, 1);
    if (!pasynInterface) {
        asynPrint(pasynUser, ASYN_TRACE_ERROR,
                  "devQuadEM::initCommon, cannot find quadEM interface %s\n",
                  pasynUser->errorMessage);
        goto bad;
    }
    pPvt->pasynQuadEM = (asynQuadEM *)pasynInterface->pinterface;
    pPvt->quadEMPvt = pasynInterface->drvPvt;
    pasynInterface = pasynManager->findInterface(pasynUser, 
                                                 asynInt32CallbackType, 1);
    if (!pasynInterface) {
        asynPrint(pasynUser, ASYN_TRACE_ERROR,
                  "devQuadEM::initCommon, cannot find int32Callback interface %s\n",
                  pasynUser->errorMessage);
        goto bad;
    }
    pPvt->int32Callback = (asynInt32Callback *)pasynInterface->pinterface;
    pPvt->int32CallbackPvt = pasynInterface->drvPvt;
    return(0);
bad:
    pr->pact = 1;
    return(-1);
}

static long queueRequest(dbCommon *pr)
{
    devQuadEMPvt *pPvt = (devQuadEMPvt *)pr->dpvt;

    pasynManager->queueRequest(pPvt->pasynUser, 0, 0);
    return(0);
}


static long initAi(aiRecord *pai)
{
    char *up;
    devQuadEMPvt *pPvt;
    int status;

    status = initCommon((dbCommon *)pai, &pai->inp, callbackAi, recTypeAi, &up);
    if (status) return 0;

    pPvt = (devQuadEMPvt *)pai->dpvt;
    if (pPvt->channel < 0 || pPvt->channel > 9) {
        errlogPrintf("devQuadEM::initAi Invalid signal #: %s = %dn", 
                     pai->name, pPvt->channel);
        pai->pact=1;
    }
    pPvt->int32Callback->registerCallbacks(pPvt->int32CallbackPvt,
                                          pPvt->pasynUser, 
                                          dataCallbackAi, 0, pPvt);
    convertAi(pai, 1);
    return(0);
}

static void dataCallbackAi(void *drvPvt, epicsInt32 value)
{
    devQuadEMPvt *pPvt = (devQuadEMPvt *)drvPvt;

    epicsMutexLock(pPvt->mutexId);
    pPvt->numAverage++;
    pPvt->sum += value;
    epicsMutexUnlock(pPvt->mutexId);
}

static void callbackAi(asynUser *pasynUser)
{
    aiRecord *pai = (aiRecord *)pasynUser->userPvt;
    devQuadEMPvt *pPvt = (devQuadEMPvt *)pai->dpvt;
    int data;

    epicsMutexLock(pPvt->mutexId);
    if (pPvt->numAverage == 0) pPvt->numAverage = 1;
    data = pPvt->sum/pPvt->numAverage + 0.5;
    pPvt->numAverage = 0;
    pPvt->sum = 0.;
    pai->rval = data;
    epicsMutexUnlock(pPvt->mutexId);
    asynPrint(pPvt->pasynUser, ASYN_TRACEIO_DEVICE,
              "devQuadEM::readAi %s value=%d\n",
              pai->name, pai->rval);
    pai->udf=0;
}

static long convertAi(aiRecord *pai, int pass)
{
    pai->eslo=(pai->eguf-pai->egul)/(double)0xffff;
    return 0;
}


static long initAo(aoRecord *pao)
{
    char *up;
    devQuadEMPvt *pPvt;
    int status;
    status = initCommon((dbCommon *)pao, &pao->out, callbackAo, recTypeAo, &up);
    if (status) return 0;

    pPvt = (devQuadEMPvt *)pao->dpvt;
    if (strcmp(up, "CONVERSION") == 0) {
        pPvt->command = WRITE_CONVERSION;
    } else if (strcmp(up, "PERIOD") == 0) {
        pPvt->command = WRITE_PERIOD;
    } else if (strcmp(up, "GO") == 0) {
        pPvt->command = WRITE_GO;
    } else if (strcmp(up, "PULSE") == 0) {
        pPvt->command = WRITE_PULSE;
    } else if (strcmp(up, "OFFSET") == 0) {
        pPvt->command = WRITE_OFFSET;
    } else if (strcmp(up, "AVERAGE") == 0) {
        pPvt->command = COMPUTE_AVERAGE;
    } else {
        errlogPrintf("devQuadEM::initAo Invalid parm field: %s = %s\n", 
                      pao->name, up);
        pao->pact=1;
    }

    if (((pPvt->command == WRITE_OFFSET) && (pPvt->channel >= 4)) ||
        ((pPvt->command != WRITE_OFFSET) && (pPvt->channel >= 1))) {
        errlogPrintf("devQuadEM::initAo Invalid signal #: %s = %dn", 
                     pao->name, pPvt->channel);
        pao->pact=1;
    }
    convertAo(pao, 1);
    return(0);
}

static void callbackAo(asynUser *pasynUser)
{
    aoRecord *pao = (aoRecord *)pasynUser->userPvt;
    devQuadEMPvt *pPvt = (devQuadEMPvt *)pao->dpvt;

    asynPrint(pPvt->pasynUser, ASYN_TRACEIO_DEVICE,
              "devQuadEM::writeAo %s command=%d, channel=%d, value=%d\n",
              pao->name, pPvt->command, pPvt->channel, pao->val);
    switch (pPvt->command) {
    case WRITE_OFFSET:
        pPvt->pasynQuadEM->setOffset(pPvt->quadEMPvt, pPvt->pasynUser, 
                                     pPvt->channel, pao->val);
        break;
    case WRITE_CONVERSION:
        pPvt->int32Callback->setCallbackInterval(pPvt->quadEMPvt, 
                                                 pPvt->pasynUser,
                                                 (double) pao->val / 1.e6);
        break;
    case WRITE_PERIOD:
        pPvt->pasynQuadEM->setPeriod(pPvt->quadEMPvt, pPvt->pasynUser, 0xffff);
        break;
    case WRITE_GO:
        pPvt->pasynQuadEM->go(pPvt->quadEMPvt, pPvt->pasynUser);
        break;
    case WRITE_PULSE:
        pPvt->pasynQuadEM->setPulse(pPvt->quadEMPvt, pPvt->pasynUser, pao->val);
        break;
    }
}

static long convertAo(aoRecord *pao, int pass)
{
    pao->eslo=(pao->eguf-pao->egul)/(double)0xffff;
    return 0;
}


static long initMbbo(mbboRecord *pmbbo)
{
    char *up;
    devQuadEMPvt *pPvt;
    int status;

    status = initCommon((dbCommon *)pmbbo, &pmbbo->out, callbackMbbo,
                        recTypeMbbo, &up);
    if (status) return 0;

    pPvt = (devQuadEMPvt *)pmbbo->dpvt;
    if (strcmp(up, "GAIN") == 0) {
        pPvt->command = WRITE_GAIN;
    } else if (strcmp(up, "PINGPONG") == 0) {
        pPvt->command = PING_PONG;
    } else {
        errlogPrintf("devQuadEM::initMbbo Invalid parm field: %s = %s\n", 
                      pmbbo->name, up);
        pmbbo->pact=1;
    }
      
    if (((pPvt->command == WRITE_GAIN) && (pPvt->channel >= 1)) ||
        ((pPvt->command == PING_PONG)  && (pPvt->channel >= 4))) {
        errlogPrintf("devQuadEM::initMbbo Invalid signal #: %s = %dn", 
                     pmbbo->name, pPvt->channel);
        pmbbo->pact=1;
    }
    return(0);
}

static void callbackMbbo(asynUser *pasynUser)
{
    mbboRecord *pmbbo = (mbboRecord *)pasynUser->userPvt;
    devQuadEMPvt *pPvt = (devQuadEMPvt *)pmbbo->dpvt;

    asynPrint(pPvt->pasynUser, ASYN_TRACEIO_DEVICE,
              "devQuadEM::writeMbbo %s command=%d, channel=%d, value=%d\n",
              pmbbo->name, pPvt->command, pPvt->channel, pmbbo->rval);
    switch (pPvt->command) {
    case WRITE_GAIN:
        pPvt->pasynQuadEM->setGain(pPvt->quadEMPvt, pPvt->pasynUser, 
                                    pmbbo->rval);
        break;
    case PING_PONG:
        pPvt->pasynQuadEM->setPingPong(pPvt->quadEMPvt, pPvt->pasynUser, 
                                       pPvt->channel, pmbbo->rval);
        break;
    }
}
