#include "writer.h"

#include <exception>
#include <iostream>

#include <pvxs/log.h>

DEFINE_LOGGER(LOG, "writer");

namespace tabulator {

static std::unique_ptr<TimeTable> extract_type_from_file(hid_t file) {
    return nullptr;
}

static void build_file_structure(hid_t file, const std::unique_ptr<TimeTable> & type) {

}

Writer::Writer(const std::string & path)
:type_(nullptr) {
    log_debug_printf(LOG, "Writing to %s\n", path.c_str());

    // Turn off error handling
    H5Eset_auto(0, NULL, NULL);

    // https://confluence.hdfgroup.org/display/HDF5/HDF5+1.10+CPP+Reference+Manual
    // https://confluence.hdfgroup.org/display/HDF5/HDF5+User%27s+Guide?preview=/61276353/61276355/Users_Guide.pdf
    file_ = H5Fcreate(path.c_str(), H5F_ACC_EXCL, H5P_DEFAULT, H5P_DEFAULT);

    if (file_ == H5I_INVALID_HID && errno == EEXIST) {
        log_info_printf(LOG, "File %s already exists, appending to it\n", path.c_str());

        // Open existing file and read type from it
        file_ = H5Fopen(path.c_str(), H5F_ACC_RDWR, H5P_DEFAULT);
        type_ = file_ != H5I_INVALID_HID ? extract_type_from_file(file_) : nullptr ;
    }

    // Check for errors
    if (file_ == H5I_INVALID_HID) {
        H5Eprint(H5E_DEFAULT, stderr);
        throw "Failed to open HDF5 file";
    }
}

Writer::~Writer() {
    log_debug_printf(LOG, "Closing file%s\n", "");
    H5Fclose(file_);
}

void Writer::write(pvxs::Value value) {
    if (!type_) {
        log_debug_printf(LOG, "First update, extracting type%s\n", "");
        type_.reset(new TimeTable(value));
        std::cout << type_->nttable.build() << std::endl;
        build_file_structure(file_, type_);
    }

    auto tvalue = type_->wrap(value, true);

    log_debug_printf(LOG, "Got valid value%s\n", "");
}

} // namespace tabulator