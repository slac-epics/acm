
#include <linux/net_tstamp.h>

#include <string.h>

#include <epicsAtomic.h>
#include <epicsMath.h>
#include <dbDefs.h>

#include "acm_drv.h"

extern "C" {
int acmRxTimeoutMS = 5000;
int acmReconstructTimeoutMS = 1000;
}

PartialSequence::PartialSequence()
    :lastSeq(-1)
    ,pushed(false)
{
    firstRx.secPastEpoch = 0;
    firstRx.nsec = 0;
}

CompleteSequence::CompleteSequence()
    :timeBase(0u)
    ,ntotal(0u)
{
    timeReceived.secPastEpoch = 0;
    timeReceived.nsec = 0;

    scanIoInit(&scanUpdate);
    scanIoInit(&totalUpdate);
}

epicsUInt32 CompleteSequence::at(size_t i) const
{
    if(complete.empty())
        throw std::out_of_range("Sequence Empty");

    // we assume all packets of a sequence have the same length
    size_t pktSIze = complete.begin()->second.values.size();

    size_t seq = i/pktSIze,
           off = i%pktSIze;

    PartialSequence::packets_t::const_iterator it = complete.find(seq);
    if(it==complete.end())
        throw std::out_of_range("Missing Sequence entry");

    return it->second.values.at(off);
}

DriverSock::DriverSock(Driver *driver, osiSockAddr& bindAddr)
    :driver(driver)
    ,running(true)
    ,sock(AF_INET, SOCK_DGRAM)
{
    // we are running from an iocsh function
    assert(bindAddr.ia.sin_family==AF_INET);

    {
        int ret = bind(sock.sock, &bindAddr.sa, sizeof(bindAddr.ia));
        if(ret) {
            int err = SOCKERRNO;
            Socket::ShowAddr A(bindAddr);
            throw std::runtime_error(SB()<<"bind('"<<A.buf<<"') error "<<err<<" : "<<strerror(err));
        }
    }

    // Find actual socket name (including port)
    {
        osiSocklen_t len = sizeof(bindAddr.ia);
        int ret = getsockname(sock.sock, &bindAddr.sa, &len);
        if(ret)
            LOGDRV(1, driver, "Warning: unable to getsockname() -> %d (%d)\n", int(ret), SOCKERRNO);
    }
    this->bindAddr = bindAddr;

    // keep a string representation of the bind address for use in messages
    Socket::ShowAddr A(bindAddr);
    bindName = SB()<<driver->name<<"/"<<A.buf;

    // setup RX timeout
    {
        unsigned long long timeout = acmRxTimeoutMS*1000; // us
        timeval timo;
        timo.tv_sec = timeout/1000000;
        timo.tv_usec = timeout%1000000;

        int ret = setsockopt(sock.sock, SOL_SOCKET, SO_RCVTIMEO, &timo, sizeof(timo));
        if(ret) {
            int err = SOCKERRNO;
            LOGDRV(1, driver, "Warning: %s Unable to set RX timeout to %d ms : (%d) %s\n",
                   bindName.c_str(), acmRxTimeoutMS,
                   err, strerror(err));
        }
    }

    // setup RX timestamp capture.
    //   See Linux kernel source  Documentation/networking/timestamping.txt
    {
        unsigned opts = SOF_TIMESTAMPING_RX_HARDWARE|SOF_TIMESTAMPING_RX_SOFTWARE|SOF_TIMESTAMPING_SOFTWARE;

        int ret = setsockopt(sock.sock, SOL_SOCKET, SO_TIMESTAMPING, &opts, sizeof(opts));
        if(ret) {
            int err = SOCKERRNO;
            LOGDRV(1, driver, "Warning: %s Unable to enable RX time capture : (%d) %s\n",
                   bindName.c_str(),
                   err, strerror(err));
        }
    }

    worker.reset(new epicsThread(*this,
                                 ("ACM "+bindName).c_str(),
                                 epicsThreadGetStackSize(epicsThreadStackBig),
                                 epicsThreadPriorityHigh
                                 ));
    // worker started later through initHook
}

