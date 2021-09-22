#!/usr/bin/env python3

import logging
import os
from copy import deepcopy
import xml.etree.ElementTree as ET

datadir = os.path.dirname(__file__)

def getargs():
    from argparse import ArgumentParser
    P = ArgumentParser()
    P.add_argument('-d','--debug',action='store_const', const=logging.DEBUG, default=logging.INFO)
    P.add_argument('-q','--quiet',action='store_const', const=logging.WARN, dest='debug')
    P.add_argument('output')
    return P

regs = [
    "ProbeBeamRawI",
    "ProbeBeamRawQ",
    "ProbeBeamI",
    "ProbeBeamQ",
    "ProbeBeamMag",
    "ProbeBeamPhs",
    "ProbeBeam",
    "ProbeBeamMagPeak",
    "ProbeBeamPeak",
    "ProbeBeamLim",
    "ProbeBeamConfLim",
    "ProbeDarkI",
    "ProbeDarkQ",
    "ProbeDarkMag",
    "ProbeDarkPhs",
    "ProbePilotRawI",
    "ProbePilotRawQ",
    "ProbePilotI",
    "ProbePilotQ",
    "ProbePilotMag",
    "ProbePilotPhs",
    "ProbePilotMagHi",
    "ProbePilotMagLo",
    "TestConfRawI",
    "TestConfRawQ",
    "TestConfI",
    "TestConfQ",
    "TestConfMag",
    "TestConfPhs",
    "TestConfMagLim",
    "TestPilotRawI",
    "TestPilotRawQ",
    "TestPilotI",
    "TestPilotQ",
    "TestPilotMag",
    "TestPilotPhs",
    "TestPilotOffsetPhs",
    "TestPilotDiffMag",
    "TestPilotDiffPhs",
    "TestPilotDiffMagHi",
    "TestPilotDiffMagLo",
    "TestPilotDiffPhsHi",
    "TestPilotDiffPhsLo",
    "GenPilotRawI",
    "GenPilotRawQ",
    "GenPilotI",
    "GenPilotQ",
    "GenPilotMag",
    "GenPilotPhs",
    "GenPilotDiffMag",
    "GenPilotDiffPhs",
    "TimeCheck",
    "BufTrigLim",
    "WFPacketDelay",
    "DigitizerTempA",
    "DigitizerTempB",
    "DigitizerTempC",
    "DigitizerTempD",
    "DigitizerTemp",
    "Digitizer6p1Volts",
    "Digitizer3p7Volts",
    "Digitizer3p3Volts",
    "Digitizer2p2Volts",
    "DigitizerCurrent",
    "DigitizerLOVrms",
    "DwnConTemp",
    "DwnConVolt",
    "DwnConCurr",
    "DwnConTime",
    "CavTempRTD"
]

regBits = [
    "FwVersion",
    "Faults",
    "TestModes",
    "TestModeLcl",
    "TestModeRem",
    "TestMode",
    "VrfyTestState",
    "DwnConSerNumHi",
    "DwnConSerNumLo",
    "FaultFast",
    "FaultSlow",
    "FaultPilotMag",
    "FaultPilotDiffMag",
    "FaultPilotDiffPhs",
    "FaultConfMag",
    "FaultPromTimeout",
    "MonitorTimeout",
    "TestModeLclRem"
]

def main(args):
    T = ET.parse(os.path.join(datadir, 'acm_reg.ui')).getroot()

    boxes = T.find('widget')
    proto = boxes.find('widget')
    boxes.remove(proto)

    nextY = 0
    nextX = 0
    for name in regs:
        W = deepcopy(proto)
        W.set('name', name)
        W.find('property/rect/x').text = str(nextX)
        W.find('property/rect/y').text = str(nextY)
        W.find("property[@name='macros']/string").text = 'REG=' + name
        W.find("property[@name='filename']/string").text = '_acm_ai_reg.ui'
        nextY += int(W.find('property/rect/height').text) + 2
        if nextY >=600:
            nextX += 620
            nextY = 0
        boxes.append(W)

    for name in regBits:
        W = deepcopy(proto)
        W.set('name', name)
        W.find('property/rect/x').text = str(nextX)
        W.find('property/rect/y').text = str(nextY)
        W.find("property[@name='macros']/string").text = 'REG=' + name
        W.find("property[@name='filename']/string").text = '_acm_reg.ui'
        nextY += int(W.find('property/rect/height').text) + 2
        if nextY >=600:
            nextX += 620
            nextY = 0
        boxes.append(W)

    boxes.find('property/rect/width').text = str(nextX + 410)
    if nextX > 0:
        boxes.find('property/rect/height').text = str(610)
    else:
        boxes.find('property/rect/height').text = str(nextY)

    with open(args.output,'wb') as F:
        F.write(b'<?xml version="1.0" encoding="UTF-8"?>\r\n')
        F.write(ET.tostring(T))

if __name__=='__main__':
    args = getargs().parse_args()
    logging.basicConfig(level=args.debug)
    main(args)
