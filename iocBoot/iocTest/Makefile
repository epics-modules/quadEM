TOP = ../..
include $(TOP)/configure/CONFIG
#ARCH = vxWorks-ppc604_long
ARCH = linux-x86
#ARCH = win32-x86
# vxWorks architecture needs a cdCommands file
buildInstall: cdCommands envPaths

include $(TOP)/configure/RULES.ioc
