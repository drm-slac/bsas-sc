#include "taligntable.h"
#include <tab/util.h>

#include <vector>
#include <set>
#include <cmath>
#include <exception>

#include <epicsTime.h>
#include <epicsMutex.h>
#include <epicsVersion.h>

#include <pvxs/log.h>
#include <pvxs/sharedArray.h>

#if EPICS_VERSION_INT < VERSION_INT(7, 0, 6, 1)
static epicsInt64 epicsTimeDiffInNS (const epicsTimeStamp *pLeft, const epicsTimeStamp *pRight)
{
    static const epicsUInt32 nSecPerSec = 1000000000u;
    epicsInt64 delta = epicsInt64(pLeft->secPastEpoch) - pRight->secPastEpoch;
    delta *= nSecPerSec;
    delta += epicsInt64(pLeft->nsec) - pRight->nsec;
    return delta;
}
#endif

DEFINE_LOGGER(LOG, "taligntable");

typedef epicsGuard<epicsMutex> Guard;

namespace tabulator {

static nt::NTTable::ColumnSpec prefixed_colspec(size_t idx, const std::string & pvname, const nt::NTTable::ColumnSpec & spec) {
    char colprefix_buf[256] = {};
    epicsSnprintf(colprefix_buf, sizeof(colprefix_buf), "pv%05lu", idx);
    std::string colprefix(colprefix_buf);

    return {
        spec.type_code, colprefix + "_" + spec.name, pvname + " " + spec.label
    };
}

TimeBounds::TimeBounds() {
    reset();
}

void TimeBounds::reset() {
    valid = false;
    earliest_start = TimeSpan::MAX_TS;
    earliest_end = TimeSpan::MAX_TS;
    latest_start = TimeSpan::MIN_TS;
    latest_end = TimeSpan::MIN_TS;
}

void TimeAlignedTable::initialize() {
    Guard G(lock_);

    if (type_)
        return;

    static const nt::NTTable::ColumnSpec VALID {
        pvxs::TypeCode::BoolA, "valid", "valid"
    };

    std::vector<nt::NTTable::ColumnSpec> data_columns;
    size_t idx = 0;
    for (const auto & buf : buffers_) {
        if (!buf.initialized())
            return;

        const auto & pvname = pvlist_[idx];

        data_columns.emplace_back(prefixed_colspec(idx, pvname, VALID));

        for (const auto & spec : buf.data_columns())
            data_columns.emplace_back(prefixed_colspec(idx, pvname, spec));

        ++idx;
    }

    type_ = std::make_unique<TimeTable>(data_columns);
}

TimeAlignedTable::TimeAlignedTable(const std::vector<std::string> & pvlist, epicsUInt32 alignment_usec)
: pvlist_(pvlist), alignment_usec_(alignment_usec), lock_(), buffers_(pvlist.size()), type_()
{
    log_debug_printf(LOG, "TimeAlignedTable(%lu PVs, align=%u us)\n", pvlist.size(), alignment_usec);
    assert(alignment_usec > 0);
}

bool TimeAlignedTable::initialized() const {
    Guard G(lock_);
    return type_.get() != 0;
}

TimeBounds TimeAlignedTable::get_timebounds() const
{
    Guard G(lock_);

    // Collect timespans
    std::vector<TimeSpan> timespans;

    for (const auto & buf : buffers_)
        timespans.emplace_back(buf.time_span());

    return TimeBounds(timespans.begin(), timespans.end());
}

void TimeAlignedTable::push(size_t idx, pvxs::Value value) {
    log_debug_printf(LOG, "push(buf_idx=%lu, value.valid=%d)\n", idx, value.valid());

    assert(idx < buffers_.size());
    Guard G(lock_);

    // Push the value to the correct buffer
    TableBuffer & buffer = buffers_[idx];
    buffer.push(value);

    // Create type if we don't have it already
    initialize();
}

static void copy_row(
    std::vector<pvxs::shared_array<void>> & dest,
    size_t dest_idx,
    const std::vector<pvxs::shared_array<const void>> src,
    size_t src_idx
) {
    assert(dest.size() == src.size());

    auto dest_it = dest.begin();
    auto src_it = src.begin();

    for(; dest_it != dest.end() && src_it != src.end(); ++dest_it, ++src_it) {
        switch (dest_it->original_type()) {
            #define CASE_COPY_ELEM(AT, T)\
                case pvxs::ArrayType::AT:\
                    *(static_cast<T*>(dest_it->data()) + dest_idx) = *(static_cast<const T*>(src_it->data()) + src_idx); break

            CASE_COPY_ELEM(Bool,    bool);
            CASE_COPY_ELEM(Int8,    int8_t);
            CASE_COPY_ELEM(Int16,   int16_t);
            CASE_COPY_ELEM(Int32,   int32_t);
            CASE_COPY_ELEM(Int64,   int64_t);
            CASE_COPY_ELEM(UInt8,   uint8_t);
            CASE_COPY_ELEM(UInt16,  uint16_t);
            CASE_COPY_ELEM(UInt32,  uint32_t);
            CASE_COPY_ELEM(UInt64,  uint64_t);
            CASE_COPY_ELEM(Float32, float);
            CASE_COPY_ELEM(Float64, double);
            CASE_COPY_ELEM(String,  std::string);

            #undef CASE_COPY_ELEM

            default:
                throw ""; // TODO: better exception
        }
    }
}

static void set_empty_row(std::vector<pvxs::shared_array<void>> & dest, size_t idx) {
    for (auto dest_it = dest.begin(); dest_it != dest.end(); ++dest_it) {
        switch (dest_it->original_type()) {
            #define CASE_SET_ELEM(AT, T)\
                case pvxs::ArrayType::AT: *(static_cast<T*>(dest_it->data()) + idx) = {}; break

            CASE_SET_ELEM(Bool,    bool);
            CASE_SET_ELEM(Int8,    int8_t);
            CASE_SET_ELEM(Int16,   int16_t);
            CASE_SET_ELEM(Int32,   int32_t);
            CASE_SET_ELEM(Int64,   int64_t);
            CASE_SET_ELEM(UInt8,   uint8_t);
            CASE_SET_ELEM(UInt16,  uint16_t);
            CASE_SET_ELEM(UInt32,  uint32_t);
            CASE_SET_ELEM(UInt64,  uint64_t);
            CASE_SET_ELEM(Float32, float);
            CASE_SET_ELEM(Float64, double);
            CASE_SET_ELEM(String,  std::string);

            #undef CASE_SET_ELEM

            default:
                throw ""; // TODO: better exception
        }
    }
}

pvxs::Value TimeAlignedTable::extract(const epicsTimeStamp & start, const epicsTimeStamp & end)
{
    Guard G(lock_);

    // Sanity check
    assert(epicsTimeGreaterThanEqual(&end, &start));

    epicsTimeStamp start_ts = util::ts::aligned_usec(start, alignment_usec_);
    epicsTimeStamp end_ts = util::ts::aligned_usec(end, alignment_usec_);

    epicsUInt64 timespan_nsec = epicsTimeDiffInNS(&end_ts, &start_ts);
    size_t num_rows = timespan_nsec / (alignment_usec_ * util::ts::NSEC_PER_USEC);

    log_debug_printf(LOG, "extract(start=%u.%u, end=%u.%u) --> %lu rows\n",
        start.secPastEpoch, start.nsec, end.secPastEpoch, end.nsec, num_rows);

    // Generate output columns and Value
    std::vector<pvxs::shared_array<void>> output_columns;
    TimeTableValue output_value(type_->create());

    // Generate timestamps
    {
        pvxs::shared_array<epicsUInt32> secondsPastEpoch(num_rows);
        pvxs::shared_array<epicsUInt32> nanoseconds(num_rows);

        epicsTimeStamp ts = start_ts;
        for (size_t i = 0; i < num_rows; ++i) {
            secondsPastEpoch[i] = ts.secPastEpoch;
            nanoseconds[i] = ts.nsec;
            util::ts::add_usec(ts, alignment_usec_);
        }

        // Add timestamps to output columns
        output_columns.emplace_back(secondsPastEpoch.castTo<void>());
        output_columns.emplace_back(nanoseconds.castTo<void>());
    }

    log_debug_printf(LOG, "extract() - generated %lu timestamp columns\n", output_columns.size());

    // Extract values from each of our buffers
    for (auto & buf : buffers_) {
        // These will hold the final extracted values
        pvxs::shared_array<bool> valid(num_rows);
        auto column_values = buf.allocate_containers(num_rows);

        // Iterate over each result row
        size_t row = 0;
        buf.consume_each_row([this, num_rows, &row, start_ts, end_ts, &valid, &column_values](
            const epicsTimeStamp & buf_ts,
            const std::vector<pvxs::shared_array<const void>> & buf_cols,
            size_t buf_idx
        ) -> bool {
            epicsTimeStamp row_ts = start_ts;
            util::ts::add_usec(row_ts, row*alignment_usec_);
            epicsTimeStamp buf_row_ts = util::ts::aligned_usec(buf_ts, alignment_usec_);

            // Check if we are past the end, stop if we are
            if (row >= num_rows || epicsTimeGreaterThan(&buf_row_ts, &end_ts))
                return true;

            // If timestamps are equal, values are valid
            if (epicsTimeEqual(&buf_row_ts, &row_ts)) {
                valid[row] = true;
                copy_row(column_values, row, buf_cols, buf_idx);
                ++row;
                return false;
            }

            // If the buffer is ahead of us (in time), it means there are missing values.
            // Skip this iteration (by filling in invalid values)
            if (epicsTimeGreaterThan(&buf_row_ts, &row_ts)) {
                valid[row] = false;
                set_empty_row(column_values, row);
                return false;
            }

            // If we got here, it means the buffer is behind us (in time).
            // Skip this iteration (do nothing)
            ++row;
            return false;
        });

        // The remaining rows are invalid
        for (; row < num_rows; ++row) {
            valid[row] = false;
            set_empty_row(column_values, row);
        }

        // We built all columns from this buffer, save them
        output_columns.emplace_back(valid.castTo<void>());
        output_columns.insert(output_columns.end(), column_values.begin(), column_values.end());

        log_debug_printf(LOG, "extract() - generated %lu data columns\n", column_values.size() + 1);
    }

    // Paranoia
    assert(type_->columns.size() == output_columns.size());

    log_debug_printf(LOG, "extract() - generated %lu total columns\n", output_columns.size());

    // Set columns in Value
    // zip()
    auto cs = type_->columns.begin();
    auto oc = output_columns.begin();

    for (; cs != type_->columns.end(); ++cs, ++oc)
        output_value.set_column(cs->name, (*oc).freeze());

    log_debug_printf(LOG, "extract() - generated complete value%s\n", "");

    return output_value.get();
}

pvxs::Value TimeAlignedTable::create() const {
    Guard G(lock_);
    if (initialized())
        return type_->create().get();

    return {};
}

void TimeAlignedTable::dump() const {
    Guard G(lock_);

    /*printf("TimeAlignedTable [ncols=%lu align=%u us start=%u.%u earliest_end=%u.%u latest_end=%u.%u]\n",
        num_columns_, alignment_usec_,
        time_span_.start.secPastEpoch, time_span_.start.nsec,
        time_span_.earliest_end.secPastEpoch, time_span_.earliest_end.nsec,
        time_span_.latest_end.secPastEpoch, time_span_.latest_end.nsec
        );*/

    /*for (size_t col = 0; col < num_columns_; ++col) {
        size_t s = timestamps_[col].size();
        if (s > 0) {
            epicsTimeStamp first_ts = timestamps_[col][0];
            epicsTimeStamp last_ts = timestamps_[col][s-1];
            double first_val = values_[col][0];
            double last_val = values_[col][s-1];

            double ts_diff = epicsTimeDiffInSeconds(&last_ts, &first_ts);

            printf("[%lu] (%.3f s) ts=%u.%u v=%.3f   ts=%u.%u v=%.3f\n",
                col, ts_diff,
                first_ts.secPastEpoch, first_ts.nsec, first_val,
                last_ts.secPastEpoch, last_ts.nsec, last_val);

        } else {
            printf("[%lu] <empty>\n", col);
        }
    }*/
}


}