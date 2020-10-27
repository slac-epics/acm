
#include <string.h>

#include <sstream>

#include <epicsVersion.h>

#include <aiRecord.h>
#include <longoutRecord.h>
#include <mbboDirectRecord.h>
#include <longinRecord.h>
#include <aaiRecord.h>
#include <stringinRecord.h>
#include <biRecord.h>
#include <mbbiRecord.h>
#include <menuFtype.h>

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

#ifndef VERSION_INT
#  define VERSION_INT(V,R,M,P) ( ((V)<<24) | ((R)<<16) | ((M)<<8) | (P))
#  define EPICS_VERSION_INT VERSION_INT(EPICS_VERSION, EPICS_REVISION, EPICS_MODIFICATION, EPICS_PATCH_LEVEL)
#endif

namespace {

long long asInt(const char* s)
{
    long long ret;
    if(epicsParseLLong(s, &ret, 0, 0))
        throw std::runtime_error(SB()<<"Not an int '"<<s<<"'");
    return ret;
}

double asDouble(const char* s)
{
    double ret;
    if(epicsParseDouble(s, &ret, 0))
        throw std::runtime_error(SB()<<"Not a double '"<<s<<"'");
    return ret;
}

#if EPICS_VERSION_INT<VERSION_INT(3,16,1,0)
static
void dbInitEntryFromRecord(dbCommon *prec, DBENTRY *pdbentry)
{
    dbInitEntry(pdbbase, pdbentry);
    long status = dbFindRecord(pdbentry, prec->name);
    assert(status==0);
}
#endif

#if EPICS_VERSION_INT<VERSION_INT(7,0,2,0)
static
DBLINK* dbGetDevLink(dbCommon* prec)
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
#endif

struct linkInfo {
    Driver *driver;

    // packet ID
    unsigned cmd;
    // dword offset in packet body
    unsigned offset;
    // bit mask
    epicsUInt32 mask;
    // after mask, right bit shift
    unsigned shift;
    // scaling
    double slope, intercept;

    linkInfo()
        :cmd(0)
        ,offset(0)
        ,mask(0xffffffff)
        ,shift(0u)
        ,slope(1.0)
        ,intercept(0.0)
    {}
};

linkInfo* parseLink(dbCommon *prec)
{
    DBLINK *plink = dbGetDevLink(prec);
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

        } else if(key=="mask") {
            info->mask = asInt(value.c_str());

        } else if(key=="shift") {
            info->shift = asInt(value.c_str());

        } else if(key=="slope") {
            info->slope = asDouble(value.c_str());

        } else if(key=="intercept") {
            info->intercept = asDouble(value.c_str());

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

long cmd_ntotal(int detach, struct dbCommon *prec, IOSCANPVT* pscan)
{
    linkInfo *info = static_cast<linkInfo*>(prec->dpvt);
    if(info) {
        *pscan = info->driver->sequences[info->cmd].totalUpdate;
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
        case 1: val = drv->peers.c_str(); break;
        case 2: val = ACM_VCS; break;
        default:val = "\?\?\?"; recGblSetSevr(prec, READ_ALARM, INVALID_ALARM); break;
        }

        strncpy(prec->val, val, sizeof(prec->val));
        prec->val[sizeof(prec->val)-1] = '\0';

    }CATCH()
    return -2;
}

dset6 devACMSiInfo = {6, 0, 0, &init_record, 0, &read_info};

long read_status(dbCommon *pcommon)
{
    TRY(biRecord) {
        Guard G(drv->lock);

        prec->rval = drv->intimeout ? 0 : 1;
        return 0;

    }CATCH()
    return -2;
}

dset6 devACMBiStatus = {6, 0, 0, &init_record, 0, &read_status};

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
        case 5: prec->val = epics::atomic::get(drv->nTimeoutGbl); break;
        default: prec->val = -1; recGblSetSevr(prec, READ_ALARM, INVALID_ALARM); break;
        }
        return 0;
    }CATCH()
    return -2;
}

dset6 devACMLiCounter = {6, 0, 0, &init_record, 0, &read_counter};

template<typename R, typename T, T (R::*FLD)>
long read_regval(dbCommon *pcommon)
{
    TRY(R) {
        Guard G(drv->lock);
        CompleteSequence& seq = drv->sequences[info->cmd];
        if(prec->tse==epicsTimeEventDeviceTime) {
            prec->time = seq.timeReceived;
        }
        epicsUInt32 rval = seq.at(info->offset);
        prec->*FLD = (rval&info->mask)>>info->shift;
        return 0;
    }catch(std::out_of_range&){
        (void)recGblSetSevr(prec, READ_ALARM, INVALID_ALARM);
    }CATCH()
    return -2;
}