DriverSock::~DriverSock()
{
    {
        Guard G(driver->lock);
        running = false;
    }
    sock.close();
    worker->exitWait();
}

void DriverSock::run()
{
    LOGDRV(2, driver, "Worker starting\n");
    PacketData::values_t rxBuf;

    Guard G(driver->lock);

    epicsTimeStamp now;
    epicsTimeGetCurrent(&now);

    while(running) {
        try {
            if(!driver->intimeout) {
                double age = epicsTimeDiffInSeconds(&now, &driver->lastRx);

                if(age > acmRxTimeoutMS/1000.0) {
                    driver->intimeout = true;
                    driver->onTimeout();
                }
            }

            // check for time'd out, or obsolete, sequences.
            for(Driver::sequences_t::iterator it_seq=driver->sequences.begin();
                it_seq!=driver->sequences.end(); ++it_seq)
            {
                for(CompleteSequence::partials_t::iterator it_partial=it_seq->second.partials.begin();
                    it_partial!=it_seq->second.partials.end();)
                {
                    CompleteSequence::partials_t::iterator cur = it_partial++;
                    assert(cur->second.firstRx.secPastEpoch!=0);
                    double age = epicsTimeDiffInSeconds(&now, &cur->second.firstRx);
                    bool expired = age > acmReconstructTimeoutMS/1000.0;

                    if(expired) {
                        if(!cur->second.pushed) {
                            LOGDRV(8, driver, "Timeout %02x:%08x age=%f\n", it_seq->first, cur->first, age);
                            ::epics::atomic::increment(driver->nTimeout);
                        }
                        it_seq->second.partials.erase(cur);
                    }
                }
            }

            struct {
                uint8_t cmd, flags;
                uint16_t seqNum;
                uint32_t timebase;
            } header;
            STATIC_ASSERT(sizeof(header)==8);

            osiSockAddr peer;
            epicsTimeStamp rxTime;
            unsigned rxTimeSrc=0;
            {
                UnGuard U(G); // unlock for I/O

                rxBuf.reserve(0x10000/4u); // theoretical max for UDP

                // all sorts of fun to prepare for recvmsg()
                union {
                    char buf[64]; // not sure how to choose this size.  This works...
                    cmsghdr align;
                } control;

                iovec vec[2];
                vec[0].iov_base = &header;
                vec[0].iov_len = sizeof(header);
                vec[1].iov_base = &rxBuf[0];
                vec[1].iov_len = 4*rxBuf.capacity(); // uint32_t to bytes

                msghdr msg;
                memset(&msg, 0 , sizeof(msg));
                msg.msg_name = &peer.sa;
                msg.msg_namelen = sizeof(peer);
                msg.msg_iov = vec;
                msg.msg_iovlen = NELEMENTS(vec);
                msg.msg_control = &control;
                msg.msg_controllen = sizeof(control);

                driver->testCycle.signal();

                // actually I/O
                ssize_t ret = recvmsg(sock.sock, &msg, 0);
                int err = SOCKERRNO;
                epicsTimeGetCurrent(&now);
                if(!running) {
                    break;

                } else if(ret<0) {

                    if(err==EAGAIN) {
                        // timeout
                        LOGDRV(1, driver, "RX Timeout %s\n", bindName.c_str());

                    } else {
                        ::epics::atomic::increment(driver->nError);
                        LOGDRV(1, driver, "Error Rx on %s : (%d, %d) %s\n", bindName.c_str(), unsigned(ret), err, strerror(err));
                        epicsThreadSleep(5.0); // slow down error spam
                    }
                    continue; // will test sequence timeout above
                }
                LOGDRV(2, driver, "Worker recvmsg() -> %d\n", int(ret));

                if(peer.sa.sa_family!=AF_INET ||
                        peer.ia.sin_addr.s_addr!=driver->peer.ia.sin_addr.s_addr)
                {
                    // ignore traffic from unknown peer
                    ::epics::atomic::increment(driver->nIgnore);
                    continue;
                }
                ::epics::atomic::increment(driver->nRX);

                if(msg.msg_flags&MSG_TRUNC) {
                    ::epics::atomic::increment(driver->nError);
                    LOGDRV(1, driver, "Error: truncated msg %u -> %u\n", ntohs(peer.ia.sin_port), ntohs(bindAddr.ia.sin_port));
                    continue;
                } else if(ret<8) {
                    ::epics::atomic::increment(driver->nError);
                    LOGDRV(1, driver, "Error: runt msg %u -> %u\n", ntohs(peer.ia.sin_port), ntohs(bindAddr.ia.sin_port));
                    continue;
                }

                rxBuf.resize((size_t(ret)-sizeof(header))/4u);
                for(size_t i=0; i<rxBuf.size(); i++)
                    rxBuf[i] = ntohl(rxBuf[i]);

                if(msg.msg_flags&MSG_CTRUNC) {
                    LOGDRV(1, driver, "Warning: truncated cmsg %u -> %u\n", ntohs(peer.ia.sin_port), ntohs(bindAddr.ia.sin_port));
                }

                for(cmsghdr *cmsg = CMSG_FIRSTHDR(&msg); cmsg; cmsg = CMSG_NXTHDR(&msg, cmsg))
                {
                    if(cmsg->cmsg_level==SOL_SOCKET && cmsg->cmsg_type==SO_TIMESTAMPING) {
                        // stamp[0] - SW stamp
                        // stamp[1] - deprecated
                        // stamp[2] - HW stamp (maybe)
                        timespec *stamp = (timespec *)CMSG_DATA(cmsg);

                        if(stamp[2].tv_sec!=0 && stamp[2].tv_nsec!=0) {
                            rxTime.secPastEpoch = stamp->tv_sec - POSIX_TIME_AT_EPICS_EPOCH;
                            rxTime.nsec = stamp->tv_nsec;
                            rxTimeSrc = 2;

                        } else if(stamp[0].tv_sec!=0 && stamp[0].tv_nsec!=0) {
                            rxTime.secPastEpoch = stamp->tv_sec - POSIX_TIME_AT_EPICS_EPOCH;
                            rxTime.nsec = stamp->tv_nsec;
                            rxTimeSrc = 1;

                        }
                    }
                }

                if(rxTimeSrc==0) {
                    rxTime = now;
                }
                LOGDRV(4, driver, "RX timestamp%u %08x:%08x\n", rxTimeSrc, unsigned(rxTime.secPastEpoch), unsigned(rxTime.nsec));

                // finally time to process RX buffer

                header.seqNum = ntohs(header.seqNum);
                header.timebase = ntohl(header.timebase);
                LOGDRV(8, driver, "RX %02x:%08x:%04x\n", header.cmd, header.timebase, header.seqNum);
            }
            // locked again

            driver->lastRx = rxTime;
            driver->intimeout = false;

            if(rxTimeSrc!=0) {
                // TODO revise time mapping with rxTime and header.timebase
            }

            // insert this packet into a sequence
            CompleteSequence& seq = driver->sequences[header.cmd];

            // late arrival from completed/expired sequence?

            PartialSequence& partial = seq.partials[header.timebase];

            if(partial.firstRx.secPastEpoch==0 && partial.firstRx.nsec==0) {
                // added new partial
                partial.firstRx = rxTime;
            }
            assert(partial.firstRx.secPastEpoch!=0);

            if(partial.pushed) {
                // sequence already pushed.
                // late, duplicate, or otherwise invalid
                epics::atomic::increment(driver->nIgnore);
                LOGDRV(8, driver, "Ignore dup/late/invalid %02x:%08x:%04x\n", header.cmd, header.timebase, header.seqNum);

            } else if(partial.lastSeq<0 || int(header.seqNum)<partial.lastSeq) {
                // this packet may be new
                PacketData& pkt = partial.packets[header.seqNum];

                if(pkt.values.empty()) {
                    pkt.last = header.flags&0x01;
                    pkt.sender = peer;
                    pkt.timeReceived = rxTime;
                    pkt.values.swap(rxBuf);

                    if(pkt.last && partial.lastSeq<0) {
                        partial.lastSeq = header.seqNum;
                    }

                    // are we done yet?
                    if(partial.lastSeq>=0 && partial.packets.size()-1u==size_t(partial.lastSeq)) {
                        // should be...
                        uint16_t expect = 0u;
                        bool match = true;

                        for(PartialSequence::packets_t::const_iterator it=partial.packets.begin(), end=partial.packets.end();
                            match && it!=end; ++it, expect++)
                        {
                            match &= it->first==expect;
                        }

                        if(match) {
                            // yup.
                            seq.complete.swap(partial.packets);
                            seq.timeReceived = rxTime;
                            seq.timeBase = header.timebase;

                            size_t ntotal=0u;

                            for(PartialSequence::packets_t::const_iterator it=seq.complete.begin(), end=seq.complete.end();
                                it!=end; ++it)
                            {
                                ntotal += it->second.values.size();
                            }

                            if(ntotal!=seq.ntotal) {
                                seq.ntotal = ntotal;
                                scanIoRequest(seq.totalUpdate);
                            }

                            epics::atomic::increment(driver->nComplete);
                            scanIoRequest(seq.scanUpdate);
                            LOGDRV(8, driver, "Sequence completion %02x:%08x:%04x\n", header.cmd, header.timebase, header.seqNum);

                        } else {
                            // not sure if this can actually happen, but it wouldn't be good.
                            epics::atomic::increment(driver->nError);
                            LOGDRV(1, driver, "Sequence completion logic error? %02x:%08x:%04x\n", header.cmd, header.timebase, header.seqNum);
                        }
                        partial.pushed = true;
                    }

                } else {
                    // duplicate packet?
                    epics::atomic::increment(driver->nIgnore);
                    LOGDRV(8, driver, "Ignore dup %02x:%08x:%04x\n", header.cmd, header.timebase, header.seqNum);
                }

            } else {
                epics::atomic::increment(driver->nIgnore);
                LOGDRV(8, driver, "Warning seq data after last %02x:%08x:%04x\n", header.cmd, header.timebase, header.seqNum);
            }

        }catch(std::exception& e){
            epics::atomic::increment(driver->nError);
            LOGDRV(1, driver, "Unexpected error in run() : %s\n", e.what());
            driver->onTimeout();
            epicsThreadSleep(5.0); // slow down error spam
        }
    }
}

Driver::drivers_t Driver::drivers;

Driver::Driver(const std::string& name, const osiSockAddr& peer)
    :name(name)
    ,peer(peer)
    ,log_mask(1)
    ,intimeout(true)
    ,nRX(0u)
    ,nTimeout(0u)
    ,nError(0u)
    ,nIgnore(0u)
    ,nComplete(0u)
{
    lastRx.secPastEpoch = 0;
    lastRx.nsec = 0;

    Socket::ShowAddr addr(peer);
    peerName = addr.buf;
}

Driver::~Driver() {}

void Driver::onTimeout()
{
    ::epics::atomic::increment(nTimeout);

    for(sequences_t::iterator it = sequences.begin(), end = sequences.end();
        it!=end; ++it)
    {
        CompleteSequence& seq = it->second;

        seq.timeReceived.secPastEpoch = seq.timeReceived.nsec = 0;
        seq.timeBase = 0;
        seq.complete.clear();
        seq.partials.clear();
        scanIoRequest(seq.scanUpdate);
    }
}

#include <epicsExport.h>

extern "C" {
epicsExportAddress(int,acmRxTimeoutMS);
epicsExportAddress(int,acmReconstructTimeoutMS);
}
