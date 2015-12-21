epicsEnvSet("PREFIX",    "quadEMTest:")
epicsEnvSet("RECORD",    "NSLS_EM")
epicsEnvSet("PORT",      "NSLS_EM")
epicsEnvSet("TEMPLATE",  "NSLS_EM")
epicsEnvSet("QSIZE",     "20")
epicsEnvSet("RING_SIZE", "10000")
epicsEnvSet("TSPOINTS",  "1000")
epicsEnvSet("BROADCAST", "164.54.160.255")
epicsEnvSet("MODULE_ID", "0")

# Load asynRecord record
dbLoadRecords("$(ASYN)/db/asynRecord.db", "P=$(PREFIX), R=asyn1,PORT=TCP_Command_$(PORT),ADDR=0,OMAX=256,IMAX=256")

drvNSLS_EMConfigure("$(PORT)", "$(BROADCAST)", $(MODULE_ID), $(RING_SIZE))

asynSetTraceIOMask("UDP_$(PORT)", 0, 2)
asynSetTraceMask("UDP_$(PORT)", 0, 9)
asynSetTraceIOMask("TCP_Command_$(PORT)", 0, 2)
#asynSetTraceMask("TCP_Command_$(PORT)", 0, 9)
asynSetTraceIOMask("TCP_Data_$(PORT)", 0, 2)
#asynSetTraceMask("TCP_Data_$(PORT)", 0, 9)

dbLoadRecords("$(QUADEM)/db/$(TEMPLATE).template", "P=$(PREFIX), R=$(RECORD):, PORT=$(PORT)")

< commonPlugins.cmd

asynSetTraceIOMask("$(PORT)",0,2)
#asynSetTraceMask("$(PORT)",  0,9)

# initFastSweep(portName, inputName, maxSignals, maxPoints)
#  portName = asyn port name for this new port (string)
#  inputName = name of asynPort providing data
#  maxSignals  = maximum number of signals (spectra)
#  maxPoints  = maximum number of channels per spectrum
#  dataString  = drvInfo string for current and position data
#  intervalString  = drvInfo string for time interval per point
initFastSweep("$(PORT)TS", "$(PORT)", 11, 2048, "QE_INT_ARRAY_DATA", "QE_SAMPLE_TIME")
dbLoadRecords("$(QUADEM)/db/quadEM_TimeSeries.template", "P=$(PREFIX),R=$(RECORD)_TS:,NUM_TS=2048,NUM_FREQ=1024,PORT=$(PORT)TS")

< saveRestore.cmd

iocInit()

# save settings every thirty seconds
create_monitor_set("auto_settings.req",30,"P=$(PREFIX), R=$(RECORD):, R_TS=$(RECORD)_TS:")

seq("quadEM_SNL", "P=$(PREFIX), R=$(RECORD)_TS:, NUM_CHANNELS=2048")
