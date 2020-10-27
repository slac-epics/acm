
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

void Driver::run()
{
    LOGDRV(2, this, "Worker starting\n");
    PacketData::values_t rxBuf;

    Guard G(lock);

    epicsTimeStamp now;
    epicsTimeGetCurrent(&now);

    while(running) {
        try {
            for(endpoints_t::iterator it=endpoints.begin(), end=endpoints.end();
                it!=end; ++it)
            {
                double age = epicsTimeDiffInSeconds(&now, &it->lastRx);

                if(age > acmRxTimeoutMS/1000.0) {

                    epicsUInt32 msg = 0u;

                    if(!it->intimeout) {
                        LOGDRV(2, this, "Sending registration packet to %s\n", it->peerName.c_str());
                    }

                    long ret = sendto(sock.sock, &msg, sizeof(msg), 0, &it->peer.sa, sizeof(it->peer.ia));
                    if(ret!=sizeof (msg)) {
                        int err = SOCKERRNO;
                        LOGDRV(1, this, "Unable to register with %s. %d\n", it->peerName.c_str(), err);
                    }

                    it->intimeout = true;
                }
            }

            if(!intimeout) {
                double age = epicsTimeDiffInSeconds(&now, &lastRx);

                if(age > acmRxTimeoutMS/1000.0) {
                    intimeout = true;
                    onTimeout();
                }
            }

            // check for time'd out, or obsolete, sequences.
            for(Driver::sequences_t::iterator it_seq=sequences.begin();
                it_seq!=sequences.end(); ++it_seq)
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
                            LOGDRV(8, this, "Timeout %02x:%08x age=%f\n", it_seq->first, cur->first, age);
                            ::epics::atomic::increment(nTimeout);
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
            DriverEndpoint *ep = 0;
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

                testCycle.signal();

                // actually I/O
                ssize_t ret = recvmsg(sock.sock, &msg, 0);
                int err = SOCKERRNO;
                epicsTimeGetCurrent(&now);
                if(!running) {
                    break;

                } else if(ret<0) {

                    if(err==EAGAIN) {
                        // timeout
                        LOGDRV(1, this, "RX Timeout %s\n", peerName.c_str());

                    } else {
                        ::epics::atomic::increment(nError);
                        LOGDRV(1, this, "Error Rx : (%d, %d) %s\n", unsigned(ret), err, strerror(err));
                        epicsThreadSleep(5.0); // slow down error spam
                    }
                    continue; // will test sequence timeout above
                }
                LOGDRV(2, this, "Worker recvmsg() -> %d\n", int(ret));

                for(endpoints_t::iterator it=endpoints.begin(), end=endpoints.end();
                    it!=end; ++it)
                {
                    if(peer.sa.sa_family==AF_INET &&
                            peer.ia.sin_addr.s_addr==it->peer.ia.sin_addr.s_addr &&
                            peer.ia.sin_port==it->peer.ia.sin_port)
                    {
                        ep = &*it;
                    }
                }
                if(!ep)
                {
                    // ignore traffic from unknown peer
                    ::epics::atomic::increment(nIgnore);
                    continue;
                }
                ::epics::atomic::increment(nRX);

                if(msg.msg_flags&MSG_TRUNC) {
                    ::epics::atomic::increment(nError);
                    LOGDRV(1, this, "Error: truncated msg %u\n", ntohs(peer.ia.sin_port));
                    continue;
                } else if(ret<8) {
                    ::epics::atomic::increment(nError);
                    LOGDRV(1, this, "Error: runt msg %u\n", ntohs(peer.ia.sin_port));
                    continue;
                }

                rxBuf.resize((size_t(ret)-sizeof(header))/4u);
                for(size_t i=0; i<rxBuf.size(); i++)
                    rxBuf[i] = ntohl(rxBuf[i]);

                if(msg.msg_flags&MSG_CTRUNC) {
                    LOGDRV(1, this, "Warning: truncated cmsg %u\n", ntohs(peer.ia.sin_port));
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
                LOGDRV(4, this, "RX timestamp%u %08x:%08x\n", rxTimeSrc, unsigned(rxTime.secPastEpoch), unsigned(rxTime.nsec));

                // finally time to process RX buffer

                header.seqNum = ntohs(header.seqNum);
                header.timebase = ntohl(header.timebase);
                LOGDRV(8, this, "RX %02x:%08x:%04x\n", header.cmd, header.timebase, header.seqNum);
            }
            // locked again

            assert(ep);

            ep->lastRx = now;
            ep->intimeout = false;

            if(rxTimeSrc!=0 && header.seqNum==0 && tbhist.size()<1024u) {
                if(tbhist.empty()) {
                    tbhist.push_back(std::make_pair(0, 0.0));

                } else {
                    double dtb = double(header.timebase) - double(lastTimebase);
                    if(dtb <= 0.0) // roleover
                        dtb += 0x100000000;

                    tbhist.push_back(std::make_pair(dtb,
                                                            epicsTimeDiffInSeconds(&rxTime, &lastRx)));
                }

                lastTimebase = header.timebase;
                lastRx = rxTime;
            }

            intimeout = false;

            // insert this packet into a sequence
            CompleteSequence& seq = sequences[header.cmd];

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
                epics::atomic::increment(nIgnore);
                LOGDRV(8, this, "Ignore dup/late/invalid %02x:%08x:%04x\n", header.cmd, header.timebase, header.seqNum);

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

                            epics::atomic::increment(nComplete);
                            scanIoRequest(seq.scanUpdate);
                            LOGDRV(8, this, "Sequence completion %02x:%08x:%04x\n", header.cmd, header.timebase, header.seqNum);

                        } else {
                            // not sure if this can actually happen, but it wouldn't be good.
                            epics::atomic::increment(nError);
                            LOGDRV(1, this, "Sequence completion logic error? %02x:%08x:%04x\n", header.cmd, header.timebase, header.seqNum);
                        }
                        partial.pushed = true;
                    }

                } else {
                    // duplicate packet?
                    epics::atomic::increment(nIgnore);
                    LOGDRV(8, this, "Ignore dup %02x:%08x:%04x\n", header.cmd, header.timebase, header.seqNum);
                }

            } else {
                epics::atomic::increment(nIgnore);
                LOGDRV(8, this, "Warning seq data after last %02x:%08x:%04x\n", header.cmd, header.timebase, header.seqNum);
            }

        }catch(std::exception& e){
            epics::atomic::increment(nError);
            LOGDRV(1, this, "Unexpected error in run() : %s\n", e.what());
            onTimeout();
            epicsThreadSleep(5.0); // slow down error spam
        }
    }
}

