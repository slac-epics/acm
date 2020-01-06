
#include <string.h>

#include <sstream>

#include <aiRecord.h>
#include <longoutRecord.h>
#include <mbboDirectRecord.h>
#include <longinRecord.h>
#include <aaiRecord.h>
#include <stringinRecord.h>

#include <epicsAtomic.h>
#include <epicsStdlib.h>
#include <dbAccess.h>
#include <dbStaticLib.h>
#include <devSup.h>
#include <dbLink.h>
#include <recGbl.h>
#include <alarm.h>

#include "acm_drv.h"
#include "acmVCS.h"

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

    std::string name, ent;
    strm>>name;
    while(strm.good()) {
        if(!(strm>>ent))
            break;
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
        throw std::runtime_error(SB()<<" Error parsing : "<<plink->value.instio.string);
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

#define TRY(rtype) \
    rtype *prec = reinterpret_cast<rtype*>(pcommon); \
    linkInfo *info = static_cast<linkInfo*>(prec->dpvt); \
    if(!info) return -1; \
    Driver *drv = info->driver; (void)drv; \
    try

#define CATCH() \
    catch(std::exception& e) { \
        (void)recGblSetSevr(prec, READ_ALARM, INVALID_ALARM); \
        errlogPrintf("%s : error : %s\n", prec->name, e.what()); \
    }

#define LOGREC(mask, FMT, ...) do{ if(::epics::atomic::get((drv)->log_mask)&(mask)) errlogPrintf("%s %s " FMT, prec->name, (drv)->name.c_str(), ##__VA_ARGS__); }while(0)

long init_record(dbCommon *prec)
{
    try {
        prec->dpvt = parseLink(prec);
    }catch(std::exception& e){
        errlogPrintf("%s : init_record error : %s\n", prec->name, e.what());
    }
    return 0;
}

long read_info(dbCommon *pcommon)
{
    TRY(stringinRecord) {
        const char* val;

        switch(info->offset) {
        case 0: val = drv->name.c_str(); break;
        case 1: val = drv->peerName.c_str(); break;
        case 2: val = ACM_VCS; break;
        default:val = "\?\?\?"; recGblSetSevr(prec, READ_ALARM, INVALID_ALARM); break;
        }

        strncpy(prec->val, val, sizeof(prec->val));
        prec->val[sizeof(prec->val)-1] = '\0';

    }CATCH()
    return -2;
}

dset6 devACMSiInfo = {6, 0, 0, &init_record, 0, &read_info};

long read_counter(dbCommon *pcommon)
{
    TRY(longinRecord) {
        // no locking needed
        switch(info->offset) {
        case 0: prec->val = epics::atomic::get(drv->nRX); break;
        case 1: prec->val = epics::atomic::get(drv->nTimeout); break;
        case 2: prec->val = epics::atomic::get(drv->nError); break;
        case 3: prec->val = epics::atomic::get(drv->nIgnore); break;
        case 4: prec->val = epics::atomic::get(drv->nComplete); break;
        default: prec->val = -1; recGblSetSevr(prec, READ_ALARM, INVALID_ALARM); break;
        }
        return 0;
    }CATCH()
    return -2;
}

dset6 devACMLiCounter = {6, 0, 0, &init_record, 0, &read_counter};

template<typename R, epicsInt32 (R::*FLD)>
long read_regval(dbCommon *pcommon)
{
    TRY(R) {
        Guard G(drv->lock);
        CompleteSequence& seq = drv->sequences[info->cmd];
        if(prec->tse==epicsTimeEventDeviceTime) {
            prec->time = seq.timeReceived;
        }
        prec->*FLD = seq.at(info->offset);
        return 0;
    }catch(std::out_of_range&){
        (void)recGblSetSevr(prec, READ_ALARM, INVALID_ALARM);
    }CATCH()
    return -2;
}

dset6 devACMLiRegVal = {6, 0, 0, &init_record, &cmd_update, &read_regval<longinRecord, &longinRecord::val>};
dset6 devACMAiRegVal = {6, 0, 0, &init_record, &cmd_update, &read_regval<aiRecord, &aiRecord::rval>};

long write_log_mask(dbCommon *pcommon)
{
    TRY(mbboDirectRecord) {
        // no locking needed
        epics::atomic::set(drv->log_mask, prec->val);
        LOGREC(0xffff, "Log mask set to %04x\n", prec->val);
        return 0;
    }CATCH()
    return -2;
}

dset6 devACMMbboDirectLogMask = {6, 0, 0, &init_record, 0, &write_log_mask};

} // namespace

#include <epicsExport.h>

extern "C" {
epicsExportAddress(dset, devACMSiInfo);
epicsExportAddress(dset, devACMLiCounter);
epicsExportAddress(dset, devACMLiRegVal);
epicsExportAddress(dset, devACMAiRegVal);
epicsExportAddress(dset, devACMMbboDirectLogMask);
}
