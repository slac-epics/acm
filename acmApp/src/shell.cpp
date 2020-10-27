
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
              const char* peers,
              const char* unused)
{
    if(!name || !peers || unused) {
        printf("Usage: acmSetup(\"instanceName\", \"peerIP:port# ...\")\n");
        return 1;
    } else if(started) {
        fprintf(stderr, "Error: can't call after iocInit\n");
        return 1;
    } else if(Driver::drivers.find(name)!=Driver::drivers.end()) {
        fprintf(stderr, "Error: duplicate instance name not allowed.\n");
        return 1;
    }

    util::auto_ptr<Driver> driver(new Driver(name, peers));
    printf("Creating ACM '%s' for %s\n", name, driver->peers.c_str());

    std::istringstream strm(peers);
    while(strm.good()) {
        std::string iface;
        strm>>iface;

        if(strm.bad()) {
            fprintf(stderr, "Error: unable to parse host:port in : \"%s\"\n", peers);
            return 1;
        }

        DriverEndpoint ep;
        if(aToIPAddr(iface.c_str(), 0, &ep.peer.ia) || ep.peer.ia.sin_family!=AF_INET) {
            fprintf(stderr, "Error: Unable to parse interface host/IP: \"%s\"\n", iface.c_str());
            return 1;
        }

        Socket::ShowAddr show(ep.peer);
        ep.peerName = show.buf;

        printf("  Peer %s\n", ep.peerName.c_str());
        driver->endpoints.push_back(ep);
    }

    Driver::drivers[driver->name] = driver.get();
    driver.release();
    return 0;
}

namespace {

void acmExit(void *unused)
{
    // provoke workers to stop
    for(Driver::drivers_t::iterator it=Driver::drivers.begin(), end=Driver::drivers.end();
        it!=end; ++it)
    {
        {
            Guard G(it->second->lock);
            it->second->running = false;
        }
        it->second->worker->exitWait();
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
            it->second->worker->start();
        }
    }
}

long acmReport(int lvl)
{
    for(Driver::drivers_t::iterator it = Driver::drivers.begin(), end = Driver::drivers.end();
        it!=end; ++it)
    {
        Driver* drv = it->second;
        printf("ACM: \"%s\" peers: \"%s\" RX=%u Cpl=%u TMO=%u ERR=%u IGN=%u\n",
               drv->name.c_str(), drv->peers.c_str(),
               ::epics::atomic::get(drv->nRX),
               ::epics::atomic::get(drv->nComplete),
               ::epics::atomic::get(drv->nTimeout),
               ::epics::atomic::get(drv->nError),
               ::epics::atomic::get(drv->nIgnore));
        if(lvl<=0)
            continue;

        Guard G(drv->lock);

        printf("  intimeout=%c\n",
               drv->intimeout ? 'Y' : 'N');
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
