#Makefile at top of application tree
TOP = .
include $(TOP)/configure/CONFIG
DIRS := $(DIRS) $(filter-out $(DIRS), configure)
DIRS := $(DIRS) quadEMApp
DIRS := $(DIRS) quadEMTestApp
quadEMTestApp_DEPEND_DIRS += quadEMApp
DIRS := $(DIRS) $(filter-out $(DIRS), $(wildcard iocBoot))
quadEMApp_DEPEND_DIRS = configure
include $(TOP)/configure/RULES_TOP
