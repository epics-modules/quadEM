/*
 * drvT4U_EM.cpp
 * 
 * Asyn driver that inherits from the drvQuadEM class to control 
 * the Sydor T4U Electrometer
 *
 * Author: Iain Marcuson
 *
 * Created October 24, 2022
 */

#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <errno.h>
#include <math.h>
#include <cstdint>

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
#include "drvT4UDirect_EM.h"

#define BROADCAST_TIMEOUT 0.2
#define NSLS_EM_TIMEOUT   0.1

#define COMMAND_PORT    15001
#define DATA_PORT       15002
#define MIN_INTEGRATION_TIME 400e-6
#define MAX_INTEGRATION_TIME 1.0
#define FREQUENCY 1e6
// 2^20 is maximum counts for 20-bit ADC
#define MAX_COUNTS 1048576.0

#define REG_T4U_CTRL            0
#define BIAS_N_EN_MASK          (1<<9)
#define BIAS_P_EN_MASK          (1<<10)
#define PULSE_BIAS_EN_MASK      (1<<18)
#define PULSE_BIAS_OFF_REG      22
#define PULSE_BIAS_ON_REG       23
#define REG_T4U_FREQ            1

#define REG_T4U_RANGE           3
#define RANGE_SEL_MASK          0x3
#define RANGE_AUTO_MASK         (1<<7)

#define REG_PID_CTRL            19
#define PID_EN_MASK             0x4
#define PID_CUTOUT_EN_MASK      0x2
#define PID_HYST_REENABLE_MASK  0x1
#define PID_POS_TRACK_MASK      0x3
#define PID_POS_TRACK_SHIFT     3
#define PID_CTRL_POL_MASK       0x40
#define PID_EXT_CTRL_MASK       0x80
#define REG_OUTPUT_MODE         93
#define OUTPUT_MODE_MASK        0x7

#define TXC_CHA_CALIB_SLOPE     100
#define TXC_CHB_CALIB_SLOPE     101
#define TXC_CHC_CALIB_SLOPE     102
#define TXC_CHD_CALIB_SLOPE     103

#define TXC_CHA_CALIB_OFFSET    104
#define TXC_CHB_CALIB_OFFSET    105
#define TXC_CHC_CALIB_OFFSET    106
#define TXC_CHD_CALIB_OFFSET    107

typedef enum {
    kGET_CMD_NAME,
    kPARSE_NAME,
    kPARSE_TR_HDR,
    kGET_TR_PAYLOAD,
    kGOT_FULL_TR,
    kGET_CMD_LEN,
    kPARSE_ASC_CMD,
    kEXEC_ASC_CMD,
    kTR_ERROR,
    kFLUSH
} CmdParseState_t;

typedef enum {
  Phase0, 
  Phase1, 
  PhaseBoth
} PingPongValue_t;

static const char *driverName="drvT4UDirect_EM";
static void readThread(void *drvPvt);
static void dataReadThread(void *drvPvt);

static CmdParseState_t parseCmdName(char *cmdName);


/** Constructor for the drvT4UDirect_EM class.
  * Calls the constructor for the drvQuadEM base class.
  * \param[in] portName The name of the asyn port driver to be created.
  * \param[in] qtHostAddress The address of the Qt middle layer
  * \param[in] ringBufferSize The number of samples to hold in the input ring buffer.
  *            This should be large enough to hold all the samples between reads of the
  *            device, e.g. 1 ms SampleTime and 1 second read rate = 1000 samples.
  *            If 0 then default of 2048 is used.
  */
drvT4UDirect_EM::drvT4UDirect_EM(const char *portName, const char *qtHostAddress, int ringBufferSize, unsigned int base_port_num, const char *cfgFileName) 
   : drvQuadEM(portName, ringBufferSize)
  
