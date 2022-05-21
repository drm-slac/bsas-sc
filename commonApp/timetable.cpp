#include "tab/timetable.h"

using pvxs::TypeCode;

namespace tabulator {

#define COLSPEC(MEMBER,TYPE,STR)\
    const std::string MEMBER ## _COL(STR);\
    const std::string MEMBER ## _LABEL(STR);\
    const nt::NTTable::ColumnSpec MEMBER { TYPE, MEMBER ## _COL, MEMBER ## _LABEL }

COLSPEC(TimeTable::SECONDS_PAST_EPOCH, TypeCode::UInt32A, "secondsPastEpoch");
COLSPEC(TimeTable::NANOSECONDS,        TypeCode::UInt32A, "nanoseconds");

COLSPEC(TimeTableScalar::VALUE,        TypeCode::Float64A, "value");
COLSPEC(TimeTableScalar::UTAG,         TypeCode::UInt64A,  "utag");
COLSPEC(TimeTableScalar::ALARM_SEV,    TypeCode::UInt16A,  "severity");
COLSPEC(TimeTableScalar::ALARM_COND,   TypeCode::UInt16A,  "condition");
COLSPEC(TimeTableScalar::ALARM_MSG,    TypeCode::StringA,  "message");

COLSPEC(TimeTableStat::VAL,            TypeCode::Float64A, "VAL");
COLSPEC(TimeTableStat::NUM_SAMP,       TypeCode::Float64A, "CNT");
COLSPEC(TimeTableStat::MIN,            TypeCode::Float64A, "MIN");
COLSPEC(TimeTableStat::MAX,            TypeCode::Float64A, "MAX");
COLSPEC(TimeTableStat::MEAN,           TypeCode::Float64A, "AVG");
COLSPEC(TimeTableStat::RMS,            TypeCode::Float64A, "RMS");
#undef COLSPEC

static std::vector<nt::NTTable::ColumnSpec> from_data_columns(const std::vector<nt::NTTable::ColumnSpec> & data_columns) {
    std::vector<nt::NTTable::ColumnSpec> specs {
        TimeTable::SECONDS_PAST_EPOCH, TimeTable::NANOSECONDS
    };
    specs.insert(specs.end(), data_columns.begin(), data_columns.end());
    return specs;
}

TimeTable::TimeTable(const std::vector<nt::NTTable::ColumnSpec> & data_columns)
: columns(from_data_columns(data_columns)), time_columns(columns.begin(), columns.begin() + 2),
  data_columns(columns.begin() + 2, columns.end()), nttable(columns.begin(), columns.end())
{}

static std::vector<nt::NTTable::ColumnSpec> from_value(const pvxs::Value & value) {
    auto & labels_field = value[nt::NTTable::LABELS_FIELD];
    auto & columns_field = value[nt::NTTable::COLUMNS_FIELD];

    assert(labels_field.valid());
    assert(columns_field.valid());

    const auto & labels = labels_field.as<pvxs::shared_array<const std::string>>();
    size_t ncolumns = columns_field.nmembers();

    assert(labels.size() == ncolumns);
    assert(ncolumns >= 2);

    std::vector<nt::NTTable::ColumnSpec> specs;

    auto columns_it = columns_field.ichildren();
    size_t idx = 0;
    for (auto it = columns_it.begin(); it != columns_it.end(); ++it, ++idx) {
        if (idx == 0)
            assert(columns_field.nameOf(*it) == TimeTable::SECONDS_PAST_EPOCH_COL);

        if (idx == 1)
            assert(columns_field.nameOf(*it) == TimeTable::NANOSECONDS_COL);

        specs.emplace_back((*it).type(), columns_field.nameOf(*it), labels[idx]);
    }

    return specs;
}

TimeTable::TimeTable(const pvxs::Value & value)
: columns(from_value(value)), time_columns(columns.begin(), columns.begin() + 2),
  data_columns(columns.begin() + 2, columns.end()), nttable(columns.begin(), columns.end())
{}

bool TimeTable::is_valid(const pvxs::Value & value) const {
    // Must be a valid overall value
    if (!value.valid())
        return false;

    // Must have at least "labels" and "value"
    auto & vlabels_field = value[nt::NTTable::LABELS_FIELD];
    auto & vcolumns_field = value[nt::NTTable::COLUMNS_FIELD];

    if (!vlabels_field.valid() || !vcolumns_field.valid())
        return false;

    const auto & vlabels = vlabels_field.as<pvxs::shared_array<const std::string>>();

    // Must have the expected number of labels
    if (vlabels.size() != columns.size())
        return false;

    // Must have the expected number of columns
    if (vcolumns_field.nmembers() != columns.size())
        return false;

    // Column name and type and label must match, in order
    auto vcolumns_it = vcolumns_field.ichildren();
    size_t idx = 0;
    for (auto it = vcolumns_it.begin(); it != vcolumns_it.end(); ++it, ++idx) {
        if (vlabels[idx] != columns[idx].label)
            return false;

        if (vcolumns_field.nameOf(*it) != columns[idx].name)
            return false;

        if ((*it).type() != columns[idx].type_code)
            return false;
    }

    return true;
}

TimeTableValue TimeTable::create() const {
    return TimeTableValue(*this, nttable.create());
}

TimeTableValue TimeTable::wrap(pvxs::Value value, bool validate) const {
    if (validate && !is_valid(value))
        throw "Value is of incompatible type";

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

