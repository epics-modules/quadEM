/*
 * testBusyAsyn.cpp
 * 
 * This is an asyn driver that is intended to test 2 problems with the devBusyAsyn device support.
 * Problem 1: 
 *  Busy record has a value of 1 autosave file.  
 *  It is not sufficient to have 1 in the .db file, because device support reads the value from the driver,
 *    and that will be 0.
 *  Driver does an initial callback with value 0 in constructor
 *  Driver gets sent the value 1, but the record comes up with the value 0.
 *  This happens because the callback with value 0 occurs after the driver has queued the request to send 1 but
 *  before the value is written to the driver.
 *  This occurs in devBusyAsyn.c commit e74be23b5b6cc47b480eb96cd40bdb748af42bf8 and earlier
 *
 * Problem 2:
 *  Driver is configured for asynchronous device support (ASYN_CANBLOCK=1)
 *  Busy record sends a value of 1 to driver.
 *  Driver immediately does a callback with a value of 0.
 *  Busy record is stuck in the 1 state, it ignores the callback value of 0 because PACT=1 when callback
 *  occurs.
 *  This occurs in devBusyAsyn.c commit a04e121da0acd71226fbe722fbf1b9551b21fe00.
 *
 * Author: Mark Rivers
 *
 * Created February 23, 2017
 */

#include <stdlib.h>

#include <iocsh.h>
#include <epicsThread.h>
#include <dbAccessDefs.h>

#include <asynPortDriver.h>

#include <epicsExport.h>

#define P_P1_String "P1"  /* asynInt32,    r/w */
#define P_P2_String "P2"  /* asynInt32,    r/w */
#define P_P3_String "P3"  /* asynInt32,    r/w */
#define P_P4_String "P4"  /* asynInt32,    r/w */

class testBusyAsyn : public asynPortDriver {
public:
    testBusyAsyn(const char *portName);
    virtual asynStatus writeInt32(asynUser *pasynUser, epicsInt32 value);

protected:
    int P_P1;
    int P_P2;
    int P_P3;
    int P_P4;
 
private:
    /* Our data */
};


static const char *driverName="testBusyAsyn";

testBusyAsyn::testBusyAsyn(const char *portName) 
   : asynPortDriver(portName, 
                    1, /* maxAddr */ 
                    0, /* Number of parameters, no longer needed with asyn R4-31 */
                    asynInt32Mask | asynDrvUserMask, /* Interface mask */
                    asynInt32Mask,                   /* Interrupt mask */
                    ASYN_CANBLOCK, /* asynFlags */
                    1, /* Autoconnect */
                    0, /* Default priority */
                    0) /* Default stack size*/    
{
    createParam(P_P1_String, asynParamInt32, &P_P1);
    createParam(P_P2_String, asynParamInt32, &P_P2);
    createParam(P_P3_String, asynParamInt32, &P_P3);
    createParam(P_P4_String, asynParamInt32, &P_P4);
    setIntegerParam(P_P1, 0);
    setIntegerParam(P_P2, 0);
    setIntegerParam(P_P3, 0);
    setIntegerParam(P_P4, 0);
}


asynStatus testBusyAsyn::writeInt32(asynUser *pasynUser, epicsInt32 value)
{
    int function = pasynUser->reason;
    const char* functionName = "writeInt32";

    /* Need to sleep some time here for the problem to show up.  This mimics a driver that takes some
     * time to communicate with the device. */

    epicsThreadSleep(1.);
    asynPrint(pasynUser, ASYN_TRACEIO_DRIVER, 
          "%s::%s function=%d, value=%d\n", 
          driverName, functionName, function, value);
    /* Set the parameter in the parameter library. */
    setIntegerParam(function, value);
    
    if (function == P_P3) {
        asynPrint(pasynUser, ASYN_TRACEIO_DRIVER, 
              "%s::%s function=%d, P3 input=%d, setting to 0\n", 
              driverName, functionName, function, value);
        setIntegerParam(P_P3, 0);
    } else if (function == P_P4) {
        asynPrint(pasynUser, ASYN_TRACEIO_DRIVER, 
              "%s::%s function=%d, P4 input=%d, setting to 0\n", 
              driverName, functionName, function, value);
        setIntegerParam(P_P4, 0);
    } 
    /* Do callbacks so higher layers see any changes */
    callParamCallbacks();
    
    return asynSuccess;
}

/* Configuration routine.  Called directly, or from the iocsh function below */

extern "C" {

int testBusyAsynConfigure(const char *portName)
{
    new testBusyAsyn(portName);
    return(asynSuccess);
}

static const iocshArg initArg0 = { "portName",iocshArgString};
static const iocshArg * const initArgs[] = {&initArg0};
static const iocshFuncDef initFuncDef = {"testBusyAsynConfigure",1,initArgs};
static void initCallFunc(const iocshArgBuf *args)
{
    testBusyAsynConfigure(args[0].sval);
}

void testBusyAsynRegister(void)
{
    iocshRegister(&initFuncDef,initCallFunc);
}

epicsExportRegistrar(testBusyAsynRegister);

}

