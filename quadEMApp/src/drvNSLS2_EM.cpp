#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <unistd.h>
#include <netinet/in.h>
#include <sys/mman.h>
#include <fcntl.h>
#include <signal.h>
#include <sys/stat.h>
#include <math.h>

#include <epicsTypes.h>
#include <epicsTime.h>
#include <epicsThread.h>
#include <epicsString.h>
#include <epicsEvent.h>
#include <asynOctetSyncIO.h>
#include <asynCommonSyncIO.h>
#include <drvAsynIPPort.h>
#include <iocsh.h>

#include <epicsExport.h>
#include "drvNSLS2_EM.h"


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

#define FREQ 500000.0

#define DEVNAME "/dev/vipic"

// Define SIMULATION_MODE to run on a system without the FPGA
//#define SIMULATION_MODE 1

// Define POLLING_MODE to poll the ADCs rather than using interrupts
//#define POLLING_MODE 1
#define POLL_TIME 0.001 
#define NOISE 1000.


static const char *driverName = "drvNSLS2_EM";

// Global variable containing pointer to your driver object
class drvNSLS2_EM *pdrvNSLS2_EM;


/*******************************************
* Read ADC
*
*
*********************************************/
asynStatus drvNSLS2_EM::readMeter(int *adcbuf)
{

    int i, val;
    static const char *functionName = "readMeter";

    for (i=0;i<=3;i++) {
#ifdef SIMULATION_MODE
        getIntegerParam(i, P_DAC, &val)
        val =  val + NOISE * ((double)rand() / (double)(RAND_MAX) -  0.5);  
#else
        val = fpgabase_[AVG+i];  
#endif
        asynPrint(pasynUserSelf, ASYN_TRACEIO_DRIVER, 
            "%s::%s i=%d val=%d\n",
            driverName, functionName, i, val);
        *adcbuf++ = val;
    } 
    return(asynSuccess);
}

asynStatus drvNSLS2_EM::setDAC(int channel, int value)
{
    int dacVolts;
    static const char *functionName = "setDAC";

    //append which of the 4 dacs to bits 18-16 (see data sheet)
    dacVolts = (channel << 16) | value; 
    // First must write the Power Control Register to turn on DAC outputs
    fpgabase_[DACS] = 0x10001F;  //see data sheet for bit def.
    epicsThreadSleep(0.001); 
    // Set Output Select Range to -10v to +10v
    fpgabase_[DACS] = 0x0C0004;   //see data sheet for bit def.
    epicsThreadSleep(0.001); 
    // Write the DAC voltage
    fpgabase_[DACS] = dacVolts;

    asynPrint(pasynUserSelf, ASYN_TRACEIO_DRIVER,
        "%s::%s channel=%d val=%d\n",
        driverName, functionName, channel, value);
    return(asynSuccess);
}

/*******************************************
* open interrupt driver 
*
********************************************/
asynStatus drvNSLS2_EM::pl_open(int *fd) {
    if ( (*fd = open(DEVNAME, O_RDWR)) <= 0 ) {
        perror(__func__);
        return(asynError);
    }

    return(asynSuccess);
}


/*******************************************
* mmap fpga register space
* returns pointer fpgabase
*
********************************************/
void drvNSLS2_EM::mmap_fpga()
{

#ifdef SIMULATION_MODE
    fpgabase_ = (unsigned int *) calloc(256, sizeof(unsigned int));
#else    
    int fd;
    fd = open("/dev/mem",O_RDWR|O_SYNC);
    if (fd < 0) {
        printf("Can't open /dev/mem\n");
        exit(1);
    }

    fpgabase_ = (unsigned int *) mmap(0, 255, PROT_READ|PROT_WRITE, MAP_SHARED, fd, 0x43C00000);

    if (fpgabase_ == NULL) {
        printf("Can't map FPGA space\n");
        exit(1);
    }
#endif
}


#ifdef POLLING_MODE
static void pollerThreadC(void *pPvt)
{
    drvNSLS2_EM *pdrvNSLS2_EM = (drvNSLS2_EM*)pPvt;;
    pdrvNSLS2_EM->pollerThread();
}
#else
// C callback function called by Linux when an interrupt occurs.  
// It calls the callbackFunc in your C++ driver.
static void frame_done(int signum)
{
    pdrvNSLS2_EM->callbackFunc();
}
#endif


void drvNSLS2_EM::pollerThread()
{
    while(1) { /* Do forever */
        if (acquiring_) callbackFunc();
        epicsThreadSleep(POLL_TIME);
    }
}

