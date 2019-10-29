// Define SIMULATION_MODE to run on a system without the FPGA
#define SIMULATION_MODE 1

// Define POLLING_MODE to poll the ADCs rather than using interrupts
#define POLLING_MODE 1

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#ifndef _WIN32
  #include <unistd.h>
  #include <sys/mman.h>
#endif
#include <fcntl.h>
#include <signal.h>
#include <sys/stat.h>
#include <math.h>
#ifndef SIMULATION_MODE
  #include <linux/i2c-dev.h>
#endif

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
#include "drvNSLS2_IC.h"


#define LEDS 0
#define FPGAVER 3
#define SA_RATE 1
#define IRQ_ENABLE 7
#define RAW 12
#define RAW_A 12
#define RAW_B 13
#define FRAME_NO 2
#define SA_RATE_DIV 1
#define GAINREG 4
#define AVG 8
#define AVG_A 8 
#define AVG_B 9 
#define INTTIME 5

#define FREQ 1000000.0
#define MIN_INT_TIME 400
#define MAX_INT_TIME 1000000

#define DEVNAME "/dev/vipic"

#define POLL_TIME 0.0001 
#define NOISE 1000.

static int i2c_dev; 
static const char *driverName = "drvNSLS2_IC";

// Global variable containing pointer to your driver object
class drvNSLS2_IC *pdrvNSLS2_IC;


//*******************************************
//* Read ADC
//*
//*
//*******************************************

asynStatus drvNSLS2_IC::readMeter(int *adcbuf)
{

    int i, val;
    static const char *functionName = "readMeter";

    for (i=0;i<=1;i++) {
#ifdef SIMULATION_MODE
        getIntegerParam(i, P_DAC, &val);
        val =  val + NOISE * ((double)rand() / (double)(RAND_MAX) -  0.5);  
#else
        val = fpgabase_[AVG+i];  
#endif
        asynPrint(pasynUserSelf, ASYN_TRACEIO_DRIVER, 
            "%s::%s i=%d val=%d\n",
            driverName, functionName, i, val);
        *adcbuf++ = val;
    } 
        *adcbuf++ = 0;
        *adcbuf++ = 0; /* IC only has 2 channels */

    return(asynSuccess);
}

/* open i2c file for DACs */

asynStatus drvNSLS2_IC::OpenDacs(void){
//   int dev;
/*
    char filename[40], buf[10];
    int addr = 0b01001010;        // The I2C address of the DAC
    int bytesWritten;
 
    sprintf(filename,"/dev/i2c-0");
    if ((i2c_dev = open(filename,O_RDWR)) < 0) {
        printf("Failed to open the bus.");
        exit(1);
	}
    if (ioctl(i2c_dev,I2C_SLAVE,addr) < 0) {
        printf("Failed to acquire bus access and/or talk to slave.\n");
        exit(1);
    }
    buf[0] = 0x80;  //Command Access Byte
    buf[1] = 0x00;
    buf[2] = 0x10;
    if (bytesWritten=write(i2c_dev,buf,3) != 3){
       printf("Error Writing DAC to Set Int Ref...   Bytes Written: %d\n",bytesWritten);
       }
    else {
       printf("Internal Reference Enabled...\n");
       }
*/
    return(asynSuccess);
} 

asynStatus drvNSLS2_IC::setDAC(int channel, int value)
{
  /*
    static const char *functionName = "setDAC";
    char buf[3] = {0};
    int bytesWritten;
    short int dacWord; 

    dacWord = value;
    if (dacWord > 4095)  dacWord = 4095; 
    if (dacWord < 0)     dacWord = 0;
    printf("Set Voltage: %i \n",dacWord);
    //printf("DAC TF: %f\n",DACTF);
    printf("DAC Word: %d   (0x%x)\n",dacWord,dacWord);
  
    buf[0] = 0x30 + channel; //Command Access Byte
    buf[1] = (char)((dacWord & 0x0FF0) >> 4);
    buf[2] = (char)((dacWord & 0x000F) << 4);
    //printf("MSB: %x    LSB: %x\n",((dacBits & 0xFF00) >> 8),(dacBits & 0x00FF));
    printf("MSB: %x    LSB: %x\n",buf[1],buf[2]);
    bytesWritten = write(i2c_dev,buf,3);
    asynPrint(pasynUserSelf, ASYN_TRACEIO_DRIVER,"%s::%s channel=%d val=%d\n", driverName, functionName, channel, value);
*/    return(asynSuccess);
}