{
    asynStatus status;
    const char *functionName = "drvT4U_EM";
    char tempString[256];
    T4U_Reg_T curr_reg;
    int32_t ret;
    
    ret = parseConfigFile(cfgFileName);
    if (ret < 0)
    {
	printf("Error in calibration file.  Aborting.\n");
	exit(1);
    }
    
    ipAddress_[0] = 0;
    firmwareVersion_[0] = 0;

    // Set a range index to a default
    currRange_ = 2;
    // Range scale factors
    ranges_[0]=5e6;
    ranges_[1]=14955.12;
    ranges_[2]=47.0;
    ranges_[3]=1.0;
    ranges_[4]=1.0;
    ranges_[5]=1.0;
    ranges_[6]=1.0;
    ranges_[7]=1.0;

    // Calibration default values
    calSlope_[0] = 1.0;
    calSlope_[1] = 1.0;
    calSlope_[2] = 1.0;
    calSlope_[3] = 1.0;
    calOffset_[0] = 0.0;
    calOffset_[1] = 0.0;
    calOffset_[2] = 0.0;
    calOffset_[3] = 0.0;
    
    acquireStartEvent_ = epicsEventCreate(epicsEventEmpty);

    // Create the parameters
    createParam(P_BiasN_En_String, asynParamInt32, &P_BiasN_En);
    createParam(P_BiasP_En_String, asynParamInt32, &P_BiasP_En);
    createParam(P_BiasN_Voltage_String, asynParamFloat64, &P_BiasN_Voltage);
    createParam(P_BiasP_Voltage_String, asynParamFloat64, &P_BiasP_Voltage);
    createParam(P_PulseBias_En_String, asynParamInt32, &P_PulseBias_En);
    createParam(P_PulseBias_OffCnt_String, asynParamInt32, &P_PulseBias_OffCnt);
    createParam(P_PulseBias_OnCnt_String, asynParamInt32, &P_PulseBias_OnCnt);
    createParam(P_SampleFreq_String, asynParamInt32, &P_SampleFreq);
    createParam(P_DACMode_String, asynParamInt32, &P_DACMode);
    createParam(P_PosTrackMode_String, asynParamInt32, &P_PosTrackMode);
    createParam(P_PIDEn_String, asynParamInt32, &P_PIDEn);
    createParam(P_Updater_String, asynParamInt32, &P_Update_Reg);
    createParam(P_PIDCuEn_String, asynParamInt32, &P_PIDCuEn);
    createParam(P_PIDHystEn_String, asynParamInt32, &P_PIDHystEn);
    createParam(P_PIDCtrlPol_String, asynParamInt32, &P_PIDCtrlPol);
    createParam(P_PIDCtrlEx_String, asynParamInt32, &P_PIDCtrlEx);
    
#include "gc_t4u_cpp_params.cpp"
    
    // Create the port names
    strcpy(tcpCommandPortName_, "TCP_Command_");
    strcat(tcpCommandPortName_, portName); // -=-= TODO Add length check?
    strcpy(tcpDataPortName_, "TCP_Data_");
    strcat(tcpDataPortName_, portName);
    strcpy(udpDataPortName_, "UDP_Data_");
    strcat(udpDataPortName_, portName);
    
    // Connect the ports

    // First the command port
    epicsSnprintf(tempString, sizeof(tempString), "%s:%d", qtHostAddress, T4U_CMD_PORT);
    status = (asynStatus)drvAsynIPPortConfigure(tcpCommandPortName_, tempString, 0, 0, 0);
    printf("Attempted command port: %s connection to: %s\nStatus: %d\n", tcpCommandPortName_, tempString, status);
    if (status) {
        asynPrint(pasynUserSelf, ASYN_TRACE_ERROR,
            "%s::%s error calling drvAsynIPPortConfigure for command port=%s, IP=%s, status=%d\n", 
            driverName, functionName, tcpCommandPortName_, tempString, status);
        return;
    }

    // Connect to the command port
    status = pasynOctetSyncIO->connect(tcpCommandPortName_, 0, &pasynUserTCPCommand_, NULL);
    if (status) {
        asynPrint(pasynUserSelf, ASYN_TRACE_ERROR,
            "%s::%s error connecting to TCP Command port, status=%d, error=%s\n", 
            driverName, functionName, status, pasynUserTCPCommand_->errorMessage);
        return;
    }

    // Now the data port
    /*
    epicsSnprintf(tempString, sizeof(tempString), "%s:%d", qtHostAddress, base_port_num+1);
    status = (asynStatus)drvAsynIPPortConfigure(tcpDataPortName_, tempString, 0, 0, 0);
    if (status) {
        asynPrint(pasynUserSelf, ASYN_TRACE_ERROR,
            "%s::%s error calling drvAsynIPPortConfigure for data port=%s, IP=%s, status=%d\n", 
            driverName, functionName, tcpDataPortName_, tempString, status);
        return;
    }
    */
    
    // Now the UDP data port
    epicsSnprintf(tempString, sizeof(tempString), "127.0.0.1:%u:%u UDP", T4U_DATA_PORT-1, T4U_DATA_PORT);
    status = (asynStatus)drvAsynIPPortConfigure(udpDataPortName_, tempString, 0, 0, 0);
    if (status) {
        asynPrint(pasynUserSelf, ASYN_TRACE_ERROR,
            "%s::%s error calling drvAsynIPPortConfigure for UDP data port=%s, IP=%s, status=%d\n", 
            driverName, functionName, udpDataPortName_, tempString, status);
        return;
    }
    
    // Connect to the command port
    status = pasynOctetSyncIO->connect(udpDataPortName_, 0, &pasynUserUDPData_, NULL);
    if (status) {
        asynPrint(pasynUserSelf, ASYN_TRACE_ERROR,
            "%s::%s error connecting to UDP Data port, status=%d, error=%s\n", 
            driverName, functionName, status, pasynUserUDPData_->errorMessage);
        return;
    }

    // Initialize the command queue
    cmd_queue = new epicsRingPointer<char>(T4U_CMD_QUEUE_LEN, false);
    if (cmd_queue == nullptr)
    {
	printf("Error initializing command queue.\n");
	return;
    }
    
    acquiring_ = 0;
    readingActive_ = 0;
    setIntegerParam(P_Range, 2);
    setIntegerParam(P_Model, QE_ModelSydor_EM);
    setIntegerParam(P_ValuesPerRead, 5);
    setStringParam(P_Firmware, "1.48");
    setDoubleParam(P_Temperature, 1234.5);
    setIntegerParam(P_Geometry, 1);
    //-=-= TODO FIXME Figure out how SampleTime works with averaging
    setDoubleParam(P_SampleTime, 0.00001);
    acquiring_ = 1;
    drvQuadEM::setAcquire(1);

    // Do everything that needs to be done when connecting to the meter initially.
    // Note that the meter could be offline when the IOC starts, so we put this in
    // the reset() function which can be done later when the meter is online.
//    lock();
//    drvQuadEM::reset();
//    unlock();

    /* Create the thread that reads commands from the meter */
    status = (asynStatus)(epicsThreadCreate("drvT4UDirect_EM_Cmd_Task",
                          epicsThreadPriorityMedium,
                          epicsThreadGetStackSize(epicsThreadStackMedium),
                          (EPICSTHREADFUNC)::readThread,
                          this) == NULL);
    if (status) {
        printf("%s:%s: epicsThreadCreate failure for Command, status=%d\n", driverName, functionName, status);
        return;
    }

    /* Create the thread that reads the meter */
    status = (asynStatus)(epicsThreadCreate("drvT4UDirect_EM_Data_Task",
                          epicsThreadPriorityMedium,
                          epicsThreadGetStackSize(epicsThreadStackMedium),
                          (EPICSTHREADFUNC)::dataReadThread,
                          this) == NULL);
    if (status) {
        printf("%s:%s: epicsThreadCreate Data failure, status=%d\n", driverName, functionName, status);
        return;
    }
    
    callParamCallbacks();
}

void drvT4UDirect_EM::report(FILE *fp, int details)
{
    return;
}

void drvT4UDirect_EM::exitHandler()
{
    return;
}

int32_t drvT4UDirect_EM::parseConfigFile(const char *cfgFileName)
{
    FILE *cfgFile;
    char *curr_line = nullptr;
    size_t line_len = 0;
    bool b_all_set;

    // Invalidate all existing calibration values
    for (uint32_t range_idx = 0; range_idx < NUM_RANGES; range_idx++)
    {
	for (uint32_t chan_idx = 0; chan_idx == 0; chan_idx++)
	{
	    fullSlope_[range_idx][chan_idx] = nanf("");
	    fullOffset_[range_idx][chan_idx] = nanf("");
	}
    }
    
    cfgFile = fopen(cfgFileName, "r");
    if (cfgFile == nullptr)
    {
	printf("Failed to open file: %s\n", cfgFileName);
	return -1;
    }

    while(!feof(cfgFile))
    {
	ssize_t read_len;
	int32_t curr_chan;
	int32_t curr_range;
	float slope;
	float offset;
	int parse_ret;

	read_len = getline(&curr_line, &line_len, cfgFile);
	if (read_len == -1)
	{
	    //-=-= FIXME TODO IM 20241122
	    // Handle this somehow
	    break;
	}

	if (curr_line[0]  == '#') // Comment line
	{
	    continue;		// Move on to the next line
	}

	parse_ret = sscanf(curr_line, " %i , %i : %f , %f ", &curr_range, &curr_chan, &slope, &offset);
	if (parse_ret != 4)	//  Not expected format
	{
	    printf("Invalid config line: %s\nIgnoring.\n", curr_line);
	    continue;
	}

        //-=-= FIXME TODO IM 20241122 Do bounds checking
	fullSlope_[curr_range][curr_chan] = slope;
	fullOffset_[curr_range][curr_chan] = offset;
	printf("Calibration Range %i Channel %i Slope %f Offset %f\n", curr_range, curr_chan, slope, offset);
    }

    fclose(cfgFile);
    free(curr_line); // Memory cleanup
    // Now check that all values have been configured
    b_all_set = true;		// Assume true and find a contradiction
    for (uint32_t range_idx = 0; range_idx < NUM_RANGES; range_idx++)
    {
	for (uint32_t chan_idx = 0; chan_idx < 4; chan_idx++)
	{
	    if (isnan(fullSlope_[range_idx][chan_idx]) || isnan(fullOffset_[range_idx][chan_idx]))
	    {
		b_all_set = false;
		printf("Error: Range %u, Channel %u not set.\n", range_idx, chan_idx);
	    }
	}
    }

    if (!b_all_set)
    {
	printf("Error: Some channel calibrations not set.\n");
	return -2;
    }

    return 0;
}

