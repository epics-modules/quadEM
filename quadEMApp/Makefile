TOP = ..
include $(TOP)/configure/CONFIG
DIRS := $(DIRS) quadEMSrc
DIRS := $(DIRS) caenSrc
caenSrc_DEPEND_DIRS += quadEMSrc
DIRS := $(DIRS) nslsSrc
nslsSrc_DEPEND_DIRS += quadEMSrc
#DIRS := $(DIRS) pyramidSrc
#pyramidSrc_DEPEND_DIRS += quadEMSrc
DIRS := $(DIRS) sensicSrc
sensicSrc_DEPEND_DIRS += quadEMSrc
DIRS := $(DIRS) sydorSrc
sydorSrc_DEPEND_DIRS += quadEMSrc

DIRS := $(DIRS) $(filter-out $(DIRS), $(wildcard *db*))
DIRS := $(DIRS) $(filter-out $(DIRS), $(wildcard *Db*))
DIRS := $(DIRS) $(filter-out $(DIRS), $(wildcard *op*))
src_DEPEND_DIRS += sydorSrc
include $(TOP)/configure/RULES_DIRS

