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
"PilotMagCav1",
"ConfMagCav1",
"PilotMagCav2",
"ConfMagCav2",
"TimeCheck",
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
"UpConTemp",
"UpConVolt",
"UpConCurr",
"UpConTime",
"DwnConTemp",
"DwnConVolt",
"DwnConCurr",
"DwnConTime",
"SprTempRTD"
]

regBits = [
"FwVersion",
"Status",
"TestModes",
"UpConSerNumHi",
"UpConSerNumLo",
"DwnConSerNumHi",
"DwnConSerNumLo",
"TestModeLclRem",
"TestModeLcl",
"TestModeRem",
"TestMode",
"PilotEnaCav1",
"ConfEnaCav1",
"PilotEnaCav2",
"ConfEnaCav2"
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
