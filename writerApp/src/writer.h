#ifndef TAB_WRITER_H
#define TAB_WRITER_H

#include <tab/timetable.h>

#include <highfive/H5File.hpp>

#include <map>

namespace tabulator {

class Writer {

private:
    std::string input_pv_;
    std::unique_ptr<TimeTable> type_;
    std::string file_path_;
    std::unique_ptr<HighFive::File> file_;
    std::string label_sep_;
    std::string col_sep_;
    std::map<std::string, HighFive::DataSet> datasets_;

    void build_file_structure(size_t chunk_size);

public:
    Writer(const std::string & input_pv, const std::string & path,
        const std::string & label_sep, const std::string & col_sep);
    void write(pvxs::Value value);
    std::string get_file_path() const;

};

} // namespace tabulator

#endif