dset6 devACMBiRegVal = {6, 0, 0, &init_record, &cmd_update, &read_regval<biRecord, epicsUInt32, &biRecord::rval>};
dset6 devACMMbbiRegVal={6, 0, 0, &init_record, &cmd_update, &read_regval<mbbiRecord, epicsUInt32, &mbbiRecord::rval>};
dset6 devACMLiRegVal = {6, 0, 0, &init_record, &cmd_update, &read_regval<longinRecord, epicsInt32, &longinRecord::val>};
dset6 devACMAiRegVal = {6, 0, 0, &init_record, &cmd_update, &read_regval<aiRecord, epicsInt32, &aiRecord::rval>};

epicsInt32 sextend(epicsUInt32 val, unsigned sbit)
{
    epicsUInt32 smask = epicsUInt32(1u)<<sbit,
                emask = ~(smask | (smask-1u)); // bits sbit+1 and higher
    if(val&smask)
        val |= emask;
    else
        val &= ~emask;
    return val;
}

long read_trace(dbCommon *pcommon)
{
    TRY(aaiRecord) {
        if(prec->ftvl!=menuFtypeDOUBLE)
            throw std::runtime_error("FTVL must be DOUBLE");

        double* arr = reinterpret_cast<double*>(prec->bptr);
        epicsUInt32 out=0, cnt=prec->nelm;

        Guard G(drv->lock);
        CompleteSequence& seq = drv->sequences[info->cmd];

        if(!seq.complete.empty()) {

            for(PartialSequence::packets_t::const_iterator it=seq.complete.begin(), end=seq.complete.end();
                it!=end && out<cnt; ++it)
            {
                const PacketData::values_t& values = it->second.values;

                for(size_t i=0, N=values.size(); i+4 <= N && out<cnt; i+=4)
                {
                    epicsInt32 val;
                    switch(info->offset) {
                    case 0: val = sextend(values[i+0]>> 0u, 15u); break;
                    case 1: val = sextend(values[i+0]>>16u, 15u); break;
                    case 2: val = sextend(values[i+1], 23u); break;
                    case 3: val = sextend(values[i+2], 23u); break;
                    case 4: val = (values[i+3]); break;
                    case 5: val = out; break; // time
                    default: val = 0xdeadbeef; break;
                    }
                    arr[out++] = double(val)*info->slope + info->intercept;
                }
            }

            prec->nord = out;

            if(prec->tse==epicsTimeEventDeviceTime) {
                prec->time = seq.timeReceived;
            }

        } else {
            recGblSetSevr(prec, UDF_ALARM, INVALID_ALARM);

            if(prec->tse==epicsTimeEventDeviceTime) {
                epicsTimeGetCurrent(&prec->time);
            }
        }

        return 0;

    }CATCH()
    return -2;
}

dset6 devACMAaiTrace = {6, 0, 0, &init_record, &cmd_update, &read_trace};
dset6 devACMAaiTimebase = {6, 0, 0, &init_record, &cmd_ntotal, &read_trace};

long read_tbhist(dbCommon *pcommon)
{
    TRY(aaiRecord) {
        if(prec->ftvl!=menuFtypeDOUBLE)
            throw std::runtime_error("FTVL must be DOUBLE");

        double* arr = reinterpret_cast<double*>(prec->bptr);
        epicsUInt32 out=0, cnt=prec->nelm;

        Guard G(drv->lock);

        for(size_t i=0, N=drv->tbhist.size(); i<N && out<cnt; i++) {
            double val;
            switch(info->offset) {
            case 0: val = drv->tbhist[i].first; break;
            case 1: val = drv->tbhist[i].second; break;
            default: val = 42.0; recGblSetSevr(prec, COMM_ALARM, INVALID_ALARM); break;
            }
            arr[out++] = val;
        }

        if(info->offset==1)
            drv->tbhist.clear(); // hack

        if(out==0)
            arr[out++] = 0.0; // CA doesn't handle empty arrays well

        prec->nord = out;

        return 0;
    }CATCH()
    return -2;
}

dset6 devACMAaiTBHist = {6, 0, 0, &init_record, 0, &read_tbhist};

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
epicsExportAddress(dset, devACMBiStatus);
epicsExportAddress(dset, devACMLiCounter);
epicsExportAddress(dset, devACMBiRegVal);
epicsExportAddress(dset, devACMMbbiRegVal);
epicsExportAddress(dset, devACMLiRegVal);
epicsExportAddress(dset, devACMAiRegVal);
epicsExportAddress(dset, devACMAaiTrace);
epicsExportAddress(dset, devACMAaiTimebase);
epicsExportAddress(dset, devACMAaiTBHist);
epicsExportAddress(dset, devACMMbboDirectLogMask);
}
