#ifndef TAB_WRITER_H
#define TAB_WRITER_H

#include <tab/timetable.h>
#include <hdf5.h>

namespace tabulator {

class Writer {

private:
    std::unique_ptr<TimeTable> type_;
    hid_t file_;

public:
    Writer(const std::string & path);
    ~Writer();

    void write(pvxs::Value value);
};

} // namespace tabulator

#endif