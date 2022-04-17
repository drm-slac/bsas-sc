#include "writer.h"

#include <exception>
#include <iostream>
#include <set>
#include <vector>

#include <pvxs/log.h>

#include <epicsTime.h>

#include <highfive/H5File.hpp>

DEFINE_LOGGER(LOG, "writer");

static const char *META_GROUP = "/meta";
static const char *META_PVNAMES = "pvnames";
static const char *META_COLUMN_PREFIXES = "column_prefixes";
static const char *META_LABELS = "labels";
static const char *META_COLUMNS = "columns";
static const char *META_TYPES = "pvxs_types";

static const char *DATA_GROUP = "/data";

namespace H5 = HighFive;

namespace tabulator {

// Assumption: input type is array
static H5::DataType pvxs_to_h5_type(pvxs::TypeCode t) {
    switch(t.code) {
        #define CASE(PT,HT) case pvxs::TypeCode::PT: return H5::create_datatype<HT>()
        CASE(Bool,     bool);
        CASE(BoolA,    bool);
        CASE(Int8,     int8_t);
        CASE(Int16,    int16_t);
        CASE(Int32,    int32_t);
        CASE(Int64,    int64_t);
        CASE(UInt8,    uint8_t);
        CASE(UInt16,   uint16_t);
        CASE(UInt32,   uint32_t);
        CASE(UInt64,   uint64_t);
        CASE(Int8A,    int8_t);
        CASE(Int16A,   int16_t);
        CASE(Int32A,   int32_t);
        CASE(Int64A,   int64_t);
        CASE(UInt8A,   uint8_t);
        CASE(UInt16A,  uint16_t);
        CASE(UInt32A,  uint32_t);
        CASE(UInt64A,  uint64_t);
        CASE(Float32,  float);
        CASE(Float64,  double);
        CASE(Float32A, float);
        CASE(Float64A, double);
        CASE(String,   std::string);
        CASE(StringA,  std::string);
        #undef CASE

        default:
            throw "Can't map pvxs type to hdf5 type";
    }
}

void Writer::build_file_structure(size_t chunk_size) {
    epicsTimeStamp start, end;
    epicsTimeGetCurrent(&start);

    H5::DataSetCreateProps props;
    props.add(H5::Chunking({chunk_size}));

    log_debug_printf(LOG, "Building file structure with chunk_size=%lu\n", chunk_size);

    // Groups to hold metadata and data
    auto meta_group = file_->createGroup(META_GROUP);
    log_debug_printf(LOG, "  Created metadata group %s\n", meta_group.getPath().c_str());

    auto data_group = file_->createGroup(DATA_GROUP);
    log_debug_printf(LOG, "  Created data group %s\n", data_group.getPath().c_str());

    // Metadata
    std::set<std::string> pvnames_set;          // Set of seen PV names
    std::vector<std::string> pvnames;           // PV names, in order (e.g. ["SIM:STAT:0", "SIM:STAT:1", ...])
    std::vector<std::string> column_prefixes;   // Column prefixes, in order (e.g. ["pv0", "pv1", ...])
    std::vector<std::string> columns;           // Columns (e.g. ["pv0_min", "pv0_max", "pv0_std", ...])
    std::vector<std::string> labels;            // Labels (e.g. ["SIM:STAT:0 min", "SIM:STAT:0 max", ...])
    std::vector<uint8_t> types;                 // PVXS types for each column

    for (auto c : type_->columns) {
        columns.push_back(c.name);
        labels.push_back(c.label);
        types.push_back(c.type_code.code);
    }

    for (auto c : type_->time_columns) {
        datasets_.emplace(
            c.name,
            data_group.createDataSet(
                c.name,
                H5::DataSpace({0}, {H5::DataSpace::UNLIMITED}),
                pvxs_to_h5_type(c.type_code),
                props
            )
        );
    }

    for (auto c : type_->data_columns) {
        std::string pvname, column_prefix, column_suffix;

        TimeTable::pvname_parts(c.label, &pvname, NULL);
        TimeTable::colname_parts(c.name, &column_prefix, &column_suffix);

        if (pvnames_set.find(pvname) == pvnames_set.end()) {
            pvnames.push_back(pvname);
            pvnames_set.insert(pvname);
            column_prefixes.push_back(column_prefix);

            data_group.createGroup(column_prefix);
        }

        auto group = data_group.getGroup(column_prefix);

        datasets_.emplace(
            c.name,

            group.createDataSet(
                column_suffix,
                H5::DataSpace({0}, {H5::DataSpace::UNLIMITED}),
                pvxs_to_h5_type(c.type_code),
                props
            )
        );
    }

    // Fill meta datasets
    meta_group.createDataSet(META_PVNAMES, pvnames);
    meta_group.createDataSet(META_COLUMN_PREFIXES, column_prefixes);
    meta_group.createDataSet(META_COLUMNS, columns);
    meta_group.createDataSet(META_LABELS, labels);
    meta_group.createDataSet(META_TYPES, types);

    epicsTimeGetCurrent(&end);
    log_debug_printf(LOG, "Built file structure in %.3f sec\n", epicsTimeDiffInSeconds(&end, &start));
}

Writer::Writer(const std::string & path)
:type_(nullptr), file_(new H5::File(path, H5F_ACC_EXCL)) {
    log_debug_printf(LOG, "Writing to file '%s'\n", path.c_str());
}

template<typename T>
static void write_dataset(H5::DataSet & dataset, const TimeTableValue & value, const std::string & colname) {
    auto data = value.get_column_as<T>(colname);
    size_t len = data.size();

    auto dims = dataset.getDimensions();
    dims[0] += len;
    dataset.resize(dims);
    dataset
        .select({dims[0] - len}, {len})
        .write_raw(data.dataPtr().get());
}

void Writer::write(pvxs::Value value) {
    if (!type_) {
        log_debug_printf(LOG, "First update, extracting type%s\n", "");
        type_.reset(new TimeTable(value));

        // Set chunk size to the size of this first update, based on the length of the secondsPastEpoch column
        size_t chunk_size = type_->wrap(value, false).get_column_as<TimeTable::SECONDS_PAST_EPOCH_T>(
            TimeTable::SECONDS_PAST_EPOCH_COL
        ).size();

        build_file_structure(chunk_size);
    }

    epicsTimeStamp start, end;
    epicsTimeGetCurrent(&start);

    auto tvalue = type_->wrap(value, true);

    for (auto c : type_->columns) {
        auto ds = datasets_.find(c.name);
        if (ds == datasets_.end())
            throw std::logic_error(std::string("Can't find dataset: ") + c.name);

        switch (c.type_code.code) {
            #define CASE(PT, T) case pvxs::TypeCode::PT: write_dataset<T>(ds->second, tvalue, c.name); break
            CASE(BoolA,    bool);
            CASE(Int8A,    int8_t);
            CASE(Int16A,   int16_t);
            CASE(Int32A,   int32_t);
            CASE(Int64A,   int64_t);
            CASE(UInt8A,   uint8_t);
            CASE(UInt16A,  uint16_t);
            CASE(UInt32A,  uint32_t);
            CASE(UInt64A,  uint64_t);
            CASE(Float32A, float);
            CASE(Float64A, double);
            CASE(StringA,  std::string);
            #undef CASE

            default:
                throw std::runtime_error(std::string("Unexpected type") + c.type_code.name());
        }
    }

    file_->flush();
    epicsTimeGetCurrent(&end);
    log_debug_printf(LOG, "Wrote update to file in %.3f sec\n", epicsTimeDiffInSeconds(&end, &start));
}

} // namespace tabulator