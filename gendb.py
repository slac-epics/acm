#!/usr/bin/env python3
"""
Translate data from Trent Allison's register spreadsheets
to .substitutions files.  Save as .csv and the pass
to this script.

eg.

"Register Name","Num","Description","Sign","Width",,"Hex","Scaling","Offset","Low","High","Units"
"ProbeBeamRawI",0,"Cavity Probe Port Beam Current Raw I","signed",19,,"8",1,0,-262144,262143,"none"
"ProbeBeamPhs",5,"Cavity Probe Port Beam Current Phase","signed",22,,"1C",8.58306884765625E-05,0,-180,179.999914169312,"Degrees"

"""

import sys
from csv import DictReader
from math import log10

input = sys.argv[1]
output = sys.argv[2]

ireg = []
areg = []

with open(input, 'r') as F:
    for row in DictReader(F):
        if row.get('Units')=='none':
            row['Units'] = ''

        if 'Prec' not in row:
            row['Prec'] = max(0, round(-log10(float(row['Scaling']))))

        if float(row['Scaling'])==1.0 and float(row['Offset'])==0:
            ireg.append(row)
        else:
            areg.append(row)

with open(output, 'w') as F:
    F.write('''
file "acm_base.db"
{
{P="\$(P)",	DEV="\$(DEV)",	DEBUG="\$(DEBUG)"}
}

file "acm_analog.db"
{
#{P="\$(P)",	DEV="\$(DEV)",	REG="",	OFF="",	HOPR="",	LOPR="", ASLO="", AOFF="", PREC="", EGU=""}
''')

    for row in areg:
        F.write('{{P="\$(P)",	DEV="\$(DEV)",	REG="{Register Name}",	OFF="{Num}",	HOPR="{High}",	LOPR="{Low}", ASLO="{Scaling}", AOFF="{Offset}", PREC="{Prec}", EGU="{Units}"}}\n'.format(**row))

    F.write('''}

file "acm_regval.db"
{
#{P="\$(P)",	DEV="\$(DEV)",	REG="",	OFF="",	HOPR="",	LOPR=""}
''')

    for row in ireg:
        F.write('{{P="\$(P)",	DEV="\$(DEV)",	REG="{Register Name}",	OFF="{Num}",	HOPR="{High}",	LOPR="{Low}", EGU="{Units}"}}\n'.format(**row))

    F.write('''}
''')