asynStatus drvT4UDirect_EM::writeInt32(asynUser *pasynUser, epicsInt32 value)
{
    int function = pasynUser->reason;
    int status = asynSuccess;
    int channel;
    int reg_lookup;
    const char *paramName;
    const char *functionName = "writeInt32";

    getAddress(pasynUser, &channel);

    // Debugging
    //printf("%s: function %i\n", functionName, function);
    //fflush(stdout);
    
    /* Set the parameter in the parameter library. */
    status |= setIntegerParam(channel, function, value);
    
    // Fetch the parameter string name
    getParamName(function, &paramName);

    // Debugging
    //printf("%s: function %i name %s\n", functionName, function, paramName);
    //fflush(stdout);
    
    if (function == P_BiasN_En)
    {
        if (value)              // Turn on
        {
            epicsSnprintf(outCmdString_, sizeof(outCmdString_), "bs 0 0x200\r\n");
        }
        else                    // Turn off
        {
            epicsSnprintf(outCmdString_, sizeof(outCmdString_), "bc 0 0x200\r\n");
        }
        writeReadMeter();
    }
    else if (function == P_BiasP_En)
    {
        if (value)              // Turn on
        {
            epicsSnprintf(outCmdString_, sizeof(outCmdString_), "bs 0 0x400\r\n");
        }
        else                    // Turn off
        {
            epicsSnprintf(outCmdString_, sizeof(outCmdString_), "bc 0 0x400\r\n");
        }
        writeReadMeter();
    }
    else if (function == P_PulseBias_En)
    {
        if (value)              // Turn on
        {
            epicsSnprintf(outCmdString_, sizeof(outCmdString_), "bs 0 %i\r\n", (int) PULSE_BIAS_EN_MASK);
        }
        else                    // Turn off
        {
            epicsSnprintf(outCmdString_, sizeof(outCmdString_), "bc 0 %i\r\n", (int) PULSE_BIAS_EN_MASK);
        }
        writeReadMeter();
    }
    else if (function == P_PulseBias_OffCnt)
    {
        epicsSnprintf(outCmdString_, sizeof(outCmdString_), "wr %i %i\r\n",
                      (int) PULSE_BIAS_OFF_REG, value);
        writeReadMeter();
    }
    else if (function == P_PulseBias_OnCnt)
    {
        epicsSnprintf(outCmdString_, sizeof(outCmdString_), "wr %i %i\r\n",
                      (int) PULSE_BIAS_ON_REG, value);
        writeReadMeter();
    }
    else if (function == P_SampleFreq)
    {
        epicsSnprintf(outCmdString_, sizeof(outCmdString_), "wr %i %i\r\n",
                      REG_T4U_FREQ, value);
        writeReadMeter();
    }
    else if (function == P_Range)
    {
	//printf("**************Setting Range\nValue %i\n",value);
        // Clip the range if needed to the limits
        if (value < 0)
        {
            value = 0;
        }
        else if (value > 2)
        {
            value = 2;
        }
        // Set in the parameter library again, since it may have been changed
        // above.
        status |= setIntegerParam(channel, function, value);
        epicsSnprintf(outCmdString_, sizeof(outCmdString_), "wr 3 %i\r\n", value);
        writeReadMeter();

	// Now send the calibration parameters
	for(uint32_t chan_idx = 0; chan_idx<4; chan_idx++)
	{
	    epicsSnprintf(outCmdString_, sizeof(outCmdString_), "wr %i %i\r\n", TXC_CHA_CALIB_SLOPE + chan_idx, *((int32_t *)(&fullSlope_[value][chan_idx]))); // Need to typepun the float value, then write to the channel for the slope
	    writeReadMeter();
	    epicsSnprintf(outCmdString_, sizeof(outCmdString_), "wr %i %i\r\n", TXC_CHA_CALIB_OFFSET + chan_idx, *((int32_t *)(&fullOffset_[value][chan_idx]))); // As above, but for offset
	    writeReadMeter();
	}
        
        currRange_ = value;
    }
    else if (function == P_DACMode)
    {
        int calc_reg = 93; // Base register
        // There are multiple functions, so clear out the DAC mode bits and then set them according to the mode
        epicsSnprintf(outCmdString_, sizeof(outCmdString_), "bc %i %i\r\n", calc_reg, OUTPUT_MODE_MASK);
        writeReadMeter();
        epicsSnprintf(outCmdString_, sizeof(outCmdString_), "bs %i %i\r\n", calc_reg, value & OUTPUT_MODE_MASK);
        writeReadMeter();
    }
    else if (function == P_PIDEn)
    {
        char *enable_cmd[2] = {"bc", "bs"}; // Off does bc; On does bs

        epicsSnprintf(outCmdString_, sizeof(outCmdString_), "%s %i %i\r\n",
                      enable_cmd[value], REG_PID_CTRL, PID_EN_MASK);
        writeReadMeter();
    }
    else if (function == P_Update_Reg)
    {
	// -=-= FIXME 20241118 IM Needs more work
	// -=-= XXX 20241009 IM Debug this
	//printf("Running updater.\n");
	epicsSnprintf(outCmdString_, sizeof(outCmdString_), "tr 0 49\r\n");
	writeReadMeter();
	epicsSnprintf(outCmdString_, sizeof(outCmdString_), "tr 50 99\r\n");
	writeReadMeter();
	epicsSnprintf(outCmdString_, sizeof(outCmdString_), "tr 100 107\r\n");
        writeReadMeter();
    }
    else if ((function == P_PIDCuEn) || (function == P_PIDHystEn)
             || (function == P_PIDCtrlPol) || (function == P_PIDCtrlEx))
    {
        char *enable_cmd[2] = {"bc", "bs"}; // Off does bc; on does bs.
        int reg = REG_PID_CTRL;
        int mask;

        if (function == P_PIDCuEn)
        {
            mask = PID_CUTOUT_EN_MASK;
        }
        else if (function == P_PIDHystEn)
        {
           mask = PID_HYST_REENABLE_MASK;
        }
        else if (function == P_PIDCtrlPol)
        {
            mask = PID_CTRL_POL_MASK;
        }
        else // (function == P_PIDCtrlEx)
        {
            mask = PID_EXT_CTRL_MASK;
        }
        

        epicsSnprintf(outCmdString_, sizeof(outCmdString_), "%s %i %i\r\n",
                      enable_cmd[value], reg, mask); // Write to X
        writeReadMeter();

    }
    else if (function == P_PosTrackMode)
    {
        //-=-= XXX The shift amount may 
        int reg = REG_PID_CTRL;
        int shift_mask = PID_POS_TRACK_MASK << PID_POS_TRACK_SHIFT;
        int shift_val = (value & PID_POS_TRACK_MASK) << PID_POS_TRACK_SHIFT;

        // First clear the exist position bits
        epicsSnprintf(outCmdString_, sizeof(outCmdString_), "bc %i %i\r\n",
                      reg, shift_mask);
        writeReadMeter();
        // Now set the bits to the selected value
        epicsSnprintf(outCmdString_, sizeof(outCmdString_), "bs %i %i\r\n",
                      reg, shift_val);
        writeReadMeter();
    }

    if (function < FIRST_T4U_COMMAND)
    {
        return (asynStatus)drvQuadEM::writeInt32(pasynUser, value);
    }
    else
    {
        return asynSuccess;
    }
    
    
    //printf("About to return from %s\n", functionName);
    //fflush(stdout);
    return (asynStatus)drvQuadEM::writeInt32(pasynUser, value);
}

