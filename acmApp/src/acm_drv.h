#ifndef ACM_DRV_H
#define ACM_DRV_H

#include <stdexcept>
#include <vector>
#include <list>
#include <string>
#include <map>

#include <osiSock.h>
#include <epicsThread.h>
#include <epicsTypes.h>
#include <epicsAtomic.h>
#include <epicsTime.h>
#include <errlog.h>
#include <dbScan.h>

#include "acm_util.h"

struct ACMType {
    enum type {
        RegData = 0x51,
        SampFault = 0xe7,
        SampInt = 0x33,
        SampExt = 0x28,
    };
    static const char* name(type c);
};

struct PacketData {
    osiSockAddr sender;
    epicsTimeStamp timeReceived;
    bool last; // Flag[0]

    typedef util::Buffer<epicsUInt32> values_t;
    values_t values;
};

/* A new PartialSequnce is added to CompleteSequence::partials
 * each time a new Timebase value is encountered, and stamped
 * with the arrival time of this first packet (may not be Seq# 0).
 * Expiration and global ordering is established from this arrival time.
 */
struct PartialSequence {
    // reception time of first packet with this cmd+timebase pair.
    // May not be seq# 0
    epicsTimeStamp firstRx;
    // -1 or the seq# of the packet with Flags[0] set (last).
    int lastSeq;
    // whether this complete/timedout sequence has been moved
    // into the timeline
    bool pushed;

    typedef std::map<uint16_t, PacketData> packets_t;
    packets_t packets;

    PartialSequence(); // sets timeoutAt
};

struct CompleteSequence {
    // time of first packet in sequence
    epicsTimeStamp timeReceived;
    uint32_t timeBase;

    typedef std::map<uint32_t, PartialSequence> partials_t;
    partials_t partials;

    // most recent complete for this packet type
    PartialSequence::packets_t complete;

    IOSCANPVT scanUpdate;

    // sum of samples in complete sequence.
    size_t ntotal;

    IOSCANPVT totalUpdate;

    CompleteSequence();

    epicsUInt32 at(size_t i) const;
};

struct Socket {
    SOCKET sock;
    Socket(int af, int type, int proto=0);
    ~Socket();
    void close();

    struct ShowAddr {
        char buf[24];
        //ShowAddr();
        explicit ShowAddr(const osiSockAddr& addr);
        //ShowAddr& operator=(const osiSockAddr& addr);
    };
};

struct Driver;

struct DriverEndpoint
{
    osiSockAddr peer;
    std::string peerName;
    epicsTimeStamp lastRx;
    bool intimeout;
    DriverEndpoint()
        :intimeout(false)
    {
        lastRx.secPastEpoch = 0;
        lastRx.nsec = 0;
    }
};

#define LOGDRV(mask, pdriver, FMT, ...) do{ if(::epics::atomic::get((pdriver)->log_mask)&(mask)) errlogPrintf("%s " FMT, (pdriver)->name.c_str(), ##__VA_ARGS__); }while(0)

struct Driver : public epicsThreadRunable
{
    typedef std::map<std::string, Driver*> drivers_t;
    static drivers_t drivers; // const after iocInit

    const std::string name;

    /** atomic
     *
     * 0x0001 - Error messages
     * 0x0002 - Worker thread activity
     * 0x0004 - timestamp capture
     * 0x0008 - Sequence reconstruction
     * 0x0010 - Raw packet RX
     */
    int log_mask;

    bool running;

    Socket sock;

    epicsTimeStamp lastRx;
    bool intimeout;

    // const after acmSetup()
    std::string peerName;

    typedef std::vector<DriverEndpoint> endpoints_t;
    endpoints_t endpoints; // only vector is const.  individual DriverSock are guarded by our lock

    // (atomic) stat counters
    int nRX, nTimeout, nTimeoutGbl, nError, nIgnore, nComplete;

    // by packet type
    typedef  std::map<uint8_t, CompleteSequence> sequences_t;
    sequences_t sequences;

    uint32_t lastTimebase;
    typedef std::vector<std::pair<uint32_t, double> > tbhist_t;
    tbhist_t tbhist;

    // all other data members guarded by lock
    mutable epicsMutex lock;

    epicsEvent testCycle;

    util::auto_ptr<epicsThread> worker;

    explicit Driver(const std::string& name);
    ~Driver();

    virtual void run() override final;
    void onTimeout();
};

extern "C"
int acmSetup(const char* name,
              const char* deviceName,
              const char* bindNames);

#endif // ACM_DRV_H
