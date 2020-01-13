TOP=../..

include $(TOP)/configure/CONFIG
#----------------------------------------
#  ADD MACRO DEFINITIONS AFTER THIS LINE
#=============================

#=============================
# Build the IOC application

PROD_Linux = acmDemo
# acm.dbd will be created and installed
DBD += acmDemo.dbd

acmDemo_DBD += base.dbd
acmDemo_DBD += acmSupport.dbd

# acm_registerRecordDeviceDriver.cpp derives from acm.dbd
acmDemo_SRCS += acmDemo_registerRecordDeviceDriver.cpp

# Build the main IOC entry point on workstation OSs.
acmDemo_SRCS_DEFAULT += acmMain.cpp
acmDemo_SRCS_vxWorks += -nil-

# Finally link to the EPICS Base libraries
acmDemo_LIBS += acm
acmDemo_LIBS += $(EPICS_BASE_IOC_LIBS)

#===========================

include $(TOP)/configure/RULES
#----------------------------------------
#  ADD RULES AFTER THIS LINE
