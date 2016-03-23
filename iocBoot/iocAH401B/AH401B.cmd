epicsEnvSet("PREFIX",    "quadEMTest:")
epicsEnvSet("RECORD",    "AH401B:")
epicsEnvSet("PORT",      "AH401B")
epicsEnvSet("TEMPLATE",  "AH401B")
epicsEnvSet("MODEL",     "AH401B")
#epicsEnvSet("MODEL",     "AH401D")
epicsEnvSet("QSIZE",     "20")
epicsEnvSet("RING_SIZE", "10000")
epicsEnvSet("TSPOINTS",  "1000")
epicsEnvSet("IP",        "164.54.160.242:10001")

< ../AHxxx.cmd

dbLoadRecords("$(QUADEM)/db/AH401B.template", "P=$(PREFIX), R=$(RECORD), PORT=$(PORT)")

< ../saveRestore.cmd

iocInit()

# save settings every thirty seconds
create_monitor_set("auto_settings.req",30,"P=$(PREFIX), R=$(RECORD)")
