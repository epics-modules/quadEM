epicsEnvSet("PREFIX",    "quadEMTest:")
epicsEnvSet("RECORD",    "AH501:")
epicsEnvSet("PORT",      "AH501")
epicsEnvSet("TEMPLATE",  "AH501")
#epicsEnvSet("MODEL",     "AH501D")
epicsEnvSet("MODEL",     "AH501BE")
epicsEnvSet("QSIZE",     "20")
epicsEnvSet("RING_SIZE", "10000")
epicsEnvSet("TSPOINTS",  "1000")
epicsEnvSet("IP",        "164.54.160.206:10001")

< $(QUADEM)/iocBoot/AHxxx.cmd

< $(QUADEM)/iocBoot/saveRestore.cmd

iocInit()

# save settings every thirty seconds
create_monitor_set("auto_settings.req",30,"P=$(PREFIX), R=$(RECORD)")