asynStatus drvT4UDirect_EM::writeFloat64(asynUser *pasynUser, epicsFloat64 value)
{
    int function = pasynUser->reason;
    int status = asynSuccess;
    int channel;
    int reg_lookup;
    T4U_Reg_T *pid_reg;
    const char *paramName;
    const char *functionName = "writeFloat64";
    
    getAddress(pasynUser, &channel);
    
    /* Set the parameter in the parameter library. */
    status |= setDoubleParam(channel, function, value);
    
    // Fetch the parameter string name
    getParamName(function, &paramName);

    // Debugging
    //printf("%s: function %i name %s\n", functionName, function, paramName);
    //fflush(stdout);

    if ((pid_reg = findRegByAsyn(function)) != nullptr)
    {
        int out_val = scaleParamToReg(value, pid_reg);
        epicsSnprintf(outCmdString_, sizeof(outCmdString_), "wr %i %i\r\n",
                      pid_reg->reg_num, out_val);
        writeReadMeter();
    }
    else if (function == P_BiasN_Voltage)
    {
        epicsSnprintf(outCmdString_, sizeof(outCmdString_), "wr 5 %i\r\n", (int) value);
        writeReadMeter();
    }
    else if (function == P_BiasP_Voltage)
    {
        epicsSnprintf(outCmdString_, sizeof(outCmdString_), "wr 4 %i\r\n", (int) value);
        writeReadMeter();
    }
    else if (function == P_AveragingTime)
    {
	double sample_time;
	getDoubleParam(P_SampleTime, &sample_time);
	
	setIntegerParam(P_NumAverage, int(value/sample_time));
    }

    //printf("About to exit from %s", functionName);
    //fflush(stdout);
    if (function < FIRST_T4U_COMMAND)
    {
        return (asynStatus)drvQuadEM::writeFloat64(pasynUser, value);
    }
    else
    {
        return asynSuccess;
    }
            
}

asynStatus drvT4UDirect_EM::setAcquire(epicsInt32 value)
{
    return asynSuccess;
}

asynStatus drvT4UDirect_EM::setPingPong(epicsInt32 value)
{
    return asynSuccess;
}

asynStatus drvT4UDirect_EM::setIntegrationTime(epicsFloat64 value)
{
    return asynSuccess;
}

asynStatus drvT4UDirect_EM::setRange(epicsInt32 value)
{
    return asynSuccess;
}

asynStatus drvT4UDirect_EM::setValuesPerRead(epicsInt32 value)
{
    return asynSuccess;
}

asynStatus drvT4UDirect_EM::readStatus()
{
    return asynSuccess;
}

asynStatus drvT4UDirect_EM::reset()
{
    return asynSuccess;
}

// Just does a write; reading is handling in its own thread
asynStatus drvT4UDirect_EM::writeReadMeter()
{
    size_t nwrite;
    int eomReason;
    asynStatus status=asynSuccess;
    asynOctet *pasynOctet;
    asynInterface *pasynInterface;
    void *octetPvt;
    char *new_cmd;

    // Debugging
    //printf("Entered writeReadMeter()\n");
    //fflush(stdout);

    if (strlen(outCmdString_) != 0) // Actual command
    {
	new_cmd = new char[strlen(outCmdString_)+1];
	if (new_cmd == nullptr)
	{
	    return asynError;
	}
	strcpy(new_cmd, outCmdString_);
	if (cmd_queue->push(new_cmd))
	{
	    return asynSuccess;
	}
	else
	{
	    return asynOverflow;
	}
        
        //printf("Write status %i\n", (int) status);
        //fflush(stdout);
    }
        
    return status;
}

asynStatus drvT4UDirect_EM::getFirmwareVersion()
{
    return asynSuccess;
}

void drvT4UDirect_EM::process_reg(const T4U_Reg_T *reg_lookup, double value)
{
    return;
}

