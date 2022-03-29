#include <registryFunction.h>
#include <epicsExport.h>
#include <aSubRecord.h>
#include <menuFtype.h>
#include <errlog.h>
#include <dbAccess.h>
#include <alarm.h>

#include <vector>
#include <iostream>
#include <random>
#include <cmath>

#include <pvxs/server.h>
#include <pvxs/sharedpv.h>
#include <pvxs/log.h>
#include <pvxs/iochooks.h>
#include <pvxs/nt.h>

#include <tab/nttable.h>
#include <tab/util.h>
#include <tab/timetable.h>

#define PI 3.14159265

DEFINE_LOGGER(LOG, "sim");

using tabulator::nt::NTTable;
using tabulator::TimeTable;
using tabulator::TimeTableScalar;
using tabulator::TimeTableValue;

namespace util = tabulator::util;

enum Columns : uint8_t {
    TIMESTAMP_UTAG      = 0x01,
    ALARM_SEVERITY      = 0x02,
    ALARM_CONDITION     = 0x04,
    ALARM_MESSAGE       = 0x08,
};


static inline std::string column_name(const std::string & prefix, const std::string & suffix) {
    // TODO: check if prefix is malformed (and throw)
    std::size_t idx = prefix.rfind(":");
    return std::string("pv") + prefix.substr(idx + 1) + "_" + suffix;
}

static inline std::string label_name(const std::string & prefix, const std::string & suffix) {
    return prefix + " " + suffix;
}

// 1 Hz sinusoid
class SimSource {
private:
    constexpr static const double HIHI = 0.99;
    constexpr static const double HIGH = 0.95;
    constexpr static const double LOW  = -0.95;
    constexpr static const double LOLO = -0.99;

    size_t t_;

public:
    const TimeTableScalar type;
    const double step_sec;

    SimSource(const TimeTableScalar::Config & columns, double step_sec)
    : t_(0), type(columns), step_sec(step_sec)
    {}

    TimeTableValue simulate(size_t num_rows) {

        auto output = type.create();

        pvxs::shared_array<TimeTableScalar::VALUE_T> value(num_rows);

        for (size_t i = 0; i < num_rows; ++i) {
            value[i] = sin(t_*step_sec*2*PI);
            ++t_;
        }

        // Populate with zeroes for now
        if (type.config.utag) {
            pvxs::shared_array<const TimeTableScalar::UTAG_T> utag(num_rows, 0);
            output.set_column(TimeTableScalar::UTAG_COL, utag);
        }

        // Hard-coded alarm limits
        if (type.config.alarm_sev) {
            pvxs::shared_array<TimeTableScalar::ALARM_SEV_T> alarm_sev(num_rows);

            for (size_t i = 0; i < num_rows; ++i) {
                auto v = value[i];

                if (v < LOLO || v > HIHI)
                    alarm_sev[i] = epicsSevMajor;
                else if (v < LOW || v > HIGH)
                    alarm_sev[i] = epicsSevMinor;
                else
                    alarm_sev[i] = epicsSevNone;
            }

            output.set_column(TimeTableScalar::ALARM_SEV_COL, alarm_sev.freeze());
        }

        if (type.config.alarm_cond) {
            pvxs::shared_array<TimeTableScalar::ALARM_COND_T> alarm_cond(num_rows);

            for (size_t i = 0; i < num_rows; ++i) {
                auto v = value[i];

                if (v < LOLO)
                    alarm_cond[i] = epicsAlarmLoLo;
                else if (v < LOW)
                    alarm_cond[i] = epicsAlarmLow;
                else if (v > HIHI)
                    alarm_cond[i] = epicsAlarmHiHi;
                else if (v > HIGH)
                    alarm_cond[i] = epicsAlarmHigh;
                else
                    alarm_cond[i] = epicsAlarmNone;
            }

            output.set_column(TimeTableScalar::ALARM_COND_COL, alarm_cond.freeze());
        }

       // Populate with empty messages for now
        if (type.config.alarm_message) {
            pvxs::shared_array<const TimeTableScalar::ALARM_MSG_T> alarm_msg(num_rows, std::string(""));
            output.set_column(TimeTableScalar::ALARM_MSG_COL, alarm_msg);
        }

        output.set_column(TimeTableScalar::VALUE_COL, value.freeze());
        return output;
    }
};

static std::vector<std::string> gen_source_names(const std::string & prefix, size_t count) {
    std::vector<std::string> names;

    int width = ceil(log2(count) / log2(16));

    for (size_t i = 0; i < count; ++i) {
        char name[1024];
        epicsSnprintf(name, sizeof(name), "%s:%0*lX", prefix.c_str(), width, i);
        names.emplace_back(name);
    }

    return names;
}

static std::vector<SimSource> gen_sources(size_t count, const TimeTableScalar::Config & config, double step_sec) {
    std::vector<SimSource> sources;

    for (size_t i = 0; i < count; ++i)
        sources.emplace_back(config, step_sec);

    return sources;
}

