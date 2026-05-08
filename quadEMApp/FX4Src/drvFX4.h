/*
 * drvFX4.h
 *
 * Asyn driver that inherits from the drvQuadEM class to control the Pyramid FX4 4-channel picoammeter
 *
 * Author: Mark Rivers
 *
 * Created May 1, 2026
 */

#include "drvQuadEM.h"

#include <websocketpp/config/asio_no_tls_client.hpp>
#include <websocketpp/client.hpp>
#include <json.hpp>
#include <list>

using json = nlohmann::json;
using websocketpp::connection_hdl;
using websocketpp::lib::bind;
using websocketpp::lib::placeholders::_1;
using websocketpp::lib::placeholders::_2;


typedef websocketpp::client<websocketpp::config::asio_client> client;

typedef struct {
    double val;
    double time;
} valuePair;

/** Class to control the Pyrimid FX4 4-Channel current meter */
class drvFX4 : public drvQuadEM {
public:
    drvFX4(const char *portName, const char *FX4_IP, int ringBufferSize);

    /* These are the methods we implement from asynPortDriver */
    void report(FILE *fp, int details);

    /* These are the methods that are new to this class */
    /* These are the methods that are new to this class */
    void pollThread(void);
    virtual void exitHandler();

protected:
    /* These are the methods we implement from quadEM */
    virtual asynStatus readStatus();
    virtual asynStatus reset();
    virtual asynStatus setAcquire(epicsInt32 value);
    virtual asynStatus setAcquireMode(epicsInt32 value);
    virtual asynStatus setAveragingTime(epicsFloat64 value);
    virtual asynStatus setNumAcquire(epicsInt32 value);
    virtual asynStatus setTriggerMode(epicsInt32 value);
    virtual asynStatus setTriggerPolarity(epicsInt32 value);
    virtual asynStatus setValuesPerRead(epicsInt32 value);
    int P_InterlockStatus;

private:
    void on_open(connection_hdl hdl);
    void on_message(connection_hdl hdl, client::message_ptr msg);
    void sendEventData(const std::string& event, json data);
    void sendSubscribeEvent();
    void sendUnsubscribeEvent();
    void sendGetEvent();
    void onMessageEvent(const std::string& event, const json& data);
    asynStatus setAcquireParams();
    client ws_client_;
    connection_hdl ws_hdl_;
    bool FX4Connected_;
    std::thread *ws_thread_;
    static constexpr int FX4_NUM_CHANS = 4;
    static inline const std::array<std::string, FX4_NUM_CHANS> 
      ADC_PATHS = {"/fx4/adc/channel_1/value", "/fx4/adc/channel_2/value", "/fx4/adc/channel_3/value", "/fx4/adc/channel_4/value"};
    static inline const std::string GATE_PATH = "/fx4/gpio_0/22/readback/value";
    std::array<std::list<valuePair>, FX4_NUM_CHANS> adcCache_;
    std::vector<valuePair> gateCache_;
    epicsInt64 startTime_;
    bool synchronized_;
    bool timestampMismatch_;
};

