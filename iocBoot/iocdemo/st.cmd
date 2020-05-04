#!../../bin/linux-x86_64/acmDemo

dbLoadDatabase("../../dbd/acmDemo.dbd",0,0)
acmDemo_registerRecordDeviceDriver(pdbbase) 

acmSetup("sim", "127.0.0.1:50006 127.0.0.1:50007")

dbLoadRecords("../../db/acm.db","P=TST:,DEV=sim,DEBUG=0x1")
dbLoadRecords("../../db/acm_tonegen.db","P=TSTG:,DEV=sim,DEBUG=0x1")

iocInit()

dbl > records.dbl