void drvT4UDirect_EM::cmdReadThread(void)
{
    asynStatus status;
    size_t nRead;
    int eomReason;
    int i;
    static char InData[MAX_COMMAND_LEN]; // Hold either an ASCII command or the header of a new command
    char *inTr;                   // Hold the payload of a tr command
    char currChar;
    size_t nRequest = 1;
    int processRet;
    static const char *functionName = "cmdReadThread";
    CmdParseState_t parseState = kGET_CMD_NAME;
    int32_t cmd_tick_count;
    

    status = asynSuccess;       // -=-= FIXME Used for a different call

    // Loop forever
    lock();
    while(1)                    // The main loop of receving commands
    {
        int totalBytesRead;
        int headerBytes;
        bool commandReceived;
	bool b_outstanding_cmd;
	bool b_got_prev_char;
	size_t nwrite;
        unlock();
        epicsThreadSleep(0.001);
        totalBytesRead = 0;
        memset(InData, '\0', MAX_COMMAND_LEN);
        commandReceived = false; // No proper command recieved yet
        uint16_t tr_len;
        uint16_t reg_num;
        uint32_t reg_val;
        bool nonWhite = false;
	cmd_tick_count = 0;    // Start at no ticks for timeout
	b_outstanding_cmd = false;
	b_got_prev_char = false;
	lock();
    process_tick:
	unlock();
	while (1)
	{
	    nRequest = 1;
	    cmd_tick_count++;

	    status = pasynOctetSyncIO->read(pasynUserTCPCommand_, &currChar, nRequest, 0.1, &nRead, &eomReason);
	    memset(InData, '\0', sizeof(InData)); // Initialize the buffer

	    // See if there are any outstanding commands
	    if (nRead == 0)
	    {
		cmd_tick_count++;
		if (cmd_tick_count >= 12) // 1 second timeout time reached
		{
		    // Process message queue
		    char *curr_msg;
		    curr_msg = cmd_queue->pop();
		    
		    if (curr_msg)
		    { 
			status = pasynOctetSyncIO->write(pasynUserTCPCommand_, curr_msg, strlen(curr_msg), T4U_EM_TIMEOUT, &nwrite);
			b_outstanding_cmd = true; // If we sent a message, track it
			cmd_tick_count = 0;
			//printf("Wrote %lu sending command: %s", (long unsigned) nwrite, curr_msg);
			if (strstr(curr_msg, "wr 1 "))
			{
			    cmd_tick_count = -40;
			}
			delete[] curr_msg;
		    }
		}
	    }
	    else		// Something in the command
	    {
		//printf("Got something.\n");
		//fflush(stdout);
		b_got_prev_char = true;
		parseState = kGET_CMD_NAME; // Note we are getting a command
		break;
	    }
	}
		
        //printf("First character: %c\n", currChar);
        while (1)
        {
	    nRequest = 1;           // Always request one byte at the start
	    if (parseState == kGET_CMD_NAME)
            {
                int charRead = 0;
                nonWhite = false; // No non-white at start
                while(1)
                {
		    if (!b_got_prev_char) // We have come from elsewhere with a character
			// Skip the read the first time
		    {
			status = pasynOctetSyncIO->read(pasynUserTCPCommand_, &currChar, nRequest, T4U_EM_TIMEOUT, &nRead, &eomReason); // Start by reading in a byte
			if (nRead == 0)     // No bytes available
			{
			    continue;
			}
		    }
		    else
		    {
			charRead = 0; // Reset the counter for the array below
		    }
		    
		    b_got_prev_char = false;
		    
                    if ((nonWhite == false) && isspace(currChar)) // Skip whitespace if we haven't already read a non-space character
                    {
                        continue;
                    }

                    nonWhite = true; // Flag that received a no
                    InData[charRead] = currChar;
                    charRead++;

                    if (charRead == 2) // Read a full command name
                    {
                        parseState = kPARSE_NAME; // Move to parsing name
                        totalBytesRead = 2;       // Two data bytes in InData
                        break;
                    }
                } // while(1) Socket reading loop
            } // if parseState = kGET_CMD_NAME

            if (parseState == kPARSE_NAME)
            {
                parseState = parseCmdName(InData);
            }

            if (parseState == kPARSE_ASC_CMD)
            {
                while (1)       // Loop until command or error
                {
                    status = pasynOctetSyncIO->read(pasynUserTCPCommand_, InData+totalBytesRead, nRequest, T4U_EM_TIMEOUT, &nRead, &eomReason); // Append a byte to the command name we already have

                    if (nRead == 0) // Didn't read a byte
                    {
                        continue; // Try again
                    }
                
                    totalBytesRead++;
                    if (*(InData+totalBytesRead-1) == '\n') // End of a command
                    {
                        parseState = kEXEC_ASC_CMD; // Not a full command received
                        break;
                    }
  
                    if (totalBytesRead >= (MAX_COMMAND_LEN -1)) // Too long
                    {
                        parseState =  kFLUSH;
                        break;
                    }
                } // Loop reading command bytes
            } // if parsing command

            if (parseState == kEXEC_ASC_CMD) // Executing an ASCII command
            {
                break;          //-=-= The actual execution would follow the break outside of the loop when we can lock.
            }

            if (parseState == kPARSE_TR_HDR)
            {
                // First we have to read the length
                nRequest = 2;   // Two byte length
                status = pasynOctetSyncIO->read(pasynUserTCPCommand_, (char *) &tr_len, nRequest, T4U_EM_TIMEOUT, &nRead, &eomReason); // Read the header length
                if (nRead != 2) // Didn't read whole length
                {
		    //printf("TR Header read %lu\n", nRead);
                    parseState = kFLUSH;
                }
                else            // Read whole length
                {
                    tr_len = ntohs(tr_len); // Sent big-endian
                    parseState = kGET_TR_PAYLOAD;
                }
            } // if parse TR_HDR

            if (parseState == kGET_TR_PAYLOAD)
            {
                inTr = new char[tr_len];
                if (inTr == nullptr)
                {
                    printf("Failed to allocate memory for TR payload.\n");
                    fflush(stdout);
                    parseState = kTR_ERROR; // Flag there was an error here
                }
                else            // Allocated memory
                {
                    nRequest = tr_len;   // Two byte length
                    status = pasynOctetSyncIO->read(pasynUserTCPCommand_, inTr, nRequest, T4U_EM_TIMEOUT, &nRead, &eomReason); // Append a byte to the command name we alreay have
                    if (nRead != nRequest) // Didn't read all bytes
                    {
                        parseState = kFLUSH;
                    }
                    else
                    {
                        parseState = kGOT_FULL_TR; // Flag so that we can parse the results
                    }
                } // memory alocated
                break;
            } // if kGET_TR_PAYLOAD

            if ((parseState == kFLUSH) || (parseState == kTR_ERROR)) // An error condition
            {
                break;
            }
        } // while parsing a message on the command socket
	
	//printf("Cmd bytes read: %u \n", totalBytesRead);
	//if (InData[0] != 't')
	//{
	//    printf("InData: %s", InData);
	//}

	//printf("Cmd thread about to lock.\n");
        lock();

        if (parseState == kEXEC_ASC_CMD) // We received an ASCII command to parse
        {
            //-=-= FIXME TODO Decide if we want to support this
        }
        else if (parseState == kFLUSH) // We had an error somewhere
        {
            unlock();
	    printf("InData: %s", InData);
	    printf("Cmd thread about to flush.\n");
            pasynOctetSyncIO->flush(pasynUserTCPCommand_); // Flush the socket
            lock();
        }
        else if (parseState == kTR_ERROR) // An error in TR handling
        {
            // Don't do anything, since there was no problem with the command
        }
        else if (parseState == kGOT_FULL_TR)
        {
            // Now we will have to iterate over each register in the tr payload
            int tr_reg_count = tr_len / 6;
            int tr_idx;
            int process_ret;
            for (tr_idx = 0 ; tr_idx < tr_reg_count; tr_idx++)
            {
                reg_num = *((uint16_t *)&inTr[tr_idx*6]);
                reg_val = *((uint32_t *)&inTr[tr_idx*6+2]);
                //-=-= XXX Sent little-endian
                //reg_num = ntohs(reg_num); // Sent big-endian
                //reg_val = ntohl(reg_val);

                // Now that we have the register number and value, we need to update the PVs
                //-=-= DEBUGGING
                //printf("Processing reg %3i\n", (int) reg_num);
                //fflush(stdout);
                //-=-= TODO XXX We need to be able to send int, but sometimes we want to treat it as unsigned
                process_ret = processRegVal(reg_num, reg_val);
                if (process_ret == 0)
                {
                    //printf("Processed reg %3i Value: %10i 0x%08x\n", (int) reg_num, (int) reg_val, (int) reg_val);
                    //fflush(stdout);
                }
            } // for tr_idx over all returned tr values
            delete inTr;
        } // if got_full_tr

        // Set the state for looking for a header again
        parseState = kGET_CMD_NAME;
	b_outstanding_cmd = false; // Clear the outstanding flag one way or another
	cmd_tick_count = 10;	   // Set to force a check of queued outgoing commands
	goto process_tick;
    } // while main receiving loop
    return;
}

