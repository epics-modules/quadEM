TOP = ..
include $(TOP)/configure/CONFIG
DIRS := $(DIRS) sydorSrc
DIRS := $(DIRS) src
DIRS := $(DIRS) $(filter-out $(DIRS), $(wildcard *db*))
DIRS := $(DIRS) $(filter-out $(DIRS), $(wildcard *Db*))
DIRS := $(DIRS) $(filter-out $(DIRS), $(wildcard *op*))
src_DEPEND_DIRS += sydorSrc
include $(TOP)/configure/RULES_DIRS