/*******************************************
* mmap fpga register space
* returns pointer fpgabase
*
********************************************/
void drvNSLS2_IC::mmap_fpga()
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
    printf("fpgabase = %x\n",fpgabase_);
#endif
}


bool drvNSLS2_IC::isAcquiring()
{
  return acquiring_;
}

#ifdef POLLING_MODE
static void pollerThread(void *pPvt)
{
    while(1) { /* Do forever */
        if (pdrvNSLS2_IC->isAcquiring()) pdrvNSLS2_IC->callbackFunc();
        epicsThreadSleep(POLL_TIME);
    }
}
#else
/*******************************************
* open interrupt driver 
*
********************************************/
asynStatus drvNSLS2_IC::pl_open(int *fd) {
    if ( (*fd = open(DEVNAME, O_RDWR)) <= 0 ) {
        perror(__func__);
        return(asynError);
    }

    return(asynSuccess);
}

// C callback function called by Linux when an interrupt occurs.  
// It calls the callbackFunc in your C++ driver.
static void frame_done(int signum)
{
    pdrvNSLS2_IC->callbackFunc();
}
#endif

asynStatus drvNSLS2_IC::computeScaleFactor()
{
    int range;
    int valuesPerRead;
    double integrationTime;
    static const char *functionName = "computeScaleFactor";

    getIntegerParam(P_ValuesPerRead,  &valuesPerRead);
    getIntegerParam(P_Range,          &range);
    getDoubleParam(P_IntegrationTime, &integrationTime);
    /* full charge in Coulombs / time in seconds */
    scaleFactor_[range] = ranges_[range]*1e-12 / (integrationTime / 1e6);
    asynPrint(pasynUserSelf, ASYN_TRACEIO_DRIVER,
        "%s::%s scaleFactor=%e\n", driverName, functionName, scaleFactor_[range]);
    return asynSuccess;
}

