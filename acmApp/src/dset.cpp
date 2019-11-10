
#include <sstream>

#include <longinRecord.h>
#include <aaiRecord.h>

#include <epicsAtomic.h>
#include <epicsStdlib.h>
#include <dbAccess.h>
#include <dbStaticLib.h>
#include <devSup.h>
#include <dbLink.h>
#include <recGbl.h>
#include <alarm.h>

#include "acm_drv.h"

namespace {

long long asInt(const char* s)
{
    long long ret;
    if(epicsParseLLong(s, &ret, 0, 0))
        throw std::runtime_error(SB()<<"Not an int '"<<s<<"'");
    return ret;
}

DBLINK* xdbGetDevLink(dbCommon* prec)
{
    DBLINK *plink = 0;
    DBENTRY entry;
    dbInitEntryFromRecord(prec, &entry);
    if(dbFindField(&entry, "INP")==0 || dbFindField(&entry, "OUT")==0) {
        plink = (DBLINK*)entry.pfield;
    }
    dbFinishEntry(&entry);
    return plink;
}

struct linkInfo {
    Driver *driver;

    unsigned cmd;
    unsigned offset;
};

linkInfo* parseLink(dbCommon *prec)
{
    DBLINK *plink = xdbGetDevLink(prec);
    assert(plink && plink->type==INST_IO);

    std::istringstream strm(plink->value.instio.string);

    util::auto_ptr<linkInfo> info(new linkInfo);

    std::string name;
    strm>>name;
    for(std::string ent; std::getline(strm, ent, ' '); ) {
        size_t sep = ent.find_first_of('=');
        if(sep==std::string::npos) {
            strm.setstate(std::ios::failbit);
            break;
        }

        std::string key = ent.substr(0, sep),
                    value = ent.substr(sep+1);

        if(key=="offset") {
            info->offset = asInt(value.c_str());

        } else if(key=="cmd") {
            info->cmd = asInt(value.c_str());

        } else {
            errlogPrintf("%s : unknown link key '%s'\n", prec->name, key.c_str());
        }
    }

    if(strm.fail() || !strm.eof()) {
        throw std::runtime_error(SB()<<prec->name<<" Error parsing : "<<plink->value.instio.string);
    }

    {
        Driver::drivers_t::iterator it = Driver::drivers.find(name);
        if(it==Driver::drivers.end()) {
            throw std::runtime_error(SB()<<"No such ACM Driver '"<<name<<"'");
        }
        info->driver = it->second;
    }

    return info.release();
}

long cmd_update(int detach, struct dbCommon *prec, IOSCANPVT* pscan)
{
    linkInfo *info = static_cast<linkInfo*>(prec->dpvt);
    if(info) {
        *pscan = info->driver->sequences[info->cmd].scanUpdate;
    }
    return 0;
}

struct dset6 {
    long number;
    long (*report)(int lvl);
    long (*init)(int after);
    long (*init_record)(dbCommon *prec);
    long (*get_ioint_info)(int detach, struct dbCommon *prec, IOSCANPVT* pscan);
    long (*readwrite)(dbCommon *prec);
};

long init_record(dbCommon *prec)
{
    try {
        prec->dpvt = parseLink(prec);
    }catch(std::exception& e){
        errlogPrintf("%s : init_record error : %s\n", prec->name, e.what());
    }
    return 0;
}

long read_counter(dbCommon *prec)
{
    longinRecord *pli = reinterpret_cast<longinRecord*>(prec);
    linkInfo *info = static_cast<linkInfo*>(prec->dpvt);
    if(!info) return -1;

    // no locking needed
    switch(info->offset) {
    case 0: pli->val = epics::atomic::get(info->driver->nRX); break;
    case 1: pli->val = epics::atomic::get(info->driver->nTimeout); break;
    case 2: pli->val = epics::atomic::get(info->driver->nError); break;
    case 3: pli->val = epics::atomic::get(info->driver->nIgnore); break;
    default: pli->val = -1;
    }
    return 0;
}

dset6 devACMLiCounter = {6, 0, 0, &init_record, 0, &read_counter};

long read_regval(dbCommon *prec)
{
    longinRecord *pli = reinterpret_cast<longinRecord*>(prec);
    linkInfo *info = static_cast<linkInfo*>(prec->dpvt);
    if(!info) return -1;
    try {
        Guard G(info->driver->lock);
        CompleteSequence& seq = info->driver->sequences[info->cmd];
        if(prec->tse==epicsTimeEventDeviceTime) {
            prec->time = seq.timeReceived;
        }
        pli->val = seq.at(info->offset);
    }catch(std::range_error&){
        (void)recGblSetSevr(prec, READ_ALARM, INVALID_ALARM);
    }catch(std::exception& e){
        (void)recGblSetSevr(prec, READ_ALARM, INVALID_ALARM);
        errlogPrintf("%s : read : %s\n", prec->name, e.what());
    }
    return -2;
}

dset6 devACMLiRegVal = {6, 0, 0, &init_record, &cmd_update, &read_regval};

} // namespace

#include <epicsExport.h>

extern "C" {
epicsExportAddress(dset, devACMLiCounter);
epicsExportAddress(dset, devACMLiRegVal);
}
