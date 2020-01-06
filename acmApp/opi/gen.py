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
    "TestMode",
    "DigitizerTempA",
    "DigitizerTempB",
    "DigitizerTempC",
    "DigitizerTempD",
    "DigitizerTemp",
    "Digitizer12Volts",
    "Digitizer3p7Volts",
    "Digitizer3p3Volts",
    "Digitizer2p2Volts",
    "DigitizerCurrent",
    "DigitizerLOPower",
    "DwnConSerNumHi",
    "DwnConSerNumLo",
    "DwnConTemp",
    "DwnConVolt",
    "DwnConCurr",
    "DwnConTime",
    "CavTempRTD",
]

def main(args):
    T = ET.parse(os.path.join(datadir, 'acm_reg.opi')).getroot()

    proto = T.find('widget')
    T.remove(proto)

    nextY = 0
    for name in regs:
        W = deepcopy(proto)
        W.find('y').text = str(nextY)
        W.find('macros/REG').text = name
        nextY += int(W.find('height').text)
        T.append(W)

    with open(args.output,'wb') as F:
        F.write(b'<?xml version="1.0" encoding="UTF-8"?>\r\n')
        F.write(ET.tostring(T))

if __name__=='__main__':
    args = getargs().parse_args()
    logging.basicConfig(level=args.debug)
    main(args)
