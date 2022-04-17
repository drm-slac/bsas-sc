#ifndef TAB_WRITER_H
#define TAB_WRITER_H

#include <tab/timetable.h>

#include <highfive/H5File.hpp>

#include <map>

namespace tabulator {

class Writer {

private:
    std::unique_ptr<TimeTable> type_;
    std::unique_ptr<HighFive::File> file_;
    std::map<std::string, HighFive::DataSet> datasets_;

    void build_file_structure(size_t chunk_size);

public:
    Writer(const std::string & path);

    void write(pvxs::Value value);
};

} // namespace tabulator

#endif