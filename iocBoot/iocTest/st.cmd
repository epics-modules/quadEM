errlogInit(5000)
< envPaths

# Tell EPICS all about the record types, device-support modules, drivers,
# etc. in this build
dbLoadDatabase("../../dbd/quadEMTestApp.dbd")
quadEMTestApp_registerRecordDeviceDriver(pdbbase)

# The search path for database files
epicsEnvSet("EPICS_DB_INCLUDE_PATH", "$(ADCORE)/db")

< st.cmd.AH401B
#< st.cmd.AH501
#< st.cmd.TetrAMM