// This one is for UDP
void drvT4UDirect_EM::dataReadThread(void)
{
    asynStatus status;
    size_t nRead;
    int eomReason;
    int i;
    char InData[MAX_COMMAND_LEN];
    size_t nRequest = 1;        // Read only one byte here to pass to the parser
    int32_t data_read;          // How many full readings we did
    const int32_t kREAD_TEXT = 0;
    const int32_t kREAD_BINARY = 1;
    int32_t read_path;          // Whether we read via text or via binary
    T4UFrame *payload;
    const int32_t MAX_PACKET_SIZE = 65535;
    static const char *functionName = "dataReadThread";
    DataBuffer_T udp_buffer;
    uint32_t ret;
    char udp_header[2];
    uint16_t packet_len;
    FILE *incoming_log;
    epicsTimeStamp recv_time;
    char *data_sink;

    //incoming_log = fopen("packet_log.txt", "w");
    status = asynSuccess;       // -=-= FIXME Used for a different call

    data_sink = new char[65535];
    payload = new T4UFrame;	// Maximum size
    // Loop forever
    lock();
    //-=-= FIXME IM 20241119 Remove the below line after restoring the locks
    //unlock();
    while(1)
    {
	char c_data;		// Char data
	uint16_t  s_data;	// Short data
	uint32_t u_data;	// UINT Data
	bool b_ok;		// Persistent error variable
	uint16_t payload_len;	// Payload length
	
        unlock();		
        //epicsThreadSleep(0.001);
	//for (uint32_t loop_idx = 0; loop_idx < 100000; loop_idx++)
	//{
        memset(payload, '\0', sizeof(T4UFrame));
	memset(udp_header, '\0', 2);
	nRead = 0;
	while(nRead != 2)
	{
	    nRequest = 2;
	    status = pasynOctetSyncIO->read(pasynUserUDPData_, udp_header, nRequest, 0.01, &nRead, &eomReason);
	}
	
	if ((udp_header[0] != 'B') || (udp_header[1] != 1))
	{
	    //-=-= TODO IM Handle B3 packets 20241118

	    if ((udp_header[0] == 'B') && (udp_header[1] == 3))
	    {
		uint32_t reg_cnt;
		nRequest = 2;
		pasynOctetSyncIO->read(pasynUserUDPData_, (char *)(&packet_len), nRequest, 0.1, &nRead, &eomReason);
		nRequest = packet_len;
		pasynOctetSyncIO->read(pasynUserUDPData_, data_sink, nRequest, 0.1, &nRead, &eomReason);
		reg_cnt = packet_len/6;
		for (uint32_t reg_idx = 0; reg_idx < reg_cnt; reg_idx++)
		{
		    uint32_t reg_num;
		    uint32_t reg_val;

		    reg_num = *((uint16_t *)&data_sink[reg_idx*6]);
		    reg_val = *((uint32_t *)&data_sink[reg_idx*6+2]);
		    processRegVal(reg_num, reg_val);
		    //printf("Process reg %u.\n", reg_num);
		}
		
		lock();
		continue;
	    }
	    else
	    {
		goto bad_type;
	    }
	    
	}

	nRequest = 2;
	pasynOctetSyncIO->read(pasynUserUDPData_, (char *)(&packet_len), nRequest, 0.1, &nRead, &eomReason);
	
	nRequest = sizeof(T4UMetadata);
        status = pasynOctetSyncIO->read(pasynUserUDPData_, (char *)(&payload->metadata), nRequest, 0.1, &nRead, &eomReason);

	/* -=-= FIXME IM 20241118
	if (status != asynSuccess)
	{
	    lock();
	    callParamCallbacks();
	    continue;		// Try again next time
	}
	*/

	//printf("Received UDP header of length %lu of expected %lu\n", (unsigned long) nRead, (unsigned long) sizeof(T4UMetadata));
	//payload->metadata.numberOfReads = ntohl(payload->metadata.numberOfReads);
	//printf("Nreads %u\n", payload->metadata.numberOfReads);

	nRequest = sizeof(T4UErrors);
        status = pasynOctetSyncIO->read(pasynUserUDPData_, (char *)(&payload->errors), nRequest, 0.1, &nRead, &eomReason);

	if (payload->metadata.numberOfReads > kT4U_MAX_DATA_SIZE)
	{
	    payload->metadata.numberOfReads = kT4U_MAX_DATA_SIZE;
	}
	nRequest = payload->metadata.numberOfReads*4*4;
	status = pasynOctetSyncIO->read(pasynUserUDPData_, (char *)(&payload->image), nRequest, 0.1, &nRead, &eomReason);

	nRequest = 1;	
	status = pasynOctetSyncIO->read(pasynUserUDPData_, (char *)(&payload->checksum), nRequest, 0.1, &nRead, &eomReason);
	
	if (payload->checksum == '*')
	{
	    b_ok = true;
	}
	else
	{

	    //printf("Failed checksum\n");
	bad_type:
	    pasynOctetSyncIO->flush(pasynUserUDPData_);
	    lock();
	    callParamCallbacks();
	    continue;
	}

	/*
	for (uint32_t lock_idx = 0; lock_idx < 50; lock_idx++)
	{
	    lock();
	    unlock();
	}
	*/
	
	
/* -=-= FIXME
	for (uint32_t byte_idx = 0; byte_idx < 20; byte_idx++)
	{
	    printf("%03u ", payload[byte_idx]);
	}
	printf("\n");
*/
	
	//fflush(stdout);
	
	// -=-= DEBUGGING
	//printf("Data Read: %c\n", InData[0]);
	//fflush(stdout);
	// Having received read data, read type and pass to handler

	bc_hdr_.units = payload->metadata.units;

	//-=-= FIXME DEBUGGING IM 20241118
	//printf("Expecting %u reads.\n", payload_len = payload->metadata.numberOfReads);

	payload_len = payload->metadata.numberOfReads;

	bc_hdr_.num_reads = payload_len;
	bc_data_payload_ = (char *)payload->image;

	//epicsTimeGetCurrent(&recv_time);
	//fprintf(incoming_log, "%u,%u,", recv_time.secPastEpoch, recv_time.nsec);
	//fprintf(incoming_log, "%u,%u\n", recv_time.secPastEpoch, recv_time.nsec);
	
	lock();

	if (b_ok)
	{
	    // Much data massaging to do
	    // For now, just send dummy values and clear the buffer
	    int32_t num_reads = bc_hdr_.num_reads;
	    uint32_t *curr_raw = (uint32_t *)bc_data_payload_;
	    double read_vals[4];
	    read_vals[1] = 100;
	    read_vals[2] = 100;
	    read_vals[3] = 100;
	    read_vals[0] = 100;
	    
	    for (int32_t read_idx = 0; (read_idx < num_reads) && (read_idx < kT4U_MAX_DATA_SIZE); read_idx++)
	    {
		if (bc_hdr_.units) // Reading current
		{
		    read_vals[0] = (double) (*((float *) &curr_raw[0]));
		    read_vals[1] = (double) (*((float *) &curr_raw[1]));
		    read_vals[2] = (double) (*((float *) &curr_raw[2]));
		    read_vals[3] = (double) (*((float *) &curr_raw[3]));
		}
		else // Reading raw values
		{
		    read_vals[0] = (rawToCurrent(curr_raw[0])-calOffset_[0]) / calSlope_[0];
		    read_vals[1] = (rawToCurrent(curr_raw[1])-calOffset_[1]) / calSlope_[1];
		    read_vals[2] = (rawToCurrent(curr_raw[2])-calOffset_[2]) / calSlope_[2];
		    read_vals[3] = (rawToCurrent(curr_raw[3])-calOffset_[3]) / calSlope_[3];
		}
		curr_raw += 4;
                
		computePositions(read_vals);
	    }
	}
         
        callParamCallbacks();

	//epicsTimeGetCurrent(&recv_time);
	//fprintf(incoming_log, "%u,%u\n", recv_time.secPastEpoch, recv_time.nsec);

//        fflush(stdout);
    }
    return;
    
}


int32_t drvT4UDirect_EM::processReceivedCommand(char *cmdString)
{
    int32_t reg_num, reg_val;
    int args_parsed;

    // We only accept rr commands.
    args_parsed = sscanf(cmdString, " rr %i %i\n", &reg_num, &reg_val);

    if (args_parsed != 2)       // Not a command we accept, or invalid number of arguments
    {
        return 1;              // We don't handle the command, but that is possibly okay, since it could be e.g. a valid "bs" command
    }
    return processRegVal(reg_num, reg_val);
}

