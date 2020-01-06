
#include <string.h>

#include <epicsUnitTest.h>
#include <dbUnitTest.h>
#include <testMain.h>
#include <epicsExit.h>
#include <dbAccess.h>
#include <iocsh.h>
#include <dbDefs.h>

#include "acm_drv.h"

namespace {

#define testIntEq(LHS, RHS) {{ int lhs = (LHS), rhs = (RHS); testOk(lhs==rhs, #LHS "(%d) == " #RHS "(%d)", lhs, rhs); }}

struct TestDevice {
    Socket sock;
    osiSockAddr bound, ioc;

    static TestDevice* instance;

    TestDevice()
        :sock(AF_INET, SOCK_DGRAM, 0)
    {
        memset(&bound, 0, sizeof (bound));
        bound.ia.sin_family = AF_INET;
        bound.ia.sin_addr.s_addr = htonl(INADDR_LOOPBACK);

        if(bind(sock.sock, &bound.sa, sizeof(bound))!=0) {
            testAbort("Unable to bind test device %d", SOCKERRNO);
        }

        osiSocklen_t len = sizeof(bound);
        if(getsockname(sock.sock, &bound.sa, &len)!=0) {
            testAbort("Unable to find bound address for test device %d", SOCKERRNO);
        }

        instance = this;
    }
    ~TestDevice()
    {
        instance = NULL;
    }

    void tx(uint8_t cmd, uint16_t seq, bool last, uint32_t tb, size_t nvals, const uint32_t* vals)
    {
        struct {
            uint8_t cmd, flags;
            uint16_t seq;
            uint32_t tb;
        } header;
        STATIC_ASSERT(sizeof(header)==8);

        header.cmd = cmd;
        header.flags = last ? 1 : 0;
        header.seq = htons(seq);
        header.tb = htonl(tb);

        iovec vec[2];
        vec[0].iov_base = &header;
        vec[0].iov_len = sizeof(header);
        vec[1].iov_base = (void*)&vals[0];
        vec[1].iov_len = 4*nvals;

        msghdr msg;
        memset(&msg, 0 , sizeof(msg));

        msg.msg_name = &ioc.sa;
        msg.msg_namelen = sizeof(ioc);
        msg.msg_iov = vec;
        msg.msg_iovlen = NELEMENTS(vec);

        ssize_t ret = sendmsg(sock.sock, &msg, 0);
        if(ret<0 || size_t(ret)!=vec[0].iov_len+vec[1].iov_len)
            testAbort("Send error %d", SOCKERRNO);
    }
};

TestDevice* TestDevice::instance;

Driver* dut;

void testRegisters()
{
    testDiag("testRegisters()");

    (void)dut->testCycle.tryWait();

    int nRX = epics::atomic::get(dut->nRX),
        nComplete = epics::atomic::get(dut->nComplete);

    testDiag("Send register data");
    const uint32_t vals[] = {htonl(0xdeadbeef), htonl(0x01020304)};
    TestDevice::instance->tx(ACMType::RegData, 0, true, 0x1234, NELEMENTS(vals), vals);

    testOk1(dut->testCycle.wait(5.0));

    {
        Guard G(dut->lock);

        testIntEq(nRX+1, epics::atomic::get(dut->nRX));
        testIntEq(nComplete+1, epics::atomic::get(dut->nComplete));

        CompleteSequence& seq = dut->sequences[ACMType::RegData];
        testOk(seq.timeReceived.secPastEpoch!=0, "sec=%u", unsigned(seq.timeReceived.secPastEpoch));
        testOk(seq.timeBase==0x1234, "tb=%u", unsigned(seq.timeBase));

        if(testOk1(seq.complete.size()==1u && seq.complete[0].values.size()==2u)) {
            testIntEq(seq.complete[0].values[0], 0xdeadbeef);
            testIntEq(seq.complete[0].values[1], 0x01020304);
        }
    }

}

}

extern "C"
void acmTest_registerRecordDeviceDriver(struct dbBase *);

MAIN(testreconstruct)
{
    testPlan(9);
    testdbPrepare();

    testdbReadDatabase("acmTest.dbd", NULL, NULL);
    acmTest_registerRecordDeviceDriver(pdbbase);
    // dynamic port assignments to be self-contained
    acmSetup("DUT", "127.0.0.1", "127.0.0.1:0");

    dut = Driver::drivers["DUT"];
    if(dut->name!="DUT" || dut->sockets.empty())
        testAbort("No DUT");

    TestDevice device;
    // fixup with actual port assignments
    device.ioc = dut->sockets[0]->bindAddr;
    testDiag("IOC socket @%d", ntohs(device.ioc.ia.sin_port));

    testdbReadDatabase("acm_base.db", "../../db", "P=TST:,DEV=DUT,DEBUG=0xf");

    testIocInitOk();

    testDiag("Wait for loop to recvmsg()");
    {
        Guard G(dut->lock);
        testOk1(dut->testCycle.wait(5.0));
    }

    testRegisters();

    testIocShutdownOk();

    testdbCleanup();
    epicsExit(testDone());
}
