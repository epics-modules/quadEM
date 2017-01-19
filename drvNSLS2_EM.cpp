#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/mman.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <math.h>

#define LEDS 5
#define FPGAVER 7
#define SA_RATE 8
#define IRQ_ENABLE 9
#define RAW 12
#define RAW_A 12
#define RAW_B 13
#define RAW_C 14
#define RAW_D 15
#define FRAME_NO 16
#define SA_RATE_DIV 19
#define GAINREG 28
#define HV_BIAS 36
#define AVG 44
#define AVG_A 44 
#define AVG_B 45 
#define AVG_C 46 
#define AVG_D 47
#define DACS 72 

virtual asynStatus setAcquire(epicsInt32 value);
virtual asynStatus setAveragingTime(epicsFloat64 value);
virtual asynStatus setBiasVoltage(epicsFloat64 value);
virtual asynStatus setRange(epicsInt32 value);

/*******************************************
* Read ADC
*
*
*********************************************/
int NSLS2_EM::readMeter(int *adcbuf)
{

    int i, val;

    for (i=0;i<=3;i++) {
        val = fpgabase[AVG+i];  
        //printf("i=%d\tval=%d\n",i,val);
        *adcbuf++ = val;
    } 
}

/*******************************************
* mmap fpga register space
* returns pointer fpgabase
*
********************************************/
void NSLS2_EM::mmap_fpga()
{
   int fd;


   fd = open("/dev/mem",O_RDWR|O_SYNC);
   if (fd < 0) {
      printf("Can't open /dev/mem\n");
      exit(1);
   }

   fpgabase = (unsigned int *) mmap(0,255,PROT_READ|PROT_WRITE,MAP_SHARED,fd,0x43C00000);

   if (fpgabase == NULL) {
      printf("Can't map FPGA space\n");
      exit(1);
   }

}

// Global variable containing pointer to your driver object
class drvNLS2_EM *pdrvNSLS2_EM;

// C callback function called by Linux when an interrupt occurs.  
// It calls the callbackFunc in your C++ driver.
static void frame_done()
{
    pdrvNSLS2_EM->callbackFunc();
}

// The constructor for your driver
drvNSLS2_EM::drvNSLS2_EM(const char *portName, int ringBufferSize)
{
// Set the global pointer
 pdrvNSLS2_EM = this;      
 
 asynStatus status;
 const char *functionName = "drvNSLS2_EM";

// Current ranges in microamps
 ranges_[0]=1;
 ranges_[1]=10;
 ranges_[2]=100;
 ranges_[3]=1000;
 ranges_[4]=50000;

// Initialize Linux driver, set callback function
 pl_open(&intfd);
 signal(SIGIO, &frame_done);
 fcntl(intfd, F_SETOWN, getpid());
 int oflags = fcntl(intfd, F_GETFL);
 fcntl(intfd, F_SETFL, oflags | FASYNC);

// set up register memory map
 mmap_fpga();

 callParamCallbacks();
}

//  Callback function in driver
void drvNSLS2_EM::callbackFunc()
{
    int input[QE_MAX_INPUTS];
    int i;
    static const char *functionName="callbackFunc";

    lock();
    /* Read the new data as integers */
    readMeter(input);
   /* Convert to double) */
    for (i=0; i<QE_MAX_INPUTS; i++) {
        rawData_[i] += input[i];
    }
    computePositions(rawData_);
    unlock();
}

// Other functions

asynStatus drvNSLS2_EM::setAquire(epicsFloat64 value)
{
	// 1=start acquire, 0=stop.
	fpgabase[IRQ_ENABLE]=value;
	return(0);
}

asynStatus drvNSLS2_EM::setAveragingTime(epicsFloat64 value)
{
	return(0);
}

asynStatus drvNSLS2_EM::setBiasVoltage(epicsFloat64 value)
{
	fpgabase[HV_BIAS] = (int) (value *50.0/65535);
	return(0);
}

asynStatus drvNSLS2_EM::setRange(epicsInt32 value)
{
	fpgabase[GAINREG] = value;     
	return(0);
}

/* That's all you need to send the data to the quadEM base class.  
You need to write a readMeter() function (or some other name) 
to actually read the ADC registers.  You also need to implement 
any of these quadEM base class functions that apply to your device: */

//    virtual asynStatus readStatus()=0;
//    virtual asynStatus reset()=0;
//    virtual asynStatus setAcquire(epicsInt32 value);
//    virtual asynStatus setAcquireMode(epicsInt32 value);
//    virtual asynStatus setAveragingTime(epicsFloat64 value);
//    virtual asynStatus setBiasState(epicsInt32 value);
//    virtual asynStatus setBiasVoltage(epicsFloat64 value);
//    virtual asynStatus setBiasInterlock(epicsInt32 value);
//    virtual asynStatus setPingPong(epicsInt32 value);
//    virtual asynStatus setIntegrationTime(epicsFloat64 value);
//    virtual asynStatus setNumChannels(epicsInt32 value);
//    virtual asynStatus setNumAcquire(epicsInt32 value);
//    virtual asynStatus setRange(epicsInt32 value);
//    virtual asynStatus setReadFormat(epicsInt32 value);
//    virtual asynStatus setResolution(epicsInt32 value);
//    virtual asynStatus setTriggerMode(epicsInt32 value);
//    virtual asynStatus setTriggerPolarity(epicsInt32 value);
//    virtual asynStatus setValuesPerRead(epicsInt32 value);
//    virtual asynStatus triggerCallbacks();

/* If your device requires parameters that are not included in the 
base class then you need to implement the writeInt32 and/or writeFloat64 
methods to handle the driver-specific parameters, and then call 
quadEM::writeInt32 or quadEM::writeFloat64 so it can handle the 
standard parameters. */
/* Configuration routine.  Called directly, or from the iocsh function below */

extern "C" {

/** EPICS iocsh callable function to call constructor for the drvNSLS_EM class.
  * \param[in] portName The name of the asyn port driver to be created.
  * \param[in] broadcastAddress The broadcast address of the network with this module
  * \param[in] moduleID The module ID of this module, set with rotary switch on module
  * \param[in] ringBufferSize The number of samples to hold in the input ring buffer.
  *            This should be large enough to hold all the samples between reads of the
  *            device, e.g. 1 ms SampleTime and 1 second read rate = 1000 samples.
  *            If 0 then default of 2048 is used.
  */
int drvNSLS_EMConfigure(const char *portName, const char *broadcastAddress, int moduleID, int ringBufferSize)
{
    new drvNSLS_EM(portName, broadcastAddress, moduleID, ringBufferSize);
    return(asynSuccess);
}


/* EPICS iocsh shell commands */

static const iocshArg initArg0 = { "portName", iocshArgString};
static const iocshArg initArg1 = { "broadcast address", iocshArgString};
static const iocshArg initArg2 = { "module ID", iocshArgInt};
static const iocshArg initArg3 = { "ring buffer size",iocshArgInt};
static const iocshArg * const initArgs[] = {&initArg0,
                                            &initArg1,
                                            &initArg2,
                                            &initArg3};
static const iocshFuncDef initFuncDef = {"drvNSLS_EMConfigure",4,initArgs};
static void initCallFunc(const iocshArgBuf *args)
{
    drvNSLS_EMConfigure(args[0].sval, args[1].sval, args[2].ival, args[3].ival);
}

void drvNSLS_EMRegister(void)
{
    iocshRegister(&initFuncDef,initCallFunc);
}

epicsExportRegistrar(drvNSLS_EMRegister);

}



