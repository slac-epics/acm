TOP=..

include $(TOP)/configure/CONFIG
#----------------------------------------
#  ADD MACRO DEFINITIONS AFTER THIS LINE
#=============================

USR_CPPFLAGS += -DUSE_TYPED_RSET
USR_CPPFLAGS += -I$(TOP)/acmApp/src

#=============================
# Build the IOC application

TARGETS += $(COMMON_DIR)/acmTest.dbd
DBDDEPENDS_FILES += acmTest.dbd$(DEP)
acmTest_DBD += base.dbd
acmTest_DBD += acmSupport.dbd
TESTFILES += $(COMMON_DIR)/acmTest.dbd

PROD_SRCS += acmTest_registerRecordDeviceDriver.cpp
PROD_LIBS += acm

TESTPROD += testreconstruct
testreconstruct_SRCS += testreconstruct.cpp
TESTS += testreconstruct

# Finally link to the EPICS Base libraries
PROD_LIBS += $(EPICS_BASE_IOC_LIBS)

TESTSCRIPTS_HOST += $(TESTS:%=%.t)

#===========================

include $(TOP)/configure/RULES
#----------------------------------------
#  ADD RULES AFTER THIS LINE

