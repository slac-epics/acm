LCLS2 ACM Receiver
==================

See [ACM Protocol](proto.md) specification doc.

Run [simulator](devsim.py) with `python3.7 devsim.py -B 127.0.0.1 127.0.0.1:50006`.

Test [Wireshark protocol disector](acm.lua) with `wireshark -X lua_script:acm.lua regdata.pcapng.gz`.

EPICS Driver
------------

Requires EPICS Base >= 3.15 to build

```sh
git clone https://github.com/slaclab/lcls2-bcs-epics-acm.git
cd lcls2-bcs-epics-acm
cat EPICS_BASE=/path/to/epics/base >> configure/RELEASE.local
make
```

To run, copy/edit `iocBoot/iocdemo/st.cmd` as a starting point.

eg. to setup to listen for traffic originating from device `W.X.Y.Z`
and destined for local ports 1234 and 5678.

```
#!../../bin/linux-x86_64/acmDemo

dbLoadDatabase("../../dbd/acmDemo.dbd",0,0)
acmDemo_registerRecordDeviceDriver(pdbbase)

acmSetup("mydev1", "W.X.Y.Z", "0.0.0.0:1234 0.0.0.0:5678")

dbLoadRecords("../../db/acm.db","P=DEV1:,DEV=mydev1,DEBUG=0xf")

iocInit()
```
