/* devQuadEM.cc

    Author: Mark Rivers
    Created:  April 21, 2003, based on devAiIp330Scan.cc

*/

/**********************************************************************/
/** Brief Description of device support                              **/
/**                                                                  **/
/** This device support does I/O to the APS Quad electrometer.       **/
/**                                                                  **/
/** Record type  Signal  Parm Field                                  **/
/**                                                                  **/
/**    ai        0-3    "@$(SERVER) CURRENT"                         **/
/**    ai        0-1    "@$(SERVER) SUM",                            **/
/**    ai        0-1    "@$(SERVER) DIFFERENCE"                      **/
/**    ai        0-1    "@$(SERVER) POSITION"                        **/
/**    ao        0      "@$(SERVER) CONVERSION"                      **/
/**    ao        0      "@$(SERVER) OFFSET"                          **/
/**    mbbo      0      "@$(SERVER) GAIN"                            **/
/**                                                                  **/
/** Signal is specified by the electrometer (0,1,2,3) for CURRENT or **/
/** OFFSET, by signal pair (0,1) for SUM, DIFFERENCE or POSITION,    **/
/** and is always 0 for CONVERSION and GAIN.                         **/
/**********************************************************************/

#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#include <dbAccess.h>
#include <dbDefs.h>
#include <link.h>
#include <epicsPrint.h>
#include <dbCommon.h>
#include <aiRecord.h>
#include <aoRecord.h>
#include <mbboRecord.h>
#include <recSup.h>
#include <dbScan.h>

#include <recGbl.h>
#include <alarm.h>

#include <Message.h>
#include <Int32Message.h>
#include <DevMpf.h>

#include "devQuadEM.h"


class DevAiQuadEM : public DevMpf
{
public:
        DevAiQuadEM(dbCommon*,DBLINK*);
        long startIO(dbCommon* pr);
        long completeIO(dbCommon* pr,Message* m);
        long convert(dbCommon* pr,int pass);
        static long dev_init(void*);
private:
        int channel;
        int command;
};

MAKE_LINCONV_DSET(devAiQuadEM,DevAiQuadEM::dev_init)

DevAiQuadEM::DevAiQuadEM(dbCommon* pr,DBLINK* l) : DevMpf(pr,l,false)
{
    vmeio* io = (vmeio*)&(l->value);
    channel=io->signal;
    aiRecord* pai=(aiRecord*)pr;
    const char* up = getUserParm();
    if (strcmp(up, "CURRENT") == 0) {
        command = READ_CURRENT;
    } else if (strcmp(up, "SUM") == 0) {
        command = READ_SUM;
    } else if (strcmp(up, "DIFFERENCE") == 0) {
        command = READ_DIFFERENCE;
    } else if (strcmp(up, "POSITION") == 0) {
        command = READ_POSITION;
    } else if (strcmp(up, "AVERAGE") == 0) {
        command = COMPUTE_AVERAGE;
    } else {
        pai->pact = 1;          /* make sure we don't process this thing */
        epicsPrintf("devQuadEM: Invalid parm field: ->%s<-\n", pai->name);
    }
      
    if (((command == READ_CURRENT) && (channel >= 4)) ||
        ((command != READ_CURRENT) && (channel >= 2))) {
        pai->pact = 1;          /* make sure we don't process this thing */
        epicsPrintf("devQuadEM: Invalid signal #: ->%s<-\n", pai->name);
    }
    convert(pr, 1);
}

long DevAiQuadEM::startIO(dbCommon* pr)
{
    Int32Message *message = new Int32Message;
    message->extra = command;
    message->address = channel;
    return(sendReply(message));
}

long DevAiQuadEM::completeIO(dbCommon* pr,Message* m)
{
    aiRecord* pai = (aiRecord*)pr;
    if((m->getType()) != messageTypeInt32) {
        epicsPrintf("%s ::completeIO illegal message type %d\n",
                    pai->name,m->getType());
        recGblSetSevr(pai,READ_ALARM,INVALID_ALARM);
        delete m;
        return(MPF_NoConvert);
    }
    Int32Message *pint32Message = (Int32Message *)m;
    if(pint32Message->status) {
        recGblSetSevr(pai,READ_ALARM,INVALID_ALARM);
        delete m;
        return(MPF_NoConvert);
    }
    pai->rval=pint32Message->value;
    pai->udf=0;
    delete m;
    return(MPF_OK);
}

long DevAiQuadEM::convert(dbCommon* pr,int /*pass*/)
{
    aiRecord* pai = (aiRecord*)pr;
    pai->eslo=(pai->eguf-pai->egul)/(double)0xffff;
    return 0;
}

long DevAiQuadEM::dev_init(void* v)
{
    aiRecord* pr = (aiRecord*)v;
    DevAiQuadEM* pDevAiQuadEM = new DevAiQuadEM((dbCommon*)pr,&(pr->inp));
    pDevAiQuadEM->bind();
    return pDevAiQuadEM->getStatus();
}


