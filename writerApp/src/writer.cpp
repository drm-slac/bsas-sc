#include "writer.h"

#include <exception>
#include <iostream>
#include <set>
#include <vector>

#include <pvxs/log.h>

#include <highfive/H5File.hpp>

DEFINE_LOGGER(LOG, "writer");

static const char *META_GROUP = "/meta";
static const char *META_PVNAMES = "pvnames";
static const char *META_COLUMN_PREFIXES = "column_prefixes";
static const char *META_LABELS = "labels";
static const char *META_COLUMNS = "columns";
static const char *META_TYPES = "types";

static const char *DATA_GROUP = "/data";

namespace H5 = HighFive;

namespace tabulator {

// Assumption: input type is array
static hid_t pvxs_to_h5_type(pvxs::TypeCode t) {
    switch(t.code) {
        #define CASE(PT,HT) case pvxs::TypeCode::PT: return H5Tcopy(HT)
        CASE(Bool,     H5T_NATIVE_UCHAR);
        CASE(BoolA,    H5T_NATIVE_UCHAR);
        CASE(Int8,     H5T_NATIVE_SCHAR);
        CASE(Int16,    H5T_NATIVE_SHORT);
        CASE(Int32,    H5T_NATIVE_INT);
        CASE(Int64,    H5T_NATIVE_LONG);
        CASE(UInt8,    H5T_NATIVE_UCHAR);
        CASE(UInt16,   H5T_NATIVE_USHORT);
        CASE(UInt32,   H5T_NATIVE_UINT);
        CASE(UInt64,   H5T_NATIVE_ULONG);
        CASE(Int8A,    H5T_NATIVE_SCHAR);
        CASE(Int16A,   H5T_NATIVE_SHORT);
        CASE(Int32A,   H5T_NATIVE_INT);
        CASE(Int64A,   H5T_NATIVE_LONG);
        CASE(UInt8A,   H5T_NATIVE_UCHAR);
        CASE(UInt16A,  H5T_NATIVE_USHORT);
        CASE(UInt32A,  H5T_NATIVE_UINT);
        CASE(UInt64A,  H5T_NATIVE_ULONG);
        CASE(Float32,  H5T_NATIVE_FLOAT);
        CASE(Float64,  H5T_NATIVE_DOUBLE);
        CASE(Float32A, H5T_NATIVE_FLOAT);
        CASE(Float64A, H5T_NATIVE_DOUBLE);
        CASE(String,   H5T_C_S1);
        CASE(StringA,  H5T_C_S1);
        #undef CASE

        default:
            throw "Can't map pvxs type to hdf5 type";
    }
}

static std::unique_ptr<TimeTable> extract_type_from_file(H5::File & file) {
    return nullptr;
}

// Use https://github.com/BlueBrain/HighFive instead of dealing with the C interface
static void build_file_structure(H5::File & file, const TimeTable & type) {
    std::set<std::string> pvnames_set;
    std::vector<std::string> pvnames;
    std::vector<std::string> column_prefixes;

    // Extract PV Names and Column prefixes
    for (auto c : type.data_columns) {

        auto i = c.label.find(' ');
        if (i == std::string::npos)
            throw "Invalid label name (must contain a space)";

        std::string pvname = c.label.substr(0, i);

        if (pvnames_set.find(pvname) != pvnames_set.end())
            continue;

        i = c.name.find('_');
        if (i == std::string::npos)
            throw "Invalid column name (must contain an underscore)";

        std::string column_prefix(c.name.substr(0, i));

        pvnames_set.insert(pvname);
        pvnames.push_back(pvname);
        column_prefixes.push_back(column_prefix);
    }

    // Extract column definitions
    std::vector<std::string> columns;
    for (auto c : type.columns)
        columns.push_back(c.name);

    std::vector<std::string> labels;
    for (auto c : type.columns)
        labels.push_back(c.label);

    std::vector<uint8_t> types;
    for (auto c: type.columns)
        types.push_back(c.type_code.code);

    // Fill datasets
    auto meta_group = file.createGroup(META_GROUP);
    meta_group.createDataSet(META_PVNAMES, pvnames);
    meta_group.createDataSet(META_COLUMN_PREFIXES, column_prefixes);
    meta_group.createDataSet(META_COLUMNS, columns);
    meta_group.createDataSet(META_LABELS, labels);
    meta_group.createDataSet(META_TYPES, types);

    // Create data datasets
    auto data_group = file.createGroup(DATA_GROUP);

    for (auto c : type.time_columns)
        data_group.createDataSet(c.name, H5::DataSpace({0}, {H5::DataSpace::UNLIMITED}));
}

Writer::Writer(const std::string & path)
:type_(nullptr), file_(nullptr) {
    log_debug_printf(LOG, "Writing to %s\n", path.c_str());

    // Turn off error handling
    //H5Eset_auto(0, NULL, NULL);

    // https://confluence.hdfgroup.org/display/HDF5/HDF5+1.10+CPP+Reference+Manual
    // https://confluence.hdfgroup.org/display/HDF5/HDF5+User%27s+Guide?preview=/61276353/61276355/Users_Guide.pdf

    errno = 0;
    try {
        file_.reset(new H5::File(path, H5F_ACC_EXCL));
    } catch (H5::FileException & ex) {
        if (errno != EEXIST)
            throw;

        log_info_printf(LOG, "File %s already exists, appending to it\n", path.c_str());

        file_.reset(new H5::File(path));
        type_ = extract_type_from_file(*file_);
    }
}

void Writer::write(pvxs::Value value) {
    if (!type_) {
        log_debug_printf(LOG, "First update, extracting type%s\n", "");
        type_.reset(new TimeTable(value));
        std::cout << type_->nttable.build() << std::endl;
        build_file_structure(*file_, *type_);
    }

    auto tvalue = type_->wrap(value, true);

    log_debug_printf(LOG, "Got valid value%s\n", "");
}

} // namespace tabulator