#!/usr/bin/env python2

#
# Intel VCA Software Stack (VCASS)
#
# Copyright(c) 2015-2017 Intel Corporation.
#
# This program is free software; you can redistribute it and/or modify
# it under the terms of the GNU General Public License, version 2, as
# published by the Free Software Foundation.
#
# This program is distributed in the hope that it will be useful, but
# WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
# General Public License for more details.
#
# The full GNU General Public License is included in this distribution in
# the file called "COPYING".
#
# Intel VCA Scripts.
#


version = u"0.1.1"
import sys, datetime, os, struct, argparse


# create command line parameters
def auto_int(x): return int(x, 0)
CmdParser = argparse.ArgumentParser(description=u"Print type 15 events from smbios log")
CmdParserExGroup = CmdParser.add_mutually_exclusive_group()
CmdParserExGroup.add_argument(u"-e", u"--errors",
                              help=u"print error events only",
                              action=u"store_true",
                              default = 2)
CmdParserExGroup.add_argument(u"-a", u"--all",
                              help=u"print all events",
                              action=u"store_true",
                              default = 2)
CmdParserExGroup.add_argument(u"-t", u"--types",
                              nargs="+",
                              type=auto_int,
                              help=u"print specified events only",
                              metavar=u"TYPE",
                              default=())
CmdParserExGroup.add_argument(u"-p", u"--print_types",
                              help=u"print event types with associated numbers for use with --types option",
                              action=u"store_true",
                              default = 2)
CmdParser.add_argument(u"-s", u"--statistics",
                       help=u"print events statistics",
                       action=u"store_true")
CmdParserExGroupFiles = CmdParser.add_mutually_exclusive_group()
CmdParserExGroupFiles.add_argument(u"-d", u"--download_log",
                                   help=u"download log data from NVRAM to binary file",
                                   default=u"")
CmdParserExGroupFiles.add_argument(u"-i", u"--input_file",
                                   help=u"parse file downloaded with download_log option",
                                   default=u"")
CmdParser.add_argument(u"--debug",
                       help=u"print additional debug logs",
                       action=u"store_true")

CmdParser.add_argument(u"-v", u"--version",
                       help=u"print version",
                       action=u"store_true",
                       default = False)

Parameters = CmdParser.parse_args()


def empty_parser():
    return ()


def parse_ecc_error_data():
    global CurShift, fp
    CurShift -= 3
    return (ord(fp.read(1)), ord(fp.read(1)), ord(fp.read(1)))

def parse_pci_error_data():
    global CurShift, fp, biosdata
    if biosdata[2] < u'012':
        return (0xff,0xff,0xff)
    CurShift -= 6
    fp.seek(3,1)
    return (ord(fp.read(1)), ord(fp.read(1)), ord(fp.read(1)))

EventDatas = {
    0x01 : (u"Single Bit ECC Memory Error", u"S%x:C%x:D%x", parse_ecc_error_data),
    0x02 : (u"Multi Bit ECC Memory Error", u"S%x:C%x:D%x", parse_ecc_error_data),
    0x03 : (u"Parity Memory Error", u"", empty_parser),
    0x04 : (u"Bus Time Out", u"", empty_parser),
    0x05 : (u"I/O Channel Check", u"", empty_parser),
    0x06 : (u"Software NMI", u"", empty_parser),
    0x07 : (u"POST Memory Resize", u"", empty_parser),
    0x08 : (u"POST Errors", u"", empty_parser),
    0x09 : (u"PCI Parity Error", u"B%x:D%x:F%x", parse_pci_error_data),
    0x0A : (u"PCI System Error", u"B%x:D%x:F%x", parse_pci_error_data),
    0x0B : (u"CPU Failure", u"", empty_parser),
    0x0C : (u"EISA Failsafe Timer Timeout", u"", empty_parser),
    0x0D : (u"Correctable Memory Log Disabled", u"", empty_parser),
    0x0E : (u"Logging Disabled for Event Type", u"", empty_parser),
    0x10 : (u"System Limit Exceeded", u"", empty_parser),
    0x11 : (u"Asyn HW Timer Expired", u"", empty_parser),
    0x12 : (u"System Configuration Information", u"", empty_parser),
    0x13 : (u"Hard Disk Information", u"", empty_parser),
    0x14 : (u"System Reconfigured", u"", empty_parser),
    0x15 : (u"Uncorrectable CPU Complex Error", u"", empty_parser),
    0x16 : (u"Log Area Reset", u"", empty_parser),
    0x17 : (u"System Boot", u"", empty_parser),
    0xE0 : (u"OEM0",u"",empty_parser),
    0xE1 : (u"OEM1",u"",empty_parser),
    0xE2 : (u"OEM2",u"",empty_parser)
    #0xFF : (u"End of log",u"",empty_parser)
}


