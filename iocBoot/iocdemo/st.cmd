#!../../bin/linux-x86_64/acmDemo

dbLoadDatabase("../../dbd/acmDemo.dbd",0,0)
acmDemo_registerRecordDeviceDriver(pdbbase) 

acmSetup("sim", "127.0.0.1", "127.0.0.1:50006")

dbLoadRecords("../../db/acm.db","P=TST:,DEV=sim,DEBUG=0xf")

iocInit()
