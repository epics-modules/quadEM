/*
 * drvT4U_EM.h
 *                              
 * Asyn driver that inherits from the drvQuadEM class to control the Sydor
 * T4U electrometer
 * Author: Iain Marcuson
 *
 * Created October 24, 2022
 */

#include <forward_list>
#include <cstdint>

#include <arpa/inet.h>

#include "drvQuadEM.h"
#include "epicsRingPointer.h"

#define MAX_COMMAND_LEN 256
#define MAX_PORTNAME_LEN 32
#define MAX_IPNAME_LEN 16
#define MAX_RANGES 8
#define T4U_EM_TIMEOUT 0.1
#define MAX_CHAN_READS 16       // The maximum number of channel reads to be sent in one message
#define NUM_RANGES 3
#define T4U_CMD_QUEUE_LEN 300


#define P_SampleFreq_String "QE_SAMPLE_FREQ"
#define P_BiasN_En_String "QE_BIAS_N"
#define P_BiasP_En_String "QE_BIAS_P"
#define P_BiasN_Voltage_String "QE_BIAS_N_VOLTAGE"
#define P_BiasP_Voltage_String "QE_BIAS_P_VOLTAGE"
#define P_PulseBias_En_String "QE_PULSE_BIAS"
#define P_PulseBias_OffCnt_String "QE_PULSE_BIAS_OFF"
#define P_PulseBias_OnCnt_String "QE_PULSE_BIAS_ON"
#define P_DACMode_String "QE_DAC_MODE"
#define P_PosTrackMode_String "QE_POS_TRACK_MODE"
#define P_PIDEn_String "QE_PID_EN"
#define P_Updater_String "QE_UPDATE_REG"
#define P_PIDCuEn_String "QE_PID_CU_EN"
#define P_PIDHystEn_String "QE_PID_HYST_EN"
#define P_PIDCtrlPol_String "QE_PID_POL"
#define P_PIDCtrlEx_String "QE_PID_EXT_CTRL"
#define P_WaitStateMode_String "QE_WSMODE"
#define P_ReadsPerPacket_String "QE_RPP"

#include "gc_t4u_hdr_string.h"

typedef struct {
    int reg_num;            // Register address on T4U
    int asyn_num;               // The asyn param number assigned
    double pv_min;              // The minimum value of the PV to scale
    double pv_max;              // The maximum value of the PV to scale
    double reg_min;             // The minimum value of the scaled PV
    double reg_max;             // The maximum value of the scaled PV
} T4U_Reg_T;

typedef struct {
    uint32_t len;
    uint32_t pos;
    char *buffer;
} DataBuffer_T;

#pragma pack(push,1)
typedef struct {
    uint16_t total_len;
    uint32_t frame_num;
    uint16_t gain;
    uint16_t decimation;
    uint32_t status;
    uint16_t units;
    uint32_t num_reads;
} T4U_Payload_Header_T;

typedef struct _T4UMetadata
{
    uint64_t timestamp;         /**< time since start *100nS */
    uint32_t frameNumber;       /**< frame number since start, 1-based */
    uint32_t status;            /**< status flags (same as status register) */
    uint16_t gain;              /**< TIA Gain */
    uint16_t overSamples;       /**< tbd */
    uint16_t adcGain;           /**< ADC Gain */
    uint16_t decimation;        /**< ADC Decimation */
    uint16_t units;			/** 0 = RAW counts, 1 = uA */
    uint32_t numberOfReads;      /* number of reads contained in imagedata*/
} T4UMetadata;

typedef struct T4UErrors
{
    int16_t data[16];   /**< Increase as needed */
} T4UErrors;

#define kT4U_MAX_DATA_SIZE 500

typedef int32_t T4UData[ kT4U_MAX_DATA_SIZE * 4];


typedef struct _T4UFrame
{
    T4UMetadata metadata;
    T4UErrors   errors;
    T4UData     image;
    char        checksum;
} T4UFrame;

#pragma pack(pop)

const unsigned int T4U_CMD_PORT = 23;
const unsigned int T4U_DATA_PORT = 10101;