ErrorEvents = [0x01, 0x02, 0x03, 0x04, 0x08, 0x09, 0x0A, 0x0B, 0x0C, 0x15]
AllEvents = EventDatas.keys()
OemEvents = [0xE0,0xE1,0xE2]


def download_nvram():
    try:
        bf = open(Parameters.download_log, u"wb")
        global fp
        fp.seek(offset)
        bf.write(fp.read(0x10000))
        bf.close()
        print(u"Log successfully downloaded to file %s" % Parameters.download_log)
    except IOError as e:
        print(u"Cannot open output binary file %s" % Parameters.download_log)
        sys.exit(-1)


def print_error_info(errorDesc):
    print(errorDesc)
    global fp
    print(u"File position: %d", fp.tell)
    return None


def print_statics_types(isstat = True):
    if isstat:
        print(u"Events statistics:")
        print(u"                                    Type ID ")
        print(u" Name                               dec   hex     Class         Nb")
        print(u"-------------------------------------------------------------------")
    else:
        print(u"Types of events:")
        print(u"                                    Type ID          ")
        print(u" Name                               dec   hex     Class")
        print(u"---------------------------------------------------------")
    global EventDatas, EventStatistics
    for s in AllEvents:
        if s in ErrorEvents:
            ec = u"error"
        elif s in OemEvents:
            ec = u"oem"
        else:
            ec = u"status"
        EventData = EventDatas[s]
        if isstat:
            print(u" %-35s%3d  0x%02x     %-10s%6d" % (EventData[0], s, s, ec, EventStatistics[s]))
        else:
            print(u" %-35s%3d  0x%02x     %-10s" % (EventData[0], s,s, ec))

    return None


def parse_time():
    year = bcd_to_bin(ord(fp.read(1))) + 2000
    month = bcd_to_bin(ord(fp.read(1)))
    day = bcd_to_bin(ord(fp.read(1)))
    hour = bcd_to_bin(ord(fp.read(1)))
    min = bcd_to_bin(ord(fp.read(1)))
    sec = bcd_to_bin(ord(fp.read(1)))
    tm = datetime.datetime(2000, 1, 1, 0, 0, 0)
    try:
        tm = datetime.datetime(
            year,
            month,
            day,
            hour,
            min,
            sec
        )
    except ValueError:
        if Debug == 1:
            print_error_info(u"Invalid time record")
    global CurShift
    CurShift -= 6
    return tm


def parse_bios_handle():
    BIOSHandle = fp.read(2)
    BIOSHandle = struct.unpack("@H", BIOSHandle)[0]
    global CurShift
    CurShift -= 2
    if Debug == 1: print("BIOSHandle %d" % BIOSHandle)
    return BIOSHandle


def parse_evt_cnt():
    EvnCnt = fp.read(4)
    EvnCnt = struct.unpack("@I", EvnCnt)[0]
    global CurShift
    CurShift -= 4
    loc_cnt = 1
    while EvnCnt != 0xffffffff:
        loc_cnt += 1
        EvnCnt >>= 1
        EvnCnt |= 0x80000000
    if Debug == 1: print(u"EvtCnt %x" % EvnCnt)
    return loc_cnt

def parse_dword():
    DWORD = fp.read(4)
    DWORD = struct.unpack("@I", DWORD)[0]
    if Debug ==1: print(u"POSTResultBitmap %08x" % DWORD)
    global CurShift
    CurShift -= 4
    return DWORD

def bcd_to_bin(par):
    res = 0
    curMul = 1
    while par != 0:
        res += curMul * (par % 16)
        curMul *= 10
        par /= 16
    return res