int drvT4UDirect_EM::processRegVal(int reg_num, uint32_t reg_val)
{
    T4U_Reg_T *pid_reg;
    
    pid_reg = findRegByNum(reg_num); // See if this is a PID register that needs scaling

    if (pid_reg)                // It is a PID register
    {
        double raw_percent;
        double final_val;
        raw_percent = (reg_val - pid_reg->reg_min)/(pid_reg->reg_max - pid_reg->reg_min);

        final_val = raw_percent*(pid_reg->pv_max - pid_reg->pv_min) + pid_reg->pv_min;

        setDoubleParam(pid_reg->asyn_num, final_val);
    }
    else                        // Not a PID register
    {
        if (reg_num == REG_T4U_CTRL)
        {
            if (reg_val & BIAS_N_EN_MASK)
            {
                setIntegerParam(P_BiasN_En, 1);
            }
            else
            {
                setIntegerParam(P_BiasN_En, 0);
            }

            if (reg_val & BIAS_P_EN_MASK)
            {
                setIntegerParam(P_BiasP_En, 1);
            }
            else
            {
                setIntegerParam(P_BiasP_En, 0);
            }
            
            if (reg_val & PULSE_BIAS_EN_MASK)
            {
                setIntegerParam(P_PulseBias_En, 1);
            }
            else
            {
                setIntegerParam(P_PulseBias_En, 0);
            }
        }
        else if (reg_num == REG_T4U_FREQ)
        {
            double sample_time;
	    double averaging_time;
            setIntegerParam(P_SampleFreq, reg_val);
            sample_time = 1.0/reg_val;
            setDoubleParam(P_SampleTime, sample_time);
	    getDoubleParam(P_AveragingTime, &averaging_time);
	    setIntegerParam(P_NumAverage, int(averaging_time/sample_time));
	    //printf("Number to average: %i\n", int(averaging_time/sample_time));
        }
        else if (reg_num == REG_T4U_RANGE)
        {
            //-=-= TODO We may incorporate autorange later
            int range_val = reg_val & RANGE_SEL_MASK;
            setIntegerParam(P_Range, range_val);
            currRange_ = range_val;
        }
        else if (reg_num == PULSE_BIAS_OFF_REG)
        {
            setIntegerParam(P_PulseBias_OffCnt, reg_val);
        }
        else if (reg_num == PULSE_BIAS_ON_REG)
        {
            setIntegerParam(P_PulseBias_OnCnt, reg_val);
        }
        else if (reg_num == REG_OUTPUT_MODE)
        {
            //-=-= TODO PID Inhibit, External Trigger Enable, and Calc Mode
            // to be added later
            int dac_mode = reg_val & OUTPUT_MODE_MASK;
            setIntegerParam(P_DACMode, dac_mode);
        }
        else if (reg_num == REG_PID_CTRL)
        {
            uint32_t calc_val;
            
            calc_val = (reg_val & PID_CUTOUT_EN_MASK) ? 1 : 0;
            setIntegerParam(P_PIDCuEn, calc_val);

            calc_val = (reg_val & PID_HYST_REENABLE_MASK) ? 1 : 0;
            setIntegerParam(P_PIDHystEn, calc_val);

            calc_val = (reg_val & PID_EN_MASK) ? 1 : 0;
            setIntegerParam(P_PIDEn, calc_val);

            calc_val = (reg_val & PID_CTRL_POL_MASK) ? 1 : 0;
            setIntegerParam(P_PIDCtrlPol, calc_val);

            calc_val = (reg_val & PID_EXT_CTRL_MASK) ? 1 : 0;
            setIntegerParam(P_PIDCtrlEx, calc_val);

            calc_val = (reg_val >> PID_POS_TRACK_SHIFT) & PID_POS_TRACK_MASK;
            setIntegerParam(P_PosTrackMode, calc_val);

            
        }
        else if ((reg_num >= TXC_CHA_CALIB_SLOPE) && (reg_num <= TXC_CHD_CALIB_SLOPE))
        {
            double slope_val;

            slope_val = (double) (*((float *) &reg_val));
	    //printf("Calculated slope %i is %f\n", reg_num-TXC_CHA_CALIB_SLOPE, slope_val);
            calSlope_[reg_num - TXC_CHA_CALIB_SLOPE] = slope_val;
        }
        else if ((reg_num >= TXC_CHA_CALIB_OFFSET) && (reg_num <= TXC_CHD_CALIB_OFFSET))
        {
            double offset_val;

            offset_val = (double) (*((float *) &reg_val));
	    //printf("Calculated offset %i is %f\n", reg_num-TXC_CHA_CALIB_OFFSET, offset_val);
            calOffset_[reg_num - TXC_CHA_CALIB_OFFSET] = offset_val;
        }
        else                    // An unhandled command
        {
            return -1;          // Flag an error
        }
            
    }
    
    return 0;                   // Return a success
}

asynStatus drvT4UDirect_EM::readResponse()
{
    return asynSuccess;
}

int32_t drvT4UDirect_EM::readDataBuf(DataBuffer_T *buf, char *dest, uint32_t size)
{
    if (buf->pos > (buf->len - size))
    {
	return -1;
    }

    if (size == 1)
    {
	*dest = buf->buffer[buf->pos];
	buf->pos++;
    }
    else if (size == 2)
    {
	*(reinterpret_cast<uint16_t *>(dest)) = *reinterpret_cast<uint16_t *>(&buf->buffer[buf->pos]);
	buf->pos += 2;
    }
    else if (size == 4)
    {
	*(reinterpret_cast<uint32_t *>(dest)) = *reinterpret_cast<uint32_t *>(&buf->buffer[buf->pos]);
	buf->pos += 4;
    }
    else
    {
	return -1;
    }

    return 0;
}
	    

asynStatus drvT4UDirect_EM::readDataParam(size_t nRequest, char *dest, size_t *nRead)
{
    asynStatus status;
    char c_data;
    uint16_t s_data;
    uint32_t u_data;            // Holds the data being read
    double READ_TIMEOUT = 0.01; // The timeout to read the data -- they should all be here
    int eomReason;

    if (nRequest == 1)          // Reading one byte
    {
        status = pasynOctetSyncIO->read(pasynUserTCPData_, &c_data, nRequest, 0.01, nRead, &eomReason);
        if (status)             // Error reading
        {
            return status;      // Abort
        }
        *dest = c_data;         // Copy the value
        return asynSuccess;
    }
    
    else if (nRequest == 2)     // Reading a network short
    {
        status = pasynOctetSyncIO->read(pasynUserTCPData_, (char *) &s_data, nRequest, 0.01, nRead, &eomReason);
        if (status)             // Error reading
        {
            return status;      // Abort
        }
        *((uint16_t *)dest) = ntohs(s_data);
        return asynSuccess;
    }

    else if (nRequest == 4)     // Reading a network log
    {
        status = pasynOctetSyncIO->read(pasynUserTCPData_, (char *) &u_data, nRequest, 0.01, nRead, &eomReason);
        if (status)             // Error reading
        {
            return status;      // Abort
        }
        *((uint32_t *)dest) = ntohl(u_data);
        return asynSuccess;
    }
    else                        // Invalid length
    {
        return asynError;       // Note there was an error
    }

}

// Read the UDP Data buffer and keep track of position and length

