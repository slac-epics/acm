#!/usr/bin/env python3
"""Basic software simulation of ACM
"""

import sys
if sys.version_info<(3,7):
    print('Requires python >=3.7')
    sys.exit(1)

import signal
import enum
import struct
import logging
import socket
import asyncio

_log = logging.getLogger(__name__)

class msgID(enum.IntEnum):
    Reg  = 0x51
    Trip = 0xe7
    Int  = 0x33
    Ext  = 0x28

LastFlag = 0x01

TBFreq = 1.0e6 # Hz

RegFreq = 1.0 # Hz

class ACMSim:
    def __init__(self, loop, dests):
        self.loop, self.dests = loop, dests
        self.transport = None

        self.regcount = 0
        self.T0 = loop.time()
        self.regsender = None

    async def close(self):
        if self.regsender is not None:
            self.regsender.cancel()
            try:
                await self.regsender
            except asyncio.CancelledError:
                pass # expected

    def connection_made(self, transport):
        self.transport = transport
        self.regsender = asyncio.create_task(self.send_register_data())

    def datagram_received(self, data, src):
        _log.warn('Silence broken by %s', src)

    def error_received(self, exc):
        _log.error('Error received: %s', exc)

    # if self.transport.close()
    def connection_lost(self, exc):
        _log.error("Connection closed")

    async def send_register_data(self):
        while True:
            now = self.loop.time()
            TB = int((now-self.T0)*TBFreq) # cnts
            TB &= 0xffffffff

            # header and 3 register values
            msg = struct.pack('>BBHIIII', msgID.Reg, LastFlag, 0, TB,
                              0xdeadbeef, self.regcount, 0xf1234567)
            self.regcount += 1

            for dest in self.dests:
                _log.debug('send register data to %s', dest)
                self.transport.sendto(msg, dest)

            await asyncio.sleep(1.0/RegFreq)

def getargs():
    def endpoint(val):
        host, _sep, port = val.partition(':')
        return host, int(port or '0')

    from argparse import ArgumentParser
    P = ArgumentParser()
    P.add_argument('-v','--verbose', action='store_const', dest='level', default=logging.INFO, const=logging.DEBUG,
                   help='Make more noise')
    P.add_argument('dest', nargs='+', type=endpoint,
                   help='List of target endpoints in IP:port# form')
    P.add_argument('-B','--bind', type=endpoint, default=':0',
                   help='Address to bind sender')
    return P

async def main(args):
    loop = asyncio.get_running_loop()

    _log.debug('Sending to: %s', args.dest)

    sock, proto = await loop.create_datagram_endpoint(lambda: ACMSim(loop, args.dest),
                                                      local_addr=args.bind,
                                                      family=socket.AF_INET,
                                                      allow_broadcast=True)

    _log.debug('Bound to %s', sock.get_extra_info('sockname'))

    done = asyncio.Future()

    loop.add_signal_handler(signal.SIGINT, done.set_result, None)
    loop.add_signal_handler(signal.SIGTERM, done.set_result, None)
    loop.add_signal_handler(signal.SIGQUIT, done.set_result, None)

    _log.info('Running')
    await done

    await proto.close()

if __name__=='__main__':
    args = getargs().parse_args()
    logging.basicConfig(level=args.level,
                        format='%(asctime)s %(levelname)s %(message)s')
    asyncio.run(main(args), debug=args.level==logging.DEBUG)
    _log.info('Done')