/** Class to control the Sydor T4U Electrometer */
class drvT4UDirect_EM : public drvQuadEM {
public:
    drvT4UDirect_EM(const char *portName, const char *qtHostAddress, int ringBufferSize, unsigned int base_port_num, const char *cfgFileName);

    /* These are the methods we implement from asynPortDriver */
    void report(FILE *fp, int details);

    /* These are the metods that are new to this class */
    void cmdReadThread(void);
    void dataReadThread(void);
    int32_t parseConfigFile(const char *cfgFileName);
    virtual void exitHandler();

    /* These are functions extended from drvQuadEM */
    virtual asynStatus writeInt32(asynUser *pasynUser, epicsInt32 value);
    virtual asynStatus writeFloat64(asynUser *pasynUser, epicsFloat64 value);

protected:
    int P_BiasN_En;
#define FIRST_T4U_COMMAND P_BiasN_En
    int P_BiasP_En;
    int P_BiasN_Voltage;
    int P_BiasP_Voltage;
    int P_PulseBias_En;
    int P_PulseBias_OffCnt;
    int P_PulseBias_OnCnt;
    int P_SampleFreq;
    int P_DACMode;
    int P_PosTrackMode;
    int P_PIDEn;
    int P_Update_Reg;
    int P_PIDCuEn;
    int P_PIDHystEn;
    int P_PIDCtrlPol;
    int P_PIDCtrlEx;
    int P_WaitStateMode;
    int P_ReadsPerPacket;
#include "gc_t4u_hdr_member.h"

    /* These are the methods we implement from quadEM */
    virtual asynStatus setAcquire(epicsInt32 value);
    virtual asynStatus setPingPong(epicsInt32 value);
    virtual asynStatus setIntegrationTime(epicsFloat64 value);
    virtual asynStatus setRange(epicsInt32 value);
    virtual asynStatus setValuesPerRead(epicsInt32 value);
    virtual asynStatus readStatus();
    virtual asynStatus reset();
    virtual T4U_Reg_T *findRegByNum(const int regNum);
    virtual T4U_Reg_T *findRegByAsyn(const int asynParam);
    
private:
    /* Our data */
    char *broadcastAddress_;
    char tcpCommandPortName_[MAX_PORTNAME_LEN];
    char tcpDataPortName_[MAX_PORTNAME_LEN];
    char udpDataPortName_[MAX_PORTNAME_LEN];
    asynUser *pasynUserTCPCommand_;
    asynUser *pasynUserTCPData_;
    asynUser *pasynUserUDPData_;
    epicsEventId acquireStartEvent_;
    epicsEventId writeCmdEvent_;
    double ranges_[MAX_RANGES];
    double scaleFactor_;
    int readingActive_;
    char firmwareVersion_[MAX_COMMAND_LEN];
    char ipAddress_[MAX_IPNAME_LEN];
    char outCmdString_[MAX_COMMAND_LEN];
    char inCmdString_[MAX_COMMAND_LEN];
    double readCurr_[MAX_CHAN_READS*4]; // The values read from the socket
    double calSlope_[4];
    double calOffset_[4];
    float fullSlope_[NUM_RANGES][4];
    float fullOffset_[NUM_RANGES][4];
    int currRange_;
    char *bc_data_payload_;      // Broadcast data payload
    T4U_Payload_Header_T bc_hdr_; // Broadcast data header
    
    std::forward_list<T4U_Reg_T> pidRegData_; /* Holds parameters for PID regs */
    epicsRingPointer<char> *cmd_queue;
    
    asynStatus writeReadMeter();
    asynStatus getFirmwareVersion();
    void process_reg(const T4U_Reg_T *reg_lookup, double value);
    asynStatus readResponse();
    int32_t readTextCurrVals();
    double scaleParamToReg(double value, const T4U_Reg_T *reg_info, bool clip = false);
    double rawToCurrent(int rawVal);
    int32_t processReceivedCommand(char *cmdString);
    int32_t processRegVal(int reg_num, uint32_t reg_val);
    asynStatus readDataParam(size_t nRequest, char *dest, size_t *nRead);
    int32_t readBroadcastPayload();
    int32_t readDataBuf(DataBuffer_T *buf, char *dest, uint32_t size);
};