// Read the values from a binary broadcast payload.  Returns number of full reads, or negative number on failure.
int32_t drvT4UDirect_EM::readBroadcastPayload()
{
    asynStatus status;
    size_t nRequest;
    size_t nRead;               // How many bytes read
    char c_data;
    uint16_t s_data;
    uint32_t u_data;            // Hold the data read from a header
    uint16_t bytes_read;        // Bytes read starting with frame number
    int eomReason;
    uint16_t payload_len;
    uint16_t units_current;
    uint32_t buffer_pos;

    // Read second byte of header
    status = readDataParam(1, &c_data, &nRead);
    if ((status) || (c_data != 1)) // Unsuccessful read or not part of header "B\x01"
    {
        return -1;              // Report error
    }

    status = readDataParam(2, (char *) &units_current, &nRead);
    if (status)
    {
        return -1;
    }
    bc_hdr_.units = units_current;
    
    // Read payload length
    status = readDataParam(2, (char *) &payload_len, &nRead);
    if (status)                 // Report error
    {
        return -1;
    }
    bc_hdr_.num_reads = payload_len/4/4; // 4 channels * 4 bytes/channel
            
    // If we made it this far, it is time to read the actual data points.

    // First, allocate the buffer
    nRequest = payload_len;                   // Total payload length
    bc_data_payload_ = new char[payload_len]; // Allocate the memory

    if (bc_data_payload_ == nullptr) // Error allocating memory
    {
        //-=-= DEBUGGING
        printf("nullptr Null pointer for data payload.\n");
        return -1;
    }

    if (bc_data_payload_ == NULL)
    {
        //-=-= DEBUGGING
        printf("NULL value pointer for data payload.\n");
        return -1;
    }

    status = pasynOctetSyncIO->read(pasynUserTCPData_, bc_data_payload_, nRequest, 0.01, &nRead, &eomReason);

    if (status)                 // An error
    {
        //-=-= DEBUGGING
        printf("Error reading data payload: %i.\n", status);
        fflush(stdout);
        delete bc_data_payload_; // Free the buffer
        return -1;
    }

    return bc_hdr_.num_reads;                   // Return total current readings
}

// Reads the current values from a text stream.  Returns number of full reads, or negative number on failure.
int32_t drvT4UDirect_EM::readTextCurrVals()
{
    char InData[MAX_COMMAND_LEN + 1];
    int data_matched;
    asynStatus status;
    int eomReason;
    size_t nRead;                  // How many bytes read in one command
    uint32_t bytes_read = 0;    // Cumulative bytes read
    size_t nRequest = 1;           // Read one byte at a time
    double read_vals[4];           // Hold the four read values
    

    // Null out the input array
    memset(InData, '\0', sizeof(InData));

    // Prepare to do the reads
    // We have the lock from the calling function
    
    while (1)
    {
        status = pasynOctetSyncIO->read(pasynUserTCPData_, InData+bytes_read, nRequest, 0.01, &nRead, &eomReason);
        if (nRead == 0)         // Problem reading
        {
            return -1;          // Error, so pass it up
        }
        
        bytes_read++;

        if (InData[bytes_read-1] == '\n') // End of a read command
        {
            break;
        }

        if (bytes_read >= MAX_COMMAND_LEN) // Too large
        {
            return -1;          // Return error
        }
    }

    // Now parse the values
    data_matched = sscanf(InData, "ead %lf , %lf , %lf , %lf ",
                          &read_vals[0], &read_vals[1], &read_vals[2], &read_vals[3]);
    if (data_matched != 4)        // Bad format
    {
        return -1;              // Return error
    }

    // Convert to expected double
    for (uint data_idx = 0; data_idx < 4; data_idx++)
    {
        readCurr_[data_idx] = read_vals[data_idx]; // We read in currents directly
    }

    return 1;                   // Read one set
}

T4U_Reg_T *drvT4UDirect_EM::findRegByNum(const int regNum)
{
    for (auto it = pidRegData_.begin(); it != pidRegData_.end(); it++)
    {
        if (it->reg_num == regNum)
        {
            return &(*it);
        }
    }
    return nullptr;
}

T4U_Reg_T *drvT4UDirect_EM::findRegByAsyn(const int asynParam)
{
    for (auto it = pidRegData_.begin(); it != pidRegData_.end(); it++)
    {
        if (it->asyn_num == asynParam)
        {
            return &(*it);
        }
    }
    return nullptr;
}

double drvT4UDirect_EM::rawToCurrent(int rawVal)
{
    double calcCurrent;
    const double kVREF = 1.50;

    calcCurrent = rawVal/524288.0 * kVREF / ranges_[currRange_];
    
    return calcCurrent;
}

double drvT4UDirect_EM::scaleParamToReg(double value, const T4U_Reg_T *reg_info, bool clip /*= false*/)
{
    double percent_orig;
    double scaled_value;

    //-=-= Not clipped here
    percent_orig = (value - reg_info->pv_min)/(reg_info->pv_max - reg_info->pv_min);
    
    scaled_value = percent_orig*(reg_info->reg_max - reg_info->reg_min)+reg_info->reg_min;

    if (!clip)                  // Not clipping -- default
    {
        return scaled_value;
    }

    if (scaled_value > reg_info->reg_max)
    {
        scaled_value = reg_info->reg_max;
    }
    else if (scaled_value < reg_info->reg_min)
    {
        scaled_value = reg_info->reg_min;
    }

    return scaled_value;
}

void readThread(void *drvPvt)
{
    drvT4UDirect_EM *pPvt = (drvT4UDirect_EM *)drvPvt;

    pPvt->cmdReadThread();
}

void dataReadThread(void *drvPvt)
{
    drvT4UDirect_EM *pPvt = (drvT4UDirect_EM *)drvPvt;

    pPvt->dataReadThread();
}


CmdParseState_t parseCmdName(char *cmdName)
{
    CmdParseState_t parseState;
    if (strcmp(cmdName, "tr") == 0)
    {
        //parseState = kPARSE_TR_HDR;
	parseState = kPARSE_ASC_CMD;
    }
    else if (strcmp(cmdName, "wr") == 0)
    {
        parseState = kPARSE_ASC_CMD;
    }
    else if (strcmp(cmdName, "rr") == 0)
    {
        parseState = kPARSE_ASC_CMD;
    }
    else if (strcmp(cmdName, "bc") == 0)
    {
        parseState = kPARSE_ASC_CMD;
    }
    else if (strcmp(cmdName, "bs") == 0)
    {
        parseState = kPARSE_ASC_CMD;
    }
    else                        // An unknown command
    {
        parseState = kFLUSH;    // Invalid command, so flush the socket
    }

    return parseState;
}

extern "C" {

// EPICS iocsh callable function to call constructor for the drvT4UDirect_EM class.
//-=-= TODO doxygen
    int drvT4UDirect_EMConfigure(const char *portName, const char *qtHostAddress, int ringBufferSize, int base_port_num, const char *cfgFileName)
{
    new drvT4UDirect_EM(portName, qtHostAddress, ringBufferSize, base_port_num, cfgFileName);
    return (asynSuccess);
}

// EPICS iocsh shell commands

static const iocshArg initArg0 = { "portName", iocshArgString};
static const iocshArg initArg1 = { "qt host address", iocshArgString};
static const iocshArg initArg2 = { "ring buffer size",iocshArgInt};
    static const iocshArg initArg3 = { "base port num",iocshArgInt};
    static const iocshArg initArg4 = { "config filename", iocshArgString};
    
static const iocshArg * const initArgs[] = {&initArg0,
                                            &initArg1,
                                            &initArg2,
                                            &initArg3,
					    &initArg4
};

static const iocshFuncDef initFuncDef = {"drvT4UDirect_EMConfigure",5,initArgs};
static void initCallFunc(const iocshArgBuf *args)
{
    drvT4UDirect_EMConfigure(args[0].sval, args[1].sval, args[2].ival, args[3].ival, args[4].sval);
}

void drvT4UDirect_EMRegister(void)
{
    iocshRegister(&initFuncDef,initCallFunc);
}

epicsExportRegistrar(drvT4UDirect_EMRegister);

}