// The constructor for your driver
drvNSLS2_EM::drvNSLS2_EM(const char *portName, int moduleID, int ringBufferSize) : drvQuadEM(portName, 0, ringBufferSize)
{
    int i;

// Set the global pointer
    pdrvNSLS2_EM = this;      
 
    //const char *functionName = "drvNSLS2_EM";

    // Current ranges in microamps
    ranges_[0]=1;
    ranges_[1]=10;
    ranges_[2]=100;
    ranges_[3]=1000;
    ranges_[4]=50000;
  
    // Initialize Linux driver, set callback function
#ifdef POLLING_MODE
    epicsThreadCreate("NSLS2_EMPoller",
                      epicsThreadPriorityMedium,
                      epicsThreadGetStackSize(epicsThreadStackMedium),
                      (EPICSTHREADFUNC)pollerThreadC,
                      this);
#else
    pl_open(&intfd_);
    signal(SIGIO, &frame_done);
    fcntl(intfd_, F_SETOWN, getpid());
    int oflags = fcntl(intfd_, F_GETFL);
    fcntl(intfd_, F_SETFL, oflags | FASYNC);
#endif   
    // set up register memory map
    mmap_fpga();
  
    // Create new parameter for DACs
    createParam(P_DACString, asynParamInt32, &P_DAC);
    fpgabase_[SA_RATE_DIV] = (int)(50e6/FREQ + 0.5); /* set for a FREQ interrupr rate */
    for (i=0;i<QE_MAX_INPUTS;i++){
        scaleFactor[i][0] = 1.0/((2^17)-1);
        scaleFactor[i][1] = 10.0/((2^17)-1);
        scaleFactor[i][2] = 100.0/((2^17)-1);
        scaleFactor[i][3] = 1000.0/((2^17)-1);
        scaleFactor[i][4] = 50000.0/((2^17)-1);
    }
 
    setStringParam(P_Firmware, "Version 1");
    setIntegerParam(P_Model, QE_ModelNSLS2_EM);
    callParamCallbacks();
}

drvNSLS2_EM::~drvNSLS2_EM()
{
    // This is your object's destructor
    // Free any resources that your object has allocated
}


/** Called when asyn clients call pasynInt32->write().
  * \param[in] pasynUser pasynUser structure that encodes the reason and address.
  * \param[in] value Value to write. */
asynStatus drvNSLS2_EM::writeInt32(asynUser *pasynUser, epicsInt32 value)
{
    int function = pasynUser->reason;
    asynStatus status = asynSuccess;
    int channel;
    const char *paramName;
    const char* functionName = "writeInt32";

    //  If this is a base class parameter then call the base class
    if (function < FIRST_NSLS2_COMMAND) {
        return drvQuadEM::writeInt32(pasynUser, value);
    }
    
    getAddress(pasynUser, &channel);
    
   /* Fetch the parameter string name for possible use in debugging */
    getParamName(function, &paramName);

    /* Set the parameter in the parameter library. */
    setIntegerParam(channel, function, value);
    
    if (function == P_DAC) {
        status = setDAC(channel, value);
    }
    
    /* Do callbacks so higher layers see any changes */
    callParamCallbacks();
    
    if (status) 
        epicsSnprintf(pasynUser->errorMessage, pasynUser->errorMessageSize, 
                  "%s:%s: status=%d, function=%d, name=%s, value=%d", 
                  driverName, functionName, status, function, paramName, value);
    else        
        asynPrint(pasynUser, ASYN_TRACEIO_DRIVER, 
              "%s:%s: function=%d, name=%s, value=%d\n", 
              driverName, functionName, function, paramName, value);
    return (asynStatus)status;
}


asynStatus drvNSLS2_EM::getBounds(asynUser *pasynUser, epicsInt32 *low, epicsInt32 *high)
{
    int function = pasynUser->reason;

    if (function == P_DAC) {
        *low = -32768;
        *high = 32767;
    }
    else {
        return drvQuadEM::getBounds(pasynUser, low, high);
    }
    return asynSuccess;
}

//  Callback function in driver
void drvNSLS2_EM::callbackFunc()
{
    int input[QE_MAX_INPUTS];
    int i, range, nvalues;
    //static const char *functionName="callbackFunc";

    lock();
    /* Read the new data as integers */
    readMeter(input);

    getIntegerParam(P_Range, &range);  
    getIntegerParam(P_ValuesPerRead, &nvalues);  
    /* Convert to double) */
    for (i=0; i<QE_MAX_INPUTS; i++) {
        rawData_[i] = input[i] * 16.0 / nvalues * scaleFactor[i][range];
    }
    
    computePositions(rawData_);
    unlock();
}


// Other functions

asynStatus drvNSLS2_EM::setAcquire(epicsInt32 value)
{
    // 1=start acquire, 0=stop.
    acquiring_ = value;
    fpgabase_[IRQ_ENABLE]=value;
    return asynSuccess;
}

asynStatus drvNSLS2_EM::setAcquireParams()
{
    int numAverage;
    int valuesPerRead;
    double sampleTime;
    double averagingTime;
    //static const char *functionName = "setAcquireParams";

    getIntegerParam(P_ValuesPerRead,    &valuesPerRead);
    getDoubleParam (P_AveragingTime,    &averagingTime);

    // Program valuesPerRead in the FPGA
    fpgabase_[SA_RATE] = valuesPerRead;

#ifdef POLLING_MODE
    sampleTime = POLL_TIME;
#else
    // Compute the sample time.  This is valuesPerRead / FREQ. 
    sampleTime = valuesPerRead / FREQ;
#endif
    setDoubleParam(P_SampleTime, sampleTime);

    numAverage = (int)((averagingTime / sampleTime) + 0.5);
    setIntegerParam(P_NumAverage, numAverage);
    if(numAverage > 1){ 
        readingsAveraged_=1;
    }
    else readingsAveraged_=0;
    printf("ReadingsAveraged = %i\n", readingsAveraged_);
    return asynSuccess;
}

asynStatus drvNSLS2_EM::setAveragingTime(epicsFloat64 value)
{
    return setAcquireParams();
}

/** Sets the values per read.
  * \param[in] value Values per read. Minimum depends on number of channels.
  */
asynStatus drvNSLS2_EM::setValuesPerRead(epicsInt32 value) 
{
    return setAcquireParams();
}



asynStatus drvNSLS2_EM::setBiasVoltage(epicsFloat64 value)
{
    fpgabase_[HV_BIAS] = (int) (value *65535.0/50.0);
    printf("Setting bias voltage\n");
    return asynSuccess ;
}

asynStatus drvNSLS2_EM::setRange(epicsInt32 value)
{
    printf("Gain: %i\n",value);
    fpgabase_[GAINREG] = value;
    return asynSuccess;
}


asynStatus drvNSLS2_EM::readStatus()
{
    return asynSuccess;
}

asynStatus drvNSLS2_EM::getFirmwareVersion()
{
 int fver;
 char tmpstr[32];
 
     fver = fpgabase_[FPGAVER];
     sprintf(tmpstr,"%i",fver);
     printf("FPGA version=%s\n",tmpstr);
     strncpy(firmwareVersion_,tmpstr, strlen(tmpstr));
     setStringParam(P_Firmware, firmwareVersion_);
    return asynSuccess;
}

asynStatus drvNSLS2_EM::reset()
{
    return asynSuccess;
}

void drvNSLS2_EM::report(FILE *fp, int details)
{
    // Print any information you want about your driver
    
    // Call the base class report method
    drvQuadEM::report(fp, details);
}

void drvNSLS2_EM::exitHandler()
{
    // Do anything that needs to be done when the EPICS is shutting down
}

/* That's all you need to send the data to the quadEM base class.  
You need to write a readMeter() function (or some other name) 
to actually read the ADC registers.  You also need to implement 
any of these quadEM base class functions that apply to your device: */

//    virtual asynStatus setAcquireMode(epicsInt32 value);
//    virtual asynStatus setAveragingTime(epicsFloat64 value);
//    virtual asynStatus setBiasState(epicsInt32 value);
//    virtual asynStatus setBiasVoltage(epicsFloat64 value);
//    virtual asynStatus setBiasInterlock(epicsInt32 value);
//    virtual asynStatus setPingPong(epicsInt32 value);
//    virtual asynStatus setIntegrationTime(epicsFloat64 value);
//    virtual asynStatus setNumChannels(epicsInt32 value);
//    virtual asynStatus setNumAcquire(epicsInt32 value);
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
  * \param[in] ringBufferSize The number of samples to hold in the input ring buffer.
  *            This should be large enough to hold all the samples between reads of the
  *            device, e.g. 1 ms SampleTime and 1 second read rate = 1000 samples.
  *            If 0 then default of 2048 is used.
  */
int drvNSLS2_EMConfigure(const char *portName, int moduleID,int ringBufferSize)
{
    new drvNSLS2_EM(portName, moduleID, ringBufferSize);
    return(asynSuccess);
}


/* EPICS iocsh shell commands */

static const iocshArg initArg0 = { "portName", iocshArgString};
static const iocshArg initArg1 = { "moduleID",iocshArgInt};
static const iocshArg initArg2 = { "ring buffer size",iocshArgInt};
static const iocshArg * const initArgs[] = {&initArg0, &initArg1,
                                            &initArg2
                                            };
static const iocshFuncDef initFuncDef = {"drvNSLS2_EMConfigure",3,initArgs};
static void initCallFunc(const iocshArgBuf *args)
{
    drvNSLS2_EMConfigure(args[0].sval, args[1].ival, args[2].ival);
}

void drvNSLS2_EMRegister(void)
{
    iocshRegister(&initFuncDef,initCallFunc);
}

epicsExportRegistrar(drvNSLS2_EMRegister);

}



