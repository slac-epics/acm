# Makefile at top of application tree
TOP = .
include $(TOP)/configure/CONFIG

# Directories to build, any order
DIRS += configure

DIRS += acmApp
acmApp_DEPEND_DIRS += configure

DIRS += demoApp
demoApp_DEPEND_DIRS += acmApp

DIRS += testApp
testApp_DEPEND_DIRS += acmApp

DIRS += $(wildcard iocBoot)
iocBoot_DEPEND_DIRS += configure

# Add any additional dependency rules here:

include $(TOP)/configure/RULES_TOP
