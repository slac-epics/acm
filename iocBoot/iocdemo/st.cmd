#!../../bin/linux-x86_64/acmDemo

dbLoadDatabase("../../dbd/acmDemo.dbd",0,0)
acmDemo_registerRecordDeviceDriver(pdbbase) 

acmSetup("sim", "127.0.0.1", "127.0.0.1:50006")

dbLoadRecords("../../db/acm_base.db","P=TST:,DEV=sim,DEBUG=0xf")

dbLoadRecords("../../db/acm_regval.db","P=TST:,DEV=sim,REG=REG0,OFF=0")
dbLoadRecords("../../db/acm_regval.db","P=TST:,DEV=sim,REG=REG1,OFF=1")
dbLoadRecords("../../db/acm_regval.db","P=TST:,DEV=sim,REG=REG2,OFF=2")

iocInit()
