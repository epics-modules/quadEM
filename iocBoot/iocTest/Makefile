TOP = ../..
include $(TOP)/configure/CONFIG
#ARCH = vxWorks-ppc604_long
#ARCH = linux-x86
ARCH = linux-x86_64
#ARCH = win32-x86
#ARCH=windows-x64
# vxWorks architecture needs a cdCommands file
buildInstall: cdCommands envPaths dllPath.bat

include $(TOP)/configure/RULES.ioc
