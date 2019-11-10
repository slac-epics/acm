
#include <linux/net_tstamp.h>

#include <string.h>

#include <epicsAtomic.h>
#include <dbDefs.h>

#include "acm_drv.h"

extern "C" {
int acmRxTimeoutMS = 5000;
int acmReconstructTimeoutMS = 1000;
}

PartialSequence::PartialSequence()
{
    epicsTimeGetCurrent(&timeoutAt);
    epicsTimeAddSeconds(&timeoutAt, acmReconstructTimeoutMS/1000.0);
}

CompleteSequence::CompleteSequence()
{
    scanIoInit(&scanUpdate);
}

CompleteSequence::~CompleteSequence()
{
    assert(false); // never destroyed
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
            errlogPrintf("Warning: unable to getsockname()\n");
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
            errlogPrintf("Warning: %s Unable to set RX timeout to %d ms : (%d) %s\n",
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
            errlogPrintf("Warning: %s Unable to enable RX time capture : (%d) %s\n",
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
    // never free'd
    assert(false);
}

void DriverSock::run()
{
    std::vector<uint32_t> rxBuf;

    Guard G(driver->lock);

    while(running) {

        struct {
            uint8_t cmd, flags;
            uint16_t seqNum;
            uint32_t timebase;
        } header;
        STATIC_ASSERT(sizeof(header)==8);

        osiSockAddr peer;
        epicsTimeStamp rxTime, now;
        unsigned rxTimeSrc=0;
        {
            UnGuard U(G); // unlock for I/O

            rxBuf.resize(0x10000/4u); // theoretical max for UDP

            // all sorts of fun to prepare for recvmsg()
            union {
                char buf[64]; // not sure how to choose this size.  This works...
                cmsghdr align;
            } control;

            iovec vec[2];
            vec[0].iov_base = &header;
            vec[0].iov_len = sizeof(header);
            vec[1].iov_base = &rxBuf[0];
            vec[1].iov_len = 4*rxBuf.size(); // uint32_t to bytes

            msghdr msg;
            memset(&msg, 0 , sizeof(msg));
            msg.msg_name = &peer.sa;
            msg.msg_namelen = sizeof(peer);
            msg.msg_iov = vec;
            msg.msg_iovlen = NELEMENTS(vec);
            msg.msg_control = &control;
            msg.msg_controllen = sizeof(control);

            // actually I/O
            ssize_t ret = recvmsg(sock.sock, &msg, 0);
            if(ret<0) {
                int err = SOCKERRNO;
                //::epics::atomic::increment(nTimeout);
                // TODO handle timeout
                errlogPrintf("Error Rx on %s : (%d) %s\n", bindName.c_str(), err, strerror(err));
                epicsThreadSleep(1.0); // slow down error spam
                continue;
            }

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
                errlogPrintf("Error: truncated msg %u -> %u\n", ntohs(peer.ia.sin_port), ntohs(bindAddr.ia.sin_port));
                continue;
            } else if(ret<8) {
                ::epics::atomic::increment(driver->nError);
                errlogPrintf("Error: runt msg %u -> %u\n", ntohs(peer.ia.sin_port), ntohs(bindAddr.ia.sin_port));
                continue;
            }

            if(msg.msg_flags&MSG_CTRUNC) {
                errlogPrintf("Warning: truncated cmsg %u -> %u\n", ntohs(peer.ia.sin_port), ntohs(bindAddr.ia.sin_port));
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

            epicsTimeGetCurrent(&now);
            if(rxTimeSrc==0) {
                rxTime = now;
            }

            // finally time to process RX buffer

            header.seqNum = ntohs(header.seqNum);
            header.timebase = ntohl(header.timebase);
        }
        // locked again

        if(rxTimeSrc!=0) {
            // TODO revise time mapping with rxTime and header.timebase
        }

        // insert this packet into a sequence
        CompleteSequence& seq = driver->sequences[header.cmd];
        PartialSequence& partial = seq.partials[header.timebase];
        {
            PacketData& pkt = partial.packets[header.seqNum];

            pkt.last = header.flags&0x01;
            pkt.sender = peer;
            pkt.values.swap(rxBuf);
        }

        if(epicsTimeDiffInSeconds(&now, &partial.timeoutAt)<0.0) {
            // see if this packet completes its sequence

            uint16_t next=0;
            for(PartialSequence::packets_t::iterator it = partial.packets.begin(), end = partial.packets.end();
                it!=end; ++it)
            {
                if(it->first!=next) {
                    // found a missing packet
                    break;

                } else if(it->second.last) {
                    // all done

                    ++it;
                    // [begin, it) is complete sequence.
                    // [it, end) is junk, it it exists
                    partial.packets.erase(it, partial.packets.end());

                    seq.complete.swap(partial.packets);

                    scanIoRequest(seq.scanUpdate);

                    break;

                } else {
                    // so far so good...
                    next++;
                }
            }
        }

        // check for time'd out sequences
        for(Driver::sequences_t::iterator it_seq=driver->sequences.begin();
            it_seq!=driver->sequences.end(); ++it_seq)
        {
            for(CompleteSequence::partials_t::iterator it_partial=it_seq->second.partials.begin();
                it_partial!=it_seq->second.partials.end();)
            {
                CompleteSequence::partials_t::iterator cur = it_partial++;
                if(epicsTimeDiffInSeconds(&now, &it_partial->second.timeoutAt)<0.0) {
                    ::epics::atomic::increment(driver->nTimeout);
                    it_seq->second.partials.erase(cur);
                }
            }
        }
    }
}

void DriverSock::show(unsigned int lvl) const
{}

Driver::drivers_t Driver::drivers;

Driver::Driver(const std::string& name, const osiSockAddr& peer)
    :name(name)
    ,peer(peer)
    ,nRX(0u)
    ,nTimeout(0u)
    ,nError(0u)
    ,nIgnore(0u)
{
    Socket::ShowAddr addr(peer);
    peerName = addr.buf;
}

Driver::~Driver()
{
    assert(false);
}

#include <epicsExport.h>

extern "C" {
epicsExportAddress(int,acmRxTimeoutMS);
epicsExportAddress(int,acmReconstructTimeoutMS);
}
