TOP=../..

include $(TOP)/configure/CONFIG
#----------------------------------------
#  ADD MACRO DEFINITIONS AFTER THIS LINE
#=============================

USR_CPPFLAGS += -DUSE_TYPED_RSET

# fault on any undefined symbols (eg. from missing LIB_SYS_LIBS)
#USR_LDFLAGS += -Wl,--no-undefined -Wl,--no-allow-shlib-undefined

#=============================
# Build the IOC application

LIBRARY_IOC = acm

DBD += acmSupport.dbd

acm_SRCS += sock.cpp
acm_SRCS += driver.cpp
acm_SRCS += shell.cpp
acm_SRCS += dset.cpp

# Finally link to the EPICS Base libraries
acm_LIBS += $(EPICS_BASE_IOC_LIBS)

#===========================

include $(TOP)/configure/RULES
#----------------------------------------
#  ADD RULES AFTER THIS LINE

dset$(DEP): $(COMMON_DIR)/acmVCS.h

$(COMMON_DIR)/acmVCS.h: FORCE
	PERLLIB=$(EPICS_BASE)/lib/perl $(PERL) $(TOP)/genVersionHeader.pl -t $(TOP) -N ACM_VCS -V "" $@

.PHONY: FORCE
