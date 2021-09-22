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
    "FwVersion",
    "Faults",
    "TestModes",
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
    "DwnConSerNumHi",
    "DwnConSerNumLo",
    "DwnConTemp",
    "DwnConVolt",
    "DwnConCurr",
    "DwnConTime",
    "CavTempRTD",
]

def main(args):
    T = ET.parse(os.path.join(datadir, 'acm_reg_grid.ui')).getroot()

#    print (ET.tostring(T.find("widget/layout/item")))
    boxes = T.find('widget/layout')
#    print (ET.tostring(boxes))
    proto = boxes.find('item')
    boxes.remove(proto)

    nextY = 0
    nextX = 0
    for name in regs:
        W = deepcopy(proto)
        W.find('widget').set('name', name)
        W.set('column', str(nextX))
        W.set('row', str(nextY))
        W.find("widget/property[@name='macros']/string").text = 'REG=' + name
        print (ET.tostring(W))
        nextY += 1
        if nextY >=20:
            nextX += 1
            nextY = 0
        boxes.append(W)

    with open(args.output,'wb') as F:
        F.write(b'<?xml version="1.0" encoding="UTF-8"?>\r\n')
        F.write(ET.tostring(T))

if __name__=='__main__':
    args = getargs().parse_args()
    logging.basicConfig(level=args.debug)
    main(args)
