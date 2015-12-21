#drvAsynIPPortConfigure("portName","hostInfo",priority,noAutoConnect,
#                        noProcessEos)
drvAsynIPPortConfigure("IP_$(PORT)", "$(IP)", 0, 0, 0)
asynOctetSetInputEos("IP_$(PORT)",  0, "\r\n")
asynOctetSetOutputEos("IP_$(PORT)", 0, "\r")

# Set both TRACE_IO_ESCAPE (for ASCII command/response) and TRACE_IO_HEX (for binary data)
asynSetTraceIOMask("IP_$(PORT)", 0, 6)
#asynSetTraceFile("IP_$(PORT)",   0, "AHxxx.out")
#asynSetTraceMask("IP_$(PORT)",   0,  9)

# Load asynRecord record
dbLoadRecords("$(ASYN)/db/asynRecord.db", "P=$(PREFIX), R=asyn1,PORT=IP_$(PORT),ADDR=0,OMAX=256,IMAX=256")

drvAHxxxConfigure("$(PORT)", "IP_$(PORT)", $(RING_SIZE), $(MODEL))
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