// The constructor for your driver
drvNSLS2_IC::drvNSLS2_IC(const char *portName, int ringBufferSize) : drvQuadEM(portName, ringBufferSize)
{
    int i;
    float fsd;

// Set the global pointer
    pdrvNSLS2_IC = this;      
 
    //const char *functionName = "drvNSLS2_EM";

    // Current ranges in picoCoulombs
    ranges_[0]=1000;
    ranges_[1]=50;
    ranges_[2]=100;
    ranges_[3]=150;
    ranges_[4]=200;
    ranges_[5]=250;
    ranges_[6]=300;
    ranges_[7]=350;
    
    calibrationMode_ = false;
    for (i=0; i<QE_MAX_INPUTS; i++) ADCOffset_[i] = 0;
  
    // Initialize Linux driver, set callback function
#ifdef POLLING_MODE
    epicsThreadCreate("NSLS2_ICPoller",
                      epicsThreadPriorityMedium,
                      epicsThreadGetStackSize(epicsThreadStackMedium),
                      (EPICSTHREADFUNC)pollerThread,
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
    createParam(P_DACString,             asynParamInt32, &P_DAC);
    createParam(P_DACDoubleString,     asynParamFloat64, &P_DACDouble);
    createParam(P_CalibrationModeString, asynParamInt32, &P_CalibrationMode);
    createParam(P_ADCOffsetString,       asynParamInt32, &P_ADCOffset);
    createParam(P_IntegrationTimeString, asynParamFloat64, &P_IntegrationTime);
    OpenDacs();
    epicsThreadSleep(0.001); 
 
    getFirmwareVersion();
    setIntegerParam(P_Model, QE_ModelNSLS2_IC);
    callParamCallbacks();
}

drvNSLS2_IC::~drvNSLS2_IC()
{
    // This is your object's destructor
    // Free any resources that your object has allocated
}


/** Called when asyn clients call pasynInt32->write().
  * \param[in] pasynUser pasynUser structure that encodes the reason and address.
  * \param[in] value Value to write. */
asynStatus drvNSLS2_IC::writeInt32(asynUser *pasynUser, epicsInt32 value)
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
        /* We need to set the equivalent double in the parameter library for fast feedback */
        status = setDoubleParam(channel, P_DACDouble, (double)value);
        status = setDAC(channel, value);
    }
    else if (function == P_CalibrationMode) {
        calibrationMode_ = (value != 0);
    }
    else if (function == P_ADCOffset) {
        ADCOffset_[channel] = value;
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

/* Ivan So: add writeFloat64 so we may set the DAC thru the asynPort with QE_DOUBLE_DATA and setDAC.*/
asynStatus drvNSLS2_IC::writeFloat64(asynUser *pasynUser, epicsFloat64 value)
{
    int function = pasynUser->reason;
    int status = asynSuccess;
    int channel;
    const char *paramName;
    const char* functionName = "writeFloat64";

    if (function != P_DoubleData) {
        //  If this is a base class parameter then call the base class
        if (function < FIRST_NSLS2_COMMAND) {
            return drvQuadEM::writeFloat64(pasynUser, value);
        }
    }

    getAddress(pasynUser, &channel);

   /* Fetch the parameter string name for possible use in debugging */
    getParamName(function, &paramName);

    /* Set the parameter in the parameter library. */
    status |= setDoubleParam(channel, function, value);

    if (function == P_DACDouble) {
      status |= setDAC(channel, int(value));
    }

    /* Do callbacks so higher layers see any changes */
    callParamCallbacks();

    if (status)
        epicsSnprintf(pasynUser->errorMessage, pasynUser->errorMessageSize,
                  "%s:%s: status=%d, function=%d, name=%s, value=%d",
                  driverName, functionName, status, function, paramName, int(value));
    else
        asynPrint(pasynUser, ASYN_TRACEIO_DRIVER,
              "%s:%s: function=%d, name=%s, value=%d\n",
              driverName, functionName, function, paramName, int(value));
    return (asynStatus)status;
}

asynStatus drvNSLS2_IC::getBounds(asynUser *pasynUser, epicsInt32 *low, epicsInt32 *high)
{
    int function = pasynUser->reason;

    if (function == P_DAC) {
        *low = 0;
        *high = 4095;
    }
    else {
        return drvQuadEM::getBounds(pasynUser, low, high);
    }
    return asynSuccess;
}

//  Callback function in driver
void drvNSLS2_IC::callbackFunc()
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
        rawData_[i] = (double)input[i] / (double)nvalues;
        if (!calibrationMode_) {
            rawData_[i] = (rawData_[i] - ADCOffset_[i]) * scaleFactor_[range];
// [i][range];;
        }
    }

    computePositions(rawData_);
    unlock();
}


// Other functions

/** Sets the integration time. 
  * \param[in] value The integration time in microseconds [400-1000000]
  */
asynStatus drvNSLS2_IC::setIntegrationTime(epicsFloat64 value) 
{
    asynStatus status;
    
    /* Make sure the integration time is valid. If not change it and put back in
 parameter library */
    if (value <= MIN_INT_TIME) {
        value = MIN_INT_TIME;
        setDoubleParam(P_IntegrationTime, value);
    }
    if (value >= MAX_INT_TIME) {
         value = MAX_INT_TIME;
         setDoubleParam(P_IntegrationTime, value);
     }
    /*epicsSnprintf(outString_, sizeof(outString_), "p %d", (int)(value * 1e6));*/
    fpgabase_[INTTIME] = value;    
    computeScaleFactor();
    return asynSuccess;
}

asynStatus drvNSLS2_IC::setAcquire(epicsInt32 value)
{
    //static const char *functionName = "setAcquire";

    // Return without doing anything if value=1 and already acquiring
    if ((value == 1) && (acquiring_)) return asynSuccess;
    
    if (value == 0) {
        // Setting this flag tells the read thread to stop
        acquiring_ = 0;
        // Stop acquisition
        fpgabase_[IRQ_ENABLE]=0;
        // Call the base class function in case anything needs to be done there.
        drvQuadEM::setAcquire(0);
    } else {
        // Call the base class function because it handles some common tasks.
        drvQuadEM::setAcquire(1);
        fpgabase_[IRQ_ENABLE]=1;
        acquiring_ = 1;
    }
    return asynSuccess;
}

asynStatus drvNSLS2_IC::setAcquireParams()
{
    int numAverage;
    int valuesPerRead;
    double sampleTime;
    double averagingTime;
    double IntegrationTime;

    //static const char *functionName = "setAcquireParams";

    getIntegerParam(P_ValuesPerRead,    &valuesPerRead);
    getDoubleParam (P_AveragingTime,    &averagingTime);
    getDoubleParam (P_IntegrationTime,    &IntegrationTime);
    // Program valuesPerRead in the FPGA
    fpgabase_[SA_RATE] = valuesPerRead;
    fpgabase_[INTTIME] = IntegrationTime;

#ifdef POLLING_MODE
    sampleTime = POLL_TIME;
#else
    // Compute the sample time.  This is valuesPerRead / FREQ. 
    sampleTime = valuesPerRead * IntegrationTime;
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

asynStatus drvNSLS2_IC::setAveragingTime(epicsFloat64 value)
{
    return setAcquireParams();
}

/** Sets the values per read.
  * \param[in] value Values per read. Minimum depends on number of channels.
  */
asynStatus drvNSLS2_IC::setValuesPerRead(epicsInt32 value) 
{
    return setAcquireParams();
}



asynStatus drvNSLS2_IC::setBiasVoltage(epicsFloat64 value)
{
int val, hv;
float max;
    hv=5;
    max=4095.0*2.048/5.0;
    val = (int) ((value)/500.0*max);
    if(val >=(int)max){
        val=(int)max;
        printf("Setting to max allowed HV, 500V\n");
        }
    printf("Setting bias voltage to %f\n",value);
    setDAC(val,hv);
    return asynSuccess ;
}

asynStatus drvNSLS2_IC::setRange(epicsInt32 value)
{
    printf("Gain: %i\n",value);
    fpgabase_[GAINREG] = value;
    return asynSuccess;
}


asynStatus drvNSLS2_IC::readStatus()
{
    return asynSuccess;
}

asynStatus drvNSLS2_IC::getFirmwareVersion()
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

asynStatus drvNSLS2_IC::reset()
{
    return asynSuccess;
}

void drvNSLS2_IC::report(FILE *fp, int details)
{
    // Print any information you want about your driver
    
    // Call the base class report method
    drvQuadEM::report(fp, details);
}

void drvNSLS2_IC::exitHandler()
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
int drvNSLS2_ICConfigure(const char *portName, int ringBufferSize)
{
    new drvNSLS2_IC(portName, ringBufferSize);
    return(asynSuccess);
}


/* EPICS iocsh shell commands */

static const iocshArg initArg0 = { "portName", iocshArgString};
//static const iocshArg initArg1 = { "moduleID",iocshArgInt};
static const iocshArg initArg1 = { "ring buffer size",iocshArgInt};
static const iocshArg * const initArgs[] = {&initArg0, &initArg1
                                            };
static const iocshFuncDef initFuncDef = {"drvNSLS2_ICConfigure",2,initArgs};
static void initCallFunc(const iocshArgBuf *args)
{
    drvNSLS2_ICConfigure(args[0].sval, args[1].ival);
}

void drvNSLS2_ICRegister(void)
{
    iocshRegister(&initFuncDef,initCallFunc);
}

epicsExportRegistrar(drvNSLS2_ICRegister);

}



