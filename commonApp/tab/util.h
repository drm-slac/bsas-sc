#ifndef TAB_UTIL_H
#define TAB_UTIL_H

#include <epicsTime.h>
#include <epicsStdio.h>
#include <epicsAssert.h>

#include <pvxs/data.h>

#include "nttable.h"

namespace tabulator {
namespace util {
namespace ts {

const epicsUInt32 NSEC_PER_SEC = 1000000000u;
const epicsUInt32 USEC_PER_SEC = 1000000u;
const epicsUInt32 MSEC_PER_SEC = 1000u;
const epicsUInt32 NSEC_PER_USEC = 1000u;

inline std::string show(const epicsTimeStamp & ts) {
    char buf[256];
    epicsSnprintf(buf, sizeof(buf), "%u.%09u", ts.secPastEpoch, ts.nsec);
    return std::string(buf);
}

inline const epicsTimeStamp & min(const epicsTimeStamp & left, const epicsTimeStamp & right) {
    return epicsTimeLessThan(&left, &right) ? left : right;
}

inline const epicsTimeStamp & max(const epicsTimeStamp & left, const epicsTimeStamp & right) {
    return epicsTimeGreaterThan(&left, &right) ? left : right;
}

inline void align_usec(epicsTimeStamp & ts, epicsUInt32 alignment_usec) {
    ts.nsec -= ts.nsec % (NSEC_PER_USEC*alignment_usec);
}

inline epicsTimeStamp aligned_usec(const epicsTimeStamp & ts, epicsUInt32 alignment_usec) {
    epicsTimeStamp aligned_ts = ts;
    align_usec(aligned_ts, alignment_usec);
    return aligned_ts;
}

inline void add_nsec(epicsTimeStamp & ts, epicsUInt32 nsec) {
    epicsUInt64 new_nsec = static_cast<epicsUInt64>(ts.nsec) + static_cast<epicsUInt64>(nsec);

    ts.nsec = new_nsec % NSEC_PER_SEC;
    ts.secPastEpoch += new_nsec / NSEC_PER_SEC;
}

inline void add_usec(epicsTimeStamp & ts, epicsUInt32 usec) {
    add_nsec(ts, usec*NSEC_PER_USEC);
}

}}} // namespace tabulator::util::ts

#endif