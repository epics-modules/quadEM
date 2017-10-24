<envPaths

dbLoadDatabase("../../dbd/testBusyAsyn.dbd")
testBusyAsyn_registerRecordDeviceDriver(pdbbase)

epicsEnvSet("PREFIX",    "TBA:")
epicsEnvSet("PORT",      "TBA")

testBusyAsynConfigure("$(PORT)")

dbLoadRecords("./busyTest.db","P=$(PREFIX),R=busy1,PORT=$(PORT),PARAM=P1")
dbLoadRecords("./busyTest.db","P=$(PREFIX),R=busy2,PORT=$(PORT),PARAM=P2")
dbLoadRecords("./busyTest.db","P=$(PREFIX),R=busy3,PORT=$(PORT),PARAM=P3")
dbLoadRecords("./busyTest.db","P=$(PREFIX),R=busy4,PORT=$(PORT),PARAM=P4")
dbLoadRecords("./busyTest.db","P=$(PREFIX),R=busy5,PORT=$(PORT),PARAM=P5")
dbLoadRecords("$(ASYN)/db/asynRecord.db","P=$(PREFIX):,R=asyn1,PORT=$(PORT),ADDR=0,OMAX=80,IMAX=80")
asynSetTraceIOMask("$(PORT)",0,2)
asynSetTraceMask("$(PORT)",0,255)

< ../saveRestore.cmd

date
iocInit()
date

# save settings every thirty seconds
create_monitor_set("auto_settings.req",30,"P=$(PREFIX)")

epicsThreadSleep(2)
date
dbpr $(PREFIX)busy1
dbpr $(PREFIX)busy2
dbpr $(PREFIX)busy3
dbpr $(PREFIX)busy4
asynReport 1 $(PORT)

epicsThreadSleep(3)
date
dbpf $(PREFIX)busy3 1
dbpf $(PREFIX)busy4 1

epicsThreadSleep(3)
date
dbpr $(PREFIX)busy1
dbpr $(PREFIX)busy2
dbpr $(PREFIX)busy3
dbpr $(PREFIX)busy4
asynReport 1 $(PORT)

epicsThreadSleep(3)
date
dbpf $(PREFIX)busy5 1
