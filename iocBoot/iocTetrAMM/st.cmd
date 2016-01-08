#!/epics/support2/quadEM/bin/linux-x86_64/quadEMTestApp

errlogInit(5000)
epicsEnvSet("QUADEM",	"/epics/support2/quadEM")
epicsEnvSet("QUAD_DET", "NSLS_EM.cmd")
epicsEnvSet("IOC",	"iocTetrAMM")
< $(QUADEM)/iocBoot/$(IOC)/envPaths
< unique.cmd

# Tell EPICS all about the record types, device-support modules, drivers,
# etc. in this build
dbLoadDatabase("$(QUADEM)/dbd/quadEMTestApp.dbd")
quadEMTestApp_registerRecordDeviceDriver(pdbbase)

# The search path for database files
# Note: the separator between the path entries needs to be changed to a semicolon (;) on Windows
epicsEnvSet("EPICS_DB_INCLUDE_PATH", "$(ADCORE)/db:$(QUADEM)/db")

< $(QUADEM)/iocBoot/$(IOC)/TetrAMM.cmd
