#include "tab/timetable.h"

#include <map>
#include <set>

#include <pvxs/log.h>

#include <epicsStdio.h>

DEFINE_LOGGER(LOG, "timetable");

using pvxs::TypeCode;

namespace tabulator {

// TODO: we are being overly strict here, enforcing the *order* of the columns
// This can be relaxed

#define COLSPEC(MEMBER,TYPE,STR)\
    const std::string MEMBER ## _COL(STR);\
    const std::string MEMBER ## _LABEL(STR);\
    const nt::NTTable::ColumnSpec MEMBER { TYPE, MEMBER ## _COL, MEMBER ## _LABEL }

COLSPEC(TimeTable::SECONDS_PAST_EPOCH, TypeCode::UInt32A, "secondsPastEpoch");
COLSPEC(TimeTable::NANOSECONDS,        TypeCode::UInt32A, "nanoseconds");
COLSPEC(TimeTable::PULSE_ID,           TypeCode::UInt64A, "pulseId");
static const size_t NUM_TIME_COLS = 3;

COLSPEC(TimeTableScalar::VALUE,        TypeCode::Float64A, "value");
COLSPEC(TimeTableScalar::UTAG,         TypeCode::UInt64A,  "utag");
COLSPEC(TimeTableScalar::ALARM_SEV,    TypeCode::UInt16A,  "severity");
COLSPEC(TimeTableScalar::ALARM_COND,   TypeCode::UInt16A,  "condition");
COLSPEC(TimeTableScalar::ALARM_MSG,    TypeCode::StringA,  "message");

COLSPEC(TimeTableStat::VAL,            TypeCode::Float64A, "VAL");
COLSPEC(TimeTableStat::NUM_SAMP,       TypeCode::UInt32A,  "CNT");
COLSPEC(TimeTableStat::MIN,            TypeCode::Float64A, "MIN");
COLSPEC(TimeTableStat::MAX,            TypeCode::Float64A, "MAX");
COLSPEC(TimeTableStat::MEAN,           TypeCode::Float64A, "AVG");
COLSPEC(TimeTableStat::RMS,            TypeCode::Float64A, "RMS");
#undef COLSPEC

static std::vector<nt::NTTable::ColumnSpec> from_data_columns(const std::vector<nt::NTTable::ColumnSpec> & data_columns) {
    std::vector<nt::NTTable::ColumnSpec> specs {
        TimeTable::SECONDS_PAST_EPOCH, TimeTable::NANOSECONDS, TimeTable::PULSE_ID
    };
    specs.insert(specs.end(), data_columns.begin(), data_columns.end());
    return specs;
}

TimeTable::TimeTable(const std::vector<nt::NTTable::ColumnSpec> & data_columns)
: columns(from_data_columns(data_columns)), time_columns(columns.begin(), columns.begin() + NUM_TIME_COLS),
  data_columns(columns.begin() + NUM_TIME_COLS, columns.end()), nttable(columns.begin(), columns.end())
{}

static std::vector<nt::NTTable::ColumnSpec> from_value(const pvxs::Value & value) {
    static const std::map<size_t, std::string> EXPECTED_COLS {
        {0u, TimeTable::SECONDS_PAST_EPOCH_COL},
        {1u, TimeTable::NANOSECONDS_COL},
        {2u, TimeTable::PULSE_ID_COL},
    };

    auto & labels_field = value[nt::NTTable::LABELS_FIELD];
    auto & columns_field = value[nt::NTTable::COLUMNS_FIELD];

    if (!labels_field.valid())
        throw std::runtime_error(std::string("Expected the field '") + nt::NTTable::LABELS_FIELD + "' to be valid");

    if (!columns_field.valid())
        throw std::runtime_error(std::string("Expected the field '") + nt::NTTable::COLUMNS_FIELD + "' to be valid");

    const auto & labels = labels_field.as<pvxs::shared_array<const std::string>>();
    size_t ncolumns = columns_field.nmembers();

    if (labels.size() != ncolumns) {
        char message[1024];
        epicsSnprintf(message, sizeof(message),
            "There are %lu lables and %lu columns, they were expected to be the same",
            labels.size(), ncolumns);
        throw std::runtime_error(message);
    }

    std::vector<nt::NTTable::ColumnSpec> specs;

    auto columns_it = columns_field.ichildren();
    size_t idx = 0;
    for (auto it = columns_it.begin(); it != columns_it.end(); ++it, ++idx) {
        std::string name(columns_field.nameOf(*it));

        auto expected = EXPECTED_COLS.find(idx);
        if (expected != EXPECTED_COLS.end() && (*expected).second != name) {
            char message[1024];
            epicsSnprintf(message, sizeof(message), "Expected column named '%s' at index %lu, but found '%s'",
                (*expected).second.c_str(), idx, name.c_str());
            throw std::runtime_error(message);
        }

        specs.emplace_back((*it).type(), name, labels[idx]);
    }

    return specs;
}

TimeTable::TimeTable(const pvxs::Value & value)
: columns(from_value(value)), time_columns(columns.begin(), columns.begin() + NUM_TIME_COLS),
  data_columns(columns.begin() + NUM_TIME_COLS, columns.end()), nttable(columns.begin(), columns.end())
{}

bool TimeTable::is_valid(const pvxs::Value & value) const {
    // Must be a valid overall value
    if (!value.valid())
        return false;

    // Must have at least "labels" and "value"
    auto & vlabels_field = value[nt::NTTable::LABELS_FIELD];
    auto & vcolumns_field = value[nt::NTTable::COLUMNS_FIELD];

    if (!vlabels_field.valid()) {
        log_warn_printf(LOG, "is_valid: expected field '%s' to be valid\n",
            nt::NTTable::LABELS_FIELD.c_str()
        );
        return false;
    }

    if(!vcolumns_field.valid()) {
        log_warn_printf( LOG, "is_valid: expected field '%s' to be valid\n",
            nt::NTTable::COLUMNS_FIELD.c_str()
        );
        return false;
    }

    const auto & vlabels = vlabels_field.as<pvxs::shared_array<const std::string>>();

    // Must have the expected number of labels
    if (vlabels.size() != columns.size()) {
        log_warn_printf(LOG, "is_valid: expected %lu labels, but got %lu instead\n",
            columns.size(), vlabels.size()
        );
        return false;
    }

    // Must have the expected number of columns
    if (vcolumns_field.nmembers() != columns.size()) {
        log_warn_printf(LOG, "is_valid: expected %lu columns, but got %lu instead\n",
            columns.size(), vlabels.size()
        );
        return false;
    }

    // Column length -> list of column names with the same column length
    std::set<size_t> vcolumn_lengths;

    // Column name and type and label must match, in order (overly strict for now)
    auto vcolumns_it = vcolumns_field.ichildren();
    size_t idx = 0;
    for (auto it = vcolumns_it.begin(); it != vcolumns_it.end(); ++it, ++idx) {
        auto & vlabel = vlabels[idx];
        if (vlabel != columns[idx].label) {
            log_warn_printf(LOG, "is_valid: expected label at index %lu to be '%s', but it is '%s' instead\n",
                idx, columns[idx].label.c_str(), vlabel.c_str()
            );
            return false;
        }

        auto & vcolumn_name = vcolumns_field.nameOf(*it);
        if (vcolumn_name != columns[idx].name) {
            log_warn_printf(LOG, "is_valid: expected column at index %lu to be '%s', but it is '%s' instead\n",
                idx, columns[idx].name.c_str(), vcolumn_name.c_str()
            );
            return false;
        }

        pvxs::TypeCode vcolumn_type = (*it).type();
        if (vcolumn_type != columns[idx].type_code) {
            log_warn_printf(LOG, "is_valid: expected column at index %lu to have type '%s', but it has type '%s' instead\n",
                idx, vcolumn_type.name(), columns[idx].type_code.name()
            );
            return false;
        }

        auto vcolumn_value = (*it).as<pvxs::shared_array<const void>>();
        vcolumn_lengths.insert(vcolumn_value.size());
    }

    // All columns must have the same length
    if (vcolumn_lengths.size() > 1) {
        log_warn_printf(LOG, "is_valid: expected all columns to have the same number of rows, but there are %lu different lengths\n",
            vcolumn_lengths.size()
        );
        return false;
    }

    return true;
}

TimeTableValue TimeTable::create() const {
    return TimeTableValue(*this, nttable.create());
}

TimeTableValue TimeTable::wrap(pvxs::Value value, bool validate) const {
    if (validate && !is_valid(value))
        throw std::runtime_error("Value is of incompatible type");

    return TimeTableValue(*this, value);
}


static std::vector<nt::NTTable::ColumnSpec> from_columns_config(TimeTableScalar::Config config) {
    std::vector<nt::NTTable::ColumnSpec> cols {
        TimeTableScalar::VALUE,
    };

    if(config.utag)          cols.push_back(TimeTableScalar::UTAG);
    if(config.alarm_sev)     cols.push_back(TimeTableScalar::ALARM_SEV);
    if(config.alarm_cond)    cols.push_back(TimeTableScalar::ALARM_COND);
    if(config.alarm_message) cols.push_back(TimeTableScalar::ALARM_MSG);

    return cols;
}

TimeTableScalar::TimeTableScalar(Config config)
: TimeTable(from_columns_config(config)),
  config(config)
{}

TimeTableStat::TimeTableStat()
: TimeTable({ VAL, NUM_SAMP, MIN, MAX, MEAN, RMS })
{}

} // namespace tabulator

