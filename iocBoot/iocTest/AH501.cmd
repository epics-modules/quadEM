epicsEnvSet("PREFIX",    "quadEMTest:")
epicsEnvSet("RECORD",    "AH501")
epicsEnvSet("PORT",      "AH501")
epicsEnvSet("TEMPLATE",  "AH501")
epicsEnvSet("MODEL",     "AH501D")
epicsEnvSet("QSIZE",     "20")
epicsEnvSet("RING_SIZE", "10000")
epicsEnvSet("TSPOINTS",  "1000")
epicsEnvSet("IP",        "164.54.160.240:10001")

< AHxxx.cmd
dbLoadRecords("$(QUADEM)/db/AH501.template", "P=$(PREFIX), R=$(RECORD):, PORT=$(PORT)")

< saveRestore.cmd

iocInit()

# save settings every thirty seconds
create_monitor_set("auto_settings.req",30,"P=$(PREFIX), R=$(RECORD):, R_TS=$(RECORD)_TS:")

seq("quadEM_SNL", "P=$(PREFIX), R=$(RECORD)_TS:, NUM_CHANNELS=2048")
