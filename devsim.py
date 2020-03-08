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

import numpy

_log = logging.getLogger(__name__)

class msgID(enum.IntEnum):
    Reg  = 0x51
    Trip = 0xe7
    Int  = 0x33
    Ext  = 0x28

LastFlag = 0x01

TBFreq = 9.43e6 # Hz

RegFreq = 4.0 # Hz (actual 100Hz)

NSamples = 32768

dsamples = numpy.dtype([
    ('i', '>i2'),
    ('q', '>i2'),
    ('cur', '>i4'),
    ('pha', '>i4'),
    ('int', '>i4'),
])

def alog_error(fn):
    async def wrapper(self):
        try:
            return await fn(self)
        except asyncio.CancelledError:
            raise
        except:
            _log.exception('Error')
            raise
    return wrapper

class ACMSim:
    def __init__(self, loop):
        self.loop = loop
        self.transport = None
        self.peer = None

        self.regcount = 0
        self.iphase = 0.0
        self.T0 = loop.time()
        self.tasks = []

    async def close(self):
        for task in self.tasks:
            task.cancel()
            try:
                await task
            except asyncio.CancelledError:
                pass # expected

    def connection_made(self, transport):
        self.transport = transport
        self.tasks += [
            asyncio.create_task(self.send_register_data()),
            asyncio.create_task(self.send_internal_data()),
        ]

    def datagram_received(self, data, src):
        _log.info('Registering peer %s', src)
        self.peer = src

    def error_received(self, exc):
        _log.error('Error received: %s', exc)

    # if self.transport.close()
    def connection_lost(self, exc):
        _log.error("Connection closed")

    @property
    def time_base(self):
        now = self.loop.time()
        TB = int((now-self.T0)*TBFreq) # cnts
        TB &= 0xffffffff
        return TB

    @alog_error
    async def send_register_data(self):
        while True:
            TB = self.time_base

            regs = numpy.arange(73, dtype='>u4')
            regs[52] = 0xdeadbeef # FwVersion
            # make something change
            regs[0] = self.regcount
            regs[1] = self.regcount ^ 0xffffffff
            self.regcount += 1

            msg = struct.pack('>BBHI', msgID.Reg, LastFlag, 0, TB) + regs.tobytes()

            if self.peer is not None:
                _log.debug('send register data to %s', self.peer)
                self.transport.sendto(msg, self.peer)

            await asyncio.sleep(1.0/RegFreq)

    @alog_error
    async def send_internal_data(self):
        while True:
            TB = self.time_base

            T = numpy.arange(NSamples)/TBFreq
            A = 0.5 + 0.5/T[-1]*T
            P = numpy.mod(self.iphase + (numpy.pi*2)/T[-1]*T, numpy.pi*2)

            I = A*numpy.sin(P)
            Q = A*numpy.cos(P)
            S = A.cumsum()/T[-1]
            S = S/S.max() # arbitrary

            P = (P/numpy.pi)-1

            self.iphase += numpy.pi/12.

            assert I.max()<=1.0 and I.min()>=-1.0, (I.max(), I.min())
            assert Q.max()<=1.0 and Q.min()>=-1.0, (Q.max(), Q.min())
            assert A.max()<=1.0 and A.min()>=-1.0, (A.max(), A.min())
            assert P.max()<=1.0 and P.min()>=-1.0, (P.max(), P.min())
            assert S.max()<=1.0 and S.min()>=-1.0, (S.max(), S.min())

            data = numpy.ndarray(NSamples, dtype=dsamples)

            data['i'] = I*0x7fff
            data['q'] = Q*0x7fff
            data['cur'] = A*0x7fffff
            data['pha'] = P*0x7fffff
            data['int'] = S*0x7fffffff

            data = data.tobytes()
            for i in range(0, len(data), 1024):
                last = 0 if i+1024 < len(data) else LastFlag

                msg = struct.pack('>BBHI', msgID.Int, last, i>>10, TB) + data[i:i+1024]

                if self.peer is not None:
                    _log.debug('send register data to %s', self.peer)
                    self.transport.sendto(msg, self.peer)

            await asyncio.sleep(5.0)

def getargs():
    def endpoint(val):
        host, _sep, port = val.partition(':')
        return host, int(port or '0')

    from argparse import ArgumentParser
    P = ArgumentParser()
    P.add_argument('-v','--verbose', action='store_const', dest='level', default=logging.INFO, const=logging.DEBUG,
                   help='Make more noise')
    P.add_argument('bind', type=endpoint, default=':0',
                   help='Address to bind sender')
    return P

async def main(args):
    loop = asyncio.get_running_loop()

    sock, proto = await loop.create_datagram_endpoint(lambda: ACMSim(loop),
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