def print_events():
    PrintedEvent = 0
    while True:
        global TypeEvt,fp,Debug,CurShift,EvtCnt,EventsToPrint
        TypeEvt = ord(fp.read(1))
        if TypeEvt == 0xff: break

        CurShift = ord(fp.read(1)) - 2

        CurTm = parse_time()
        EvtLogFrm = ord(fp.read(1))
        CurShift -= 1

        MulEvtCnt = 0
        BiosHandle = 0

        POSTRestBmp = 0
        SysMngType = 0

        # parse data
        if EvtLogFrm == 0x01:
            BiosHandle = parse_bios_handle()
        elif EvtLogFrm == 0x02:
            MulEvtCnt = parse_evt_cnt()
        elif EvtLogFrm == 0x03:
            BiosHandle = parse_bios_handle()
            MulEvtCnt = parse_evt_cnt()
        elif EvtLogFrm == 0x04:
            POSTRestBmp = parse_dword()
        elif EvtLogFrm == 0x05:
            SysMngType = parse_dword()
        elif EvtLogFrm == 0x06:
            SysMngType = parse_dword()
            MulEvtCnt = parse_evt_cnt()
        elif 0x80 <= EvtLogFrm <= 0xFF:
            if Debug == 1:
                print(u"OEM EvtLogFrm: %d" %  EvtLogFrm)
        else:
            fp.seek(CurShift, 1)
            if Debug == 1:
                print(u"TypeEvt %d" % TypeEvt)
                print(u"EvtLogFrm: %d" % EvtLogFrm)
            continue

        # parse specific event data
        if TypeEvt in AllEvents:
            EventData = EventDatas[TypeEvt]
            ErrInfo = EventData[2]()

            if TypeEvt in EventsToPrint:
                print(EventData[0])
                if CurTm.year == 2000:
                    print(u"   Date: <unknown>")
                else:
                    print(u"   Date: " + CurTm.strftime(u"%Y-%m-%d %H:%M:%S"))
                if len(ErrInfo) > 0 : print(u"   " + (EventData[1] % ErrInfo))
                PrintedEvent += 1
            EventStatistics[TypeEvt] += 1
            EvtCnt += 1
        else:
            if TypeEvt != 0 and Debug == 1:
                print_error_info(u"Invalid event type %d" % TypeEvt)
        fp.seek(CurShift, 1)
    if PrintedEvent == 0 and Parameters.statistics == False:
        print(u"No events")

def open_memory_file():
    global fp,mfp
    try:
        fp = open(mfp, u"rb")
    except IOError as e:
        print(u"Cannot open memory file %s" % (mfp))
        sys.exit(-1)
    fp.seek(offset)

def GetBIOSInformation():
    biosversion =(False,u"",0,0)
    cmdresp = os.popen(u'dmidecode | grep -A 20 "BIOS Information"')
    bioslines = cmdresp.read().split('\n')
    linescnt = 1
    for line in bioslines:
        if line.find(u"Version") == -1:
            continue
        lines = line.split(u": ")
        # lines[1] is like "0ACIE204.0000" on MV, "VCAA.3.00.00.T05" on VCAA
        biosname = lines[1][0:5]
        if biosname != u'VCAA.' and biosname != u'VCGA.' and biosname != u'0ACGC' and biosname != u'0ACIE':
            break
        biosmajor = lines[1][5:8]
        biosminor = lines[1][9:13]
        biosversion = (True,biosname,biosmajor,biosminor)
        break
    return biosversion

Debug = 0
CurShift = 0
EvtCnt = 0
EventStatistics = {key: 0 for key in AllEvents}
offset = 0
mfp = Parameters.input_file
fp = 0
EventsToPrint = []
biosdata = ()

if Parameters.debug:
    Debug = 1

if Parameters.print_types == True:
    print_statics_types(False)
    sys.exit(0)

if Parameters.version:
    print(u"vca_elog version: %s" % version)
    sys.exit(0)

Info_not_supported_platform = \
    u'Reading NVRAM on %s is not supported. Please use option "-i" commonly with "-e" "-a" "-t" and "-s"'

if len(mfp) == 0:
    if os.name == u"nt":
        print(Info_not_supported_platform % u'Windows')
        sys.exit(-1)
    else:
        biosdata = GetBIOSInformation()
        if biosdata[1] == u'0ACGC':
            if (biosdata[0] == False) or (biosdata[2] < u'008'):
                print(Info_not_supported_platform % u'this platform')
                print(u"vca_elog.py script can be run only on ValleyVista node on Linux with BIOS version >= 0ACGC008.0000")
                sys.exit(-1)
            offset = 0xcb0010
        elif biosdata[1] == u'0ACIE':
            offset = 0xb60010
        elif biosdata[1][0:2] == u'VC':
            print('BIOS platform: ' + biosdata[1])
            # unknown offset
        else:
            offset = 0xb60010
            print(u"E3S10 platform BIOS")
        offset += 4 * 1024 * 1024 * 1024 - 16 * 1024 * 1024
        mfp = u"/dev/mem"

if len(Parameters.download_log):
    if Parameters.errors == True or Parameters.all == True or len(Parameters.types) != 0 or Parameters.statistics:
        print(u"Warning: When NVRAM area is downloaded, error report and statistics are not printed")
    open_memory_file()
    download_nvram()
else:
    if len(Parameters.types) > 0:
        EventsToPrint = Parameters.types
    elif Parameters.all == True:
        EventsToPrint = AllEvents
    else:
        EventsToPrint = ErrorEvents
        if Parameters.statistics and (Parameters.errors == 2):
            EventsToPrint = []
    if Debug == 1: print(EventsToPrint)
    open_memory_file()
    print_events()
    if Parameters.statistics:
        print(u"\nSTATISTICS:")
        print(u"Number of log events: %d" % EvtCnt)
        print_statics_types()
