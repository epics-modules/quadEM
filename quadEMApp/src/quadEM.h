//quadEM.h

/*
    Author:  Mark Rivers
    Created: April 10, 2003, based on Ip330.h

*/

#ifndef quadEMH
#define quadEMH

#include <ipUnidig.h>

/* This is the scale factor to go from (difference/sum) to position.  The total
 * range will be -32767 to 32767, which preserves the full 16 bit range of the
 * device
*/
#define QUAD_EM_POS_SCALE 32767
#define MAX_RAW 8
#define MAX_MESSAGES 100

class quadEMData {
public:
    quadEMData();
    quadEMData(int value);
    int raw[8];
    int current[4];
    int offset[4];
    int pingpong[4];
    int sum[2];
    int difference[2];
    int position[2];
    int array[10];
    void computeCurrent();
    void computePosition();
};

typedef void (*quadEMCallback)(void *pvt, quadEMData data);
class quadEMClient {
public:
   quadEMCallback callback;
   void *pvt;
};

class quadEM
{
public:
    quadEM(const char *name, unsigned short *baseAddr, 
           int fiberChannel, int microSecondsPerScan, 
           int maxClients, IpUnidig *pIpUnidig, int unidigChan);
    quadEMData getData();
    float setMicroSecondsPerScan(float microSeconds);
    float getMicroSecondsPerScan();
    void setOffset(int channel, int offset);
    void setPingPong(int channel, int pingpong);
    void setPeriod(int period);
    void setGain(int gain);
    void setPulse(int pulse);
    void go();
    static quadEM *findModule(const char *name);
    static void poller(quadEM*);  // Polling routine if no IP-Unidig
    static void intTask(quadEM*);  // Task that waits for interrupts
    int registerCallback(quadEMCallback callback, void *pvt);
private:
    unsigned short *baseAddress;
    IpUnidig *pIpUnidig;
    static void intFunc(void*, unsigned int mask); // Interrupt function
    void write(int command, int value);
    void read(int *raw);
    float getActualMicroSecondsPerScan();
    float setTimeRegs(float microSecondsPerScan);
    quadEMData data;
    int maxClients;
    int numClients;
    epicsMessageQueue *intMsgQ;
    quadEMClient *client;
    float actualMicroSecondsPerScan;
};

#endif //quadEMH
