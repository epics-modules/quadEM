epicsEnvSet("PREFIX",    "quadEMTest:")
epicsEnvSet("RECORD",    "APS_EM:")
epicsEnvSet("PORT",      "APS_EM")
epicsEnvSet("TEMPLATE",  "APS_EM")
epicsEnvSet("QSIZE",     "20")
epicsEnvSet("RING_SIZE", "10000")
epicsEnvSet("TSPOINTS",  "2048")

ipacAddVIPC616_01("0x3000,0xa0000000")
# Initialize Greenspring IP-Unidig
# initIpUnidig(char *portName, 
#              int carrier, 
#              int slot,
#              int msecPoll,
#              int intVec, 
#              int risingMask, 
#              int fallingMask)
# portName  = name to give this asyn port
# carrier     = IPAC carrier number (0, 1, etc.)
# slot        = IPAC slot (0,1,2,3, etc.)
# msecPoll    = polling time for input bits in msec.  Default=100.
# intVec      = interrupt vector
# risingMask  = mask of bits to generate interrupts on low to high (24 bits)
# fallingMask = mask of bits to generate interrupts on high to low (24 bits)
# The Quad-EM input is on IP-Unidig input 0 so we disable interrupts for bi records on that line
initIpUnidig("Unidig1", 0, 1, 2000, 116, 0xfffffe, 0xfffffe)

# drvAPS_EMConfigure(const char *portName, unsigned short *baseAddr, int fiberChannel,
#                    const char *unidigName, int unidigChan, char *unidigDrvInfo)
#  portName     = name of APS_EM asyn port driver created 
#  baseAddress = base address of VME card
#  channel     = 0-3, fiber channel number
#  unidigName  = name of ipInidig server if it is used for interrupts.
#                Set to 0 if there is no IP-Unidig being used, in which
#                case the quadEM will be read at 60Hz.
#  unidigChan  = IP-Unidig channel connected to quadEM pulse output
#  unidigDrvInfo = drvInfo string for digital input parameter
# The Quad-EM input is on IP-Unidig input 0
drvAPS_EMConfigure("$PORT)", 0xf000, 0, "Unidig1", 0, "DIGITAL_INPUT")
dbLoadRecords("$(QUADEM)/db/$(TEMPLATE).template", "P=$(PREFIX), R=$(RECORD), PORT=$(PORT), ADDR=0, TIMEOUT=1")

# Fast feedback using EPID record
# We don't actually load this, because that requires the synApps "std" and "dac128V" modules 
# which we don't include in this example application
#dbLoadTemplate("quadEM_pid.substitutions")

< $(QUADEM)/iocBoot/commonPlugins.cmd

asynSetTraceIOMask("$(PORT)",0,2)
#asynSetTraceMask("$(PORT)",  0,9)

< ../saveRestore.cmd

iocInit()

# save settings every thirty seconds
create_monitor_set("auto_settings.req",30,"P=$(PREFIX), R=$(RECORD)")