static TimeTable gen_type(const std::vector<std::string> & names, const std::vector<SimSource> & sources) {
    std::vector<NTTable::ColumnSpec> data_columns;

    auto name = names.begin();
    auto source = sources.begin();

    for (; name != names.end(); ++name, ++source)
        for (auto col : source->type.data_columns)
            data_columns.emplace_back(col.type_code, column_name(*name, col.name), label_name(*name, col.label));

    return TimeTable(data_columns);
}

struct SimTable {
    // Configuration
    const std::string name;
    const TimeTableScalar::Config config;
    const double step_sec;

    // Inner sources
    const std::vector<std::string> source_names;
    std::vector<SimSource> sources;

    // PVXS
    const TimeTable type;
    pvxs::server::SharedPV pv;

    // State
    epicsTimeStamp ts;

    SimTable(const char *name, size_t count, TimeTableScalar::Config config, double step_sec,
        const char * output_pv_name)
    :name(name), config(config), step_sec(step_sec), source_names(gen_source_names(output_pv_name, count)),
     sources(gen_sources(count, config, step_sec)), type(gen_type(source_names, sources)),
     pv(pvxs::server::SharedPV::buildReadonly())
    {
        epicsTimeGetCurrent(&ts);

        pvxs::ioc::server().addPV(output_pv_name, pv);

        auto initial = type.create();
        pv.open(initial.get());

        log_debug_printf(LOG, "Sim[%s]: initialized\n", name);
    }

    void process(size_t num_rows) {
        log_debug_printf(LOG, "Sim[%s]: process %lu rows\n", name.c_str(), num_rows);

        // Output
        auto output = type.create();

        // Timestamps
        pvxs::shared_array<TimeTableScalar::SECONDS_PAST_EPOCH_T> secondsPastEpoch(num_rows);
        pvxs::shared_array<TimeTableScalar::NANOSECONDS_T> nanoseconds(num_rows);

        for (size_t i = 0; i < num_rows; ++i) {
            epicsTimeStamp row_ts = ts;
            epicsTimeAddSeconds(&row_ts, i*step_sec);

            secondsPastEpoch[i] = row_ts.secPastEpoch;
            nanoseconds[i] = row_ts.nsec;
        }

        output.set_column(TimeTable::SECONDS_PAST_EPOCH_COL, secondsPastEpoch.freeze());
        output.set_column(TimeTable::NANOSECONDS_COL, nanoseconds.freeze());

        // Outer columns
        auto outer_column = type.data_columns.begin();

        auto source_name = source_names.begin();
        auto source = sources.begin();
        for (; source_name != source_names.end(); ++source_name, ++source) {
            auto v = source->simulate(num_rows);

            for (auto inner_column : v.type.data_columns) {
                output.set_column(outer_column->name, v.get_column_as<void>(inner_column.name));
                ++outer_column;
            }
        }
        pv.post(output.get());

        epicsTimeAddSeconds(&ts, num_rows*step_sec);
    }
};

// Called during aSub initialization
static long sim_init(aSubRecord *prec) {
    #define CHECK_INP(FT,INP,TYP)\
        do {\
            if (prec->FT != menuFtype ## TYP) {\
                errlogSevPrintf(errlogMajor, "%s: incorrect input type for " #INP "; expected " #TYP "\n", prec->name);\
                return S_dev_badInpType;\
            }\
        } while(0)

    CHECK_INP(fta, INPA, LONG);     // Number of PVs
    CHECK_INP(ftb, INPB, LONG);     // Column selection
    CHECK_INP(ftc, INPC, DOUBLE);   // Time Step (sec)
    CHECK_INP(ftd, INPD, LONG);     // Number of rows in each update
    CHECK_INP(fte, INPE, CHAR);     // Output NTTable name
    #undef CHECK_INP

    long num_pvs = *static_cast<long*>(prec->a);
    long columns = *static_cast<long*>(prec->b);
    double step_sec = *static_cast<double*>(prec->c);
    const char *output_pv_name = static_cast<const char*>(prec->e);

    TimeTableScalar::Config config(
        (columns & Columns::TIMESTAMP_UTAG) != 0,
        (columns & Columns::ALARM_SEVERITY) != 0,
        (columns & Columns::ALARM_CONDITION) != 0,
        (columns & Columns::ALARM_MESSAGE) != 0
    );

    SimTable *sim = new SimTable(prec->name, num_pvs, config, step_sec, output_pv_name);
    prec->dpvt = static_cast<void*>(sim);

    return 0;
}

static long sim_proc(aSubRecord *prec) {
    SimTable *sim = static_cast<SimTable*>(prec->dpvt);

    if (!sim) {
        log_crit_printf(LOG, "sim_proc[%s] record in bad state\n", prec->name);
        errlogSevPrintf(errlogMajor, "%s: record in bad state\n", prec->name);
        return S_dev_NoInit;
    }

    long num_rows = *static_cast<long*>(prec->d);
    sim->process(num_rows);

    return 0;
}

epicsRegisterFunction(sim_init);
epicsRegisterFunction(sim_proc);