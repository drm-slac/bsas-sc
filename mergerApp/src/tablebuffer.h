#ifndef TAB_TABLEBUFFER_H
#define TAB_TABLEBUFFER_H

#include <map>
#include <deque>
#include <string>
#include <functional>

#include <epicsTime.h>

#include <tab/nttable.h>
#include <tab/timetable.h>
#include <pvxs/data.h>

namespace tabulator {

struct TimeSpan {

    static const epicsUInt32 MAX_U32;
    static const epicsTimeStamp MAX_TS;
    static const epicsTimeStamp MIN_TS;

    bool valid;
    epicsTimeStamp start;
    epicsTimeStamp end;

    TimeSpan();
    TimeSpan(const epicsTimeStamp & start, const epicsTimeStamp & end);
    void update(const epicsTimeStamp & start, const epicsTimeStamp & end);
    void reset();
    double span_sec() const;
};

/* TableBuffer
 *
 * Buffers a series of given `pvxs::Value` objects in a FIFO manner.
 * Keeps track of earliest and latest sample timestamp. The `pvxs::Value`
 * is assumed to be an NTTable with a particular format: the first two
 * columns are named "secondsPastEpoch" and "nanoseconds", both containing
 * uint32_t values that compose the parts of an `epicsTimeStamp` for each
 * row. Also, timestamps within an NTTable and from older and newer NTTables
 * are assumed to be strictly non-decreasing.
 * *
 */
class TableBuffer {

public:
    typedef std::function<
        bool( /* Returns "done": true to stop iterating early, false to continue iterating.*/
            const epicsTimeStamp & /* row timestamp */,
            const TimeTable::PULSE_ID_T, /* row pulseId */
            const std::vector<pvxs::shared_array<const void>> & /* column containers */,
            size_t /* row index within each column container */
        )
    > ConsumeFunc;

private:
    std::unique_ptr<TimeTable> type_;
    epicsTimeStamp start_ts_;
    epicsTimeStamp end_ts_;
    std::deque<TimeTableValue> buffer_;
    size_t inner_idx_;

    void update_timestamps();

    std::pair<size_t, size_t> consume_each_row_inner(ConsumeFunc f);

public:
    /* Constructs an empty TableBuffer */
    TableBuffer();

    /* A TableBuffer is initialized if at least one sample has been
     * pushed into it (so we know its type)
     */
    bool initialized() const;

    /* A TableBuffer is empty if it holds no samples */
    bool empty() const;

    /* Returns a list of NTTable::ColumnSpec that can be used to construct
     * a new NTTable. The columns in this list correspond to the columns
     * of the stored `pvxs::Value`s.
     */
    const std::vector<nt::NTTable::ColumnSpec> & columns() const;

    /* Returns a list of NTTable::ColumnSpec that can be used to construct
     * a new NTTable. The columns in this list correspond to the columns
     * of the stored `pvxs::Value`s, skipping the first two (secondsPastEpoch
     * and nanoseconds).
     */
    const std::vector<nt::NTTable::ColumnSpec> & data_columns() const;

    /* Returns the time span that this buffer covers */
    TimeSpan time_span() const;

    /* Convenience method that allocates a list of `pvxs::shared_array`s that
     * correspond to the "data" columns in the buffer (non-timestamp columns).
     * Each allocated `shared_array` will have `num_rows` elements.
     */
    std::vector<pvxs::shared_array<void>> allocate_containers(size_t num_rows) const;

    /* Pushes a new `pvxs::Value` into the buffer. It will be appended at the
     * end of the buffer (queue).
     */
    void push(pvxs::Value value);

    /* Executes the given function `f` on each row, starting at the oldest.
     * Keeps calling `f` until it returns `true` or all rows are consumed.
     * At the end, removes all rows consumed from the buffer.
     */
    void consume_each_row(ConsumeFunc f);

    /* Extracts the time difference between rows into the given `diffs` map.
     * `diffs` is a map with the keys being the time difference (in ns)
     * between rows and the values being the number of times that
     * the time difference appears.
     * Returns the number of processed differences.
     */
    size_t extract_time_diffs(std::map<epicsInt64, size_t> & diffs) const;
};

}

#endif