
#include <epicsExit.h>
#include <iocsh.h>
#include <drvSup.h>
#include <initHooks.h>
#include <epicsVersion.h>
#include <epicsStdio.h> // redirects stdout/stderr

#include "acm_drv.h"

static
bool started = false;

int acmSetup(const char* name,
              const char* deviceName,
              const char* bindNames)
{
    if(!name || !deviceName || !bindNames) {
        printf("Usage: acmSetup(\"instanceName\", \"peerIP:port#\", \"ifaceIP:port# ...\")\n");
        return 1;
    } else if(started) {
        fprintf(stderr, "Error: can't call after iocInit\n");
        return 1;
    } else if(Driver::drivers.find(name)!=Driver::drivers.end()) {
        fprintf(stderr, "Error: duplicate instance name not allowed.\n");
        return 1;
    }

    osiSockAddr peer;
    if(aToIPAddr(deviceName, 0, &peer.ia) || peer.ia.sin_family!=AF_INET) {
        fprintf(stderr, "Error: Unable to parse device host/IP: \"%s\"\n", deviceName);
        return 1;
    }

    util::auto_ptr<Driver> driver(new Driver(name, peer));
    printf("Creating ACM '%s' for %s\n", name, driver->peerName.c_str());

    std::istringstream strm(bindNames);
    while(strm.good()) {
        std::string iface;
        strm>>iface;

        if(strm.bad()) {
            fprintf(stderr, "Error: unable to parse an iface in : \"%s\"\n", bindNames);
            return 1;
        }

        osiSockAddr ifaceAddr;
        if(aToIPAddr(iface.c_str(), 0, &ifaceAddr.ia) || ifaceAddr.ia.sin_family!=AF_INET) {
            fprintf(stderr, "Error: Unable to parse interface host/IP: \"%s\"\n", iface.c_str());
            return 1;
        }

        util::auto_ptr<DriverSock> sock(new DriverSock(driver.get(), ifaceAddr));
        driver->sockets.push_back(sock.get());
        printf("  Listen on %s\n", sock->bindName.c_str());
        sock.release();
    }

    Driver::drivers[driver->name] = driver.get();
    driver.release();
    return 0;
}

namespace {

void acmExit(void *unused)
{
    for(Driver::drivers_t::iterator it=Driver::drivers.begin(), end=Driver::drivers.end();
        it!=end; ++it)
    {
        // provoke workers to stop
        {
            Guard G(it->second->lock);

            for(Driver::sockets_t::iterator it2=it->second->sockets.begin(), end2=it->second->sockets.end();
                it2!=end2; ++it2)
            {
                (*it2)->running = false;
                (void)shutdown((*it2)->sock.sock, SHUT_RDWR); // on Linux, interrupts recvmsg()
                (*it2)->sock.close();
            }
        }
        // wait for workers to stop
        for(Driver::sockets_t::iterator it2=it->second->sockets.begin(), end2=it->second->sockets.end();
            it2!=end2; ++it2)
        {
            (*it2)->worker->exitWait();
        }
    }
}

void acmHook(initHookState hook)
{
    started = true;
    if(hook==initHookAfterIocRunning) {
        epicsAtExit(&acmExit, 0);

        for(Driver::drivers_t::iterator it=Driver::drivers.begin(), end=Driver::drivers.end();
            it!=end; ++it)
        {
            for(Driver::sockets_t::iterator it2=it->second->sockets.begin(), end2=it->second->sockets.end();
                it2!=end2; ++it2)
            {
                (*it2)->worker->start();
            }
        }
    }
}

long acmReport(int lvl)
{
    for(Driver::drivers_t::iterator it = Driver::drivers.begin(), end = Driver::drivers.end();
        it!=end; ++it)
    {
        Driver* drv = it->second;
        printf("ACM: \"%s\" peer: %s RX=%u TMO=%u ERR=%u IGN=%u\n",
               drv->name.c_str(), drv->peerName.c_str(),
               ::epics::atomic::get(drv->nRX),
               ::epics::atomic::get(drv->nTimeout),
               ::epics::atomic::get(drv->nError),
               ::epics::atomic::get(drv->nIgnore));
        if(lvl<=0)
            continue;
    }
    return 0;
}

const iocshArg acmSetupArg0 = { "name",iocshArgString};
const iocshArg acmSetupArg1 = { "peerip:port",iocshArgString};
const iocshArg acmSetupArg2 = { "iface:port ...",iocshArgString};
const iocshArg * const acmSetupArgs[3] = {&acmSetupArg0, &acmSetupArg1, &acmSetupArg2};
const iocshFuncDef acmSetupFuncDef = {
    "acmSetup",3,acmSetupArgs};
void acmSetupCall(const iocshArgBuf *args)
{
    try {
        int ret = acmSetup(args[0].sval, args[1].sval, args[2].sval);
#if EPICS_VERSION_INT >= VERSION_INT(7,0,3,1)
        iocshSetError(ret);
#else
        (void)ret;
#endif
    }catch(std::exception& e){
        fprintf(stderr, "Error: %s\n", e.what());
    }
}

void acmRegistrar()
{
    iocshRegister(&acmSetupFuncDef, &acmSetupCall);
    initHookRegister(&acmHook);
}

drvet ACM = {2, (DRVSUPFUN)&acmReport, NULL};

} // namespace

#include <epicsExport.h>

extern "C" {
epicsExportRegistrar(acmRegistrar);
epicsExportAddress(drvet, ACM);
}