Driver::drivers_t Driver::drivers;

Driver::Driver(const std::string& name)
    :name(name)
    ,log_mask(1)
    ,running(true)
    ,sock(AF_INET, SOCK_DGRAM)
    ,intimeout(true)
    ,nRX(0u)
    ,nTimeout(0u)
    ,nTimeoutGbl(0u)
    ,nError(0u)
    ,nIgnore(0u)
    ,nComplete(0u)
{
    lastRx.secPastEpoch = 0;
    lastRx.nsec = 0;
    lastTimebase = 0u;

    // setup RX timeout
    {
        unsigned long long timeout = acmRxTimeoutMS*1000; // us
        timeval timo;
        timo.tv_sec = timeout/1000000;
        timo.tv_usec = timeout%1000000;

        int ret = setsockopt(sock.sock, SOL_SOCKET, SO_RCVTIMEO, &timo, sizeof(timo));
        if(ret) {
            int err = SOCKERRNO;
            LOGDRV(1, this, "Warning: %s Unable to set RX timeout to %d ms : (%d) %s\n",
                   peerName.c_str(), acmRxTimeoutMS,
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
            LOGDRV(1, this, "Warning: %s Unable to enable RX time capture : (%d) %s\n",
                   peerName.c_str(),
                   err, strerror(err));
        }
    }

    worker.reset(new epicsThread(*this,
                                 std::string(SB()<<"ACM "<<name<<"/"<<peerName).c_str(),
                                 epicsThreadGetStackSize(epicsThreadStackBig),
                                 epicsThreadPriorityHigh
                                 ));
    // worker started later through initHook
}

Driver::~Driver()
{
    {
        Guard G(lock);
        running = false;
    }
    sock.close();
    worker->exitWait();
}

void Driver::onTimeout()
{
    LOGDRV(1, this, "Timeout");
    ::epics::atomic::increment(nTimeoutGbl);

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

    tbhist.clear();
}

#include <epicsExport.h>

extern "C" {
epicsExportAddress(int,acmRxTimeoutMS);
epicsExportAddress(int,acmReconstructTimeoutMS);
}