class DevAoQuadEM : public DevMpf
{
public:
        DevAoQuadEM(dbCommon*,DBLINK*);
        long startIO(dbCommon* pr);
        long completeIO(dbCommon* pr, Message* m);
        static long dev_init(void*);
private:
        int channel;
        int command;
};

MAKE_LINCONV_DSET(devAoQuadEM,DevAoQuadEM::dev_init)

DevAoQuadEM::DevAoQuadEM(dbCommon* pr,DBLINK* l) : DevMpf(pr,l,false)
{
    vmeio* io = (vmeio*)&(l->value);
    channel=io->signal;
    aoRecord* pao=(aoRecord*)pr;
    const char* up = getUserParm();
    if (strcmp(up, "CONVERSION") == 0) {
        command = WRITE_CONVERSION;
    } else if (strcmp(up, "PERIOD") == 0) {
        command = WRITE_PERIOD;
    } else if (strcmp(up, "GO") == 0) {
        command = WRITE_GO;
    } else if (strcmp(up, "PULSE") == 0) {
        command = WRITE_PULSE;
    } else if (strcmp(up, "OFFSET") == 0) {
        command = WRITE_OFFSET;
    } else {
        pao->pact = 1;          /* make sure we don't process this thing */
        epicsPrintf("devQuadEM: Invalid parm field: ->%s<-\n", pao->name);
    }
      
    if (((command == WRITE_OFFSET) && (channel >= 4)) ||
        ((command != WRITE_OFFSET) && (channel >= 1))) {
        pao->pact = 1;          /* make sure we don't process this thing */
        epicsPrintf("devQuadEM: Invalid signal #: ->%s<-\n", pao->name);
    }
}

long DevAoQuadEM::startIO(dbCommon* pr)
{
    Int32Message *message = new Int32Message;
    aoRecord* pao=(aoRecord*)pr;
    message->extra = command;
    message->address = channel;
    switch (command) {
    case WRITE_OFFSET:
        message->value = (int)pao->val;
        break;
    case WRITE_CONVERSION:
        message->value = (int) (pao->val);
        break;
    case WRITE_PERIOD:
        message->value = 0xffff;
        break;
    case WRITE_GO:
        message->value = 1;
        break;
    case WRITE_PULSE:
        message->value = (int)pao->val;
        break;
    }
    return(sendReply(message));
}

long DevAoQuadEM::completeIO(dbCommon* pr,Message* m)
{
    aoRecord* pao = (aoRecord*)pr;
    /* This function does not need to do anything */
    pao->udf=0;
    delete m;
    return(MPF_OK);
}

long DevAoQuadEM::dev_init(void* v)
{
    aoRecord* pr = (aoRecord*)v;
    DevAoQuadEM* pDevAoQuadEM = new DevAoQuadEM((dbCommon*)pr,&(pr->out));
    pDevAoQuadEM->bind();
    return pDevAoQuadEM->getStatus();
}

class DevMbboQuadEM : public DevMpf
{
public:
        DevMbboQuadEM(dbCommon*,DBLINK*);
        long startIO(dbCommon* pr);
        long completeIO(dbCommon* pr,Message* m);
        static long dev_init(void*);
private:
        int channel;
        int command;
};

MAKE_LINCONV_DSET(devMbboQuadEM,DevMbboQuadEM::dev_init)

DevMbboQuadEM::DevMbboQuadEM(dbCommon* pr,DBLINK* l) : DevMpf(pr,l,false)
{
    vmeio* io = (vmeio*)&(l->value);
    channel=io->signal;
    mbboRecord* pmbbo=(mbboRecord*)pr;
    const char* up = getUserParm();
    if (strcmp(up, "GAIN") == 0) {
        command = WRITE_GAIN;
    } else {
        pmbbo->pact = 1;          /* make sure we don't process this thing */
        epicsPrintf("devQuadEM: Invalid parm field: ->%s<-\n", pmbbo->name);
    }
      
    if (((command == WRITE_GAIN) && (channel >= 1)) ||
        ((command != WRITE_GAIN) && (channel >= 0))) {
        pmbbo->pact = 1;          /* make sure we don't process this thing */
        epicsPrintf("devQuadEM: Invalid signal #: ->%s<-\n", pmbbo->name);
    }
}

long DevMbboQuadEM::startIO(dbCommon* pr)
{
    Int32Message *message = new Int32Message;
    mbboRecord* pmbbo=(mbboRecord*)pr;
    message->extra = command;
    message->address = channel;
    switch (command) {
    case WRITE_GAIN:
        message->value = pmbbo->rval;
        break;
    }
    return(sendReply(message));
}

long DevMbboQuadEM::completeIO(dbCommon* pr,Message* m)
{
    mbboRecord* pmbbo = (mbboRecord*)pr;
    /* This function does not need to do anything */
    pmbbo->udf=0;
    delete m;
    return(MPF_OK);
}

long DevMbboQuadEM::dev_init(void* v)
{
    mbboRecord* pr = (mbboRecord*)v;
    DevMbboQuadEM* pDevMbboQuadEM = new DevMbboQuadEM((dbCommon*)pr,&(pr->out));
    pDevMbboQuadEM->bind();
    return pDevMbboQuadEM->getStatus();
}
