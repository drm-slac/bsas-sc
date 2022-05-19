#ifndef TAB_TALIGNTABLE_H
#define TAB_TALIGNTABLE_H

#include <vector>

#include <epicsTime.h>
#include <epicsMutex.h>

#include <pvxs/data.h>

#include <tab/util.h>

#include "tablebuffer.h"

namespace tabulator {

struct TimeBounds {
    bool valid;
    epicsTimeStamp earliest_start;
    epicsTimeStamp earliest_end;
    epicsTimeStamp latest_start;
    epicsTimeStamp latest_end;

    TimeBounds();

    template<typename I>
    TimeBounds(const I & begin, const I & end) {
        reset();

        if (begin == end)
            return;

        valid = true;

        for (I it = begin; it != end; ++it) {
            if (!it->valid) {
                reset();
                return;
            }

            earliest_start = util::ts::min(it->start, earliest_start);
            earliest_end   = util::ts::min(it->end,   earliest_end);
            latest_start   = util::ts::max(it->start, latest_start);
            latest_end     = util::ts::max(it->end,   latest_end);
        }
    }

    void reset();
};

class TimeAlignedTable {

private:
    std::vector<std::string> pvlist_;
    epicsUInt32 alignment_usec_;
    const std::string label_sep_;
    const std::string col_sep_;

    mutable epicsMutex lock_;
    std::vector<TableBuffer> buffers_;
    std::unique_ptr<TimeTable> type_;

    void initialize();
    nt::NTTable::ColumnSpec prefixed_colspec(size_t idx, const std::string & pvname,
        const nt::NTTable::ColumnSpec & spec);

public:
    TimeAlignedTable(const std::vector<std::string> & pvlist, epicsUInt32 alignment_usec,
        const std::string & label_sep, const std::string & col_sep);

    // Returns true if all inner buffers were initialized (got at least 1 update),
    // false otherwise
    bool initialized() const;

    TimeBounds get_timebounds() const;

    // Push a new update to one of the buffers
    void push(size_t idx, pvxs::Value value);

    // Extract a time-aligned table chunk, between start and end
    pvxs::Value extract(const epicsTimeStamp & start, const epicsTimeStamp & end);

    // Build an empty value
    pvxs::Value create() const;

    void dump() const;
};

}

#endif