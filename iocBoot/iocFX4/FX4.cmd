epicsEnvSet("PREFIX",    "QE1:")
epicsEnvSet("RECORD",    "FX4:")
epicsEnvSet("PORT",      "FX4")
epicsEnvSet("TEMPLATE",  "FX4")
epicsEnvSet("QSIZE",     "20")
epicsEnvSet("RING_SIZE", "10000")
epicsEnvSet("TSPOINTS",  "2048")
epicsEnvSet("IP",        "164.54.161.10")

drvFX4Configure("$(PORT)", "$(IP)", $(RING_SIZE))
dbLoadRecords("$(QUADEM)/db/$(TEMPLATE).template", "P=$(PREFIX), R=$(RECORD), PORT=$(PORT), FXP=$(IP):, ADDR=0, TIMEOUT=1")

< $(QUADEM)/iocBoot/quadEM_Plugins.cmd

asynSetTraceIOMask("$(PORT)",0,2)

asynSetTraceMask("$(PORT)",  0, ERROR | DRIVER)

< $(QUADEM)/iocBoot/saveRestore.cmd

iocInit()

# save settings every thirty seconds
create_monitor_set("auto_settings.req",30,"P=$(PREFIX), R=$(RECORD)")
