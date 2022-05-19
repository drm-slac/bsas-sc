#include <registryFunction.h>
#include <epicsExport.h>
#include <aSubRecord.h>
#include <menuFtype.h>
#include <errlog.h>
#include <dbAccess.h>
#include <alarm.h>
#include <devSup.h>
#include <epicsVersion.h>

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
using tabulator::TimeTableStat;
using tabulator::TimeTableValue;

namespace util = tabulator::util;

enum Config : uint8_t {
    TIMESTAMP_UTAG      = 0x01,
    ALARM_SEVERITY      = 0x02,
    ALARM_CONDITION     = 0x04,
    ALARM_MESSAGE       = 0x08,
    STAT                = 0x10,
};

static inline std::string column_name(const std::string & prefix, const std::string & suffix, const std::string & sep) {
    // TODO: check if prefix is malformed (and throw)
    std::size_t idx = prefix.rfind(":");
    return std::string("pv") + prefix.substr(idx + 1) + sep + suffix;
}

static inline std::string label_name(const std::string & prefix, const std::string & suffix, const std::string & sep) {
    return prefix + sep + suffix;
}

struct SimSource {
    const std::unique_ptr<TimeTable> type;
    virtual TimeTableValue simulate(size_t num_rows) = 0;
    virtual ~SimSource() {}

    SimSource(std::unique_ptr<TimeTable> && type)
    : type(std::move(type))
    {}
};

// 1 Hz sinusoid
class SimSourceScalar : public SimSource {
private:
    constexpr static const double HIHI = 0.99;
    constexpr static const double HIGH = 0.95;
    constexpr static const double LOW  = -0.95;
    constexpr static const double LOLO = -0.99;

    size_t t_;

public:
    //const TimeTableScalar type;
    const double step_sec;

    SimSourceScalar(const TimeTableScalar::Config & columns, double step_sec)
    : SimSource(std::unique_ptr<TimeTable>(new TimeTableScalar(columns))), t_(0), step_sec(step_sec)
    {}

    TimeTableValue simulate(size_t num_rows) {

        auto output = type->create();

        pvxs::shared_array<TimeTableScalar::VALUE_T> value(num_rows);

        for (size_t i = 0; i < num_rows; ++i) {
            value[i] = sin(t_*step_sec*2*PI);
            ++t_;
        }

        const TimeTableScalar::Config *config = &static_cast<const TimeTableScalar*>(type.get())->config;

        // Populate with zeroes for now
        if (config->utag) {
            pvxs::shared_array<const TimeTableScalar::UTAG_T> utag(num_rows, 0);
            output.set_column(TimeTableScalar::UTAG_COL, utag);
        }

        // Hard-coded alarm limits
        if (config->alarm_sev) {
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

        if (config->alarm_cond) {
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
        if (config->alarm_message) {
            pvxs::shared_array<const TimeTableScalar::ALARM_MSG_T> alarm_msg(num_rows, std::string(""));
            output.set_column(TimeTableScalar::ALARM_MSG_COL, alarm_msg);
        }

        output.set_column(TimeTableScalar::VALUE_COL, value.freeze());
        return output;
    }
};

class SimSourceStat : public SimSource {
private:
    size_t t_;

public:
    const size_t num_samp;
    const double step_sec;

    SimSourceStat(size_t num_samp, double step_sec)
    : SimSource(std::unique_ptr<TimeTable>(new TimeTableStat())), t_(0), num_samp(num_samp), step_sec(step_sec)
    {}

    TimeTableValue simulate(size_t num_rows) {

        pvxs::shared_array<TimeTableStat::VAL_T> val_col(num_rows);
        pvxs::shared_array<TimeTableStat::NUM_SAMP_T> num_samp_col(num_rows, static_cast<TimeTableStat::NUM_SAMP_T>(num_samp));
        pvxs::shared_array<TimeTableStat::MIN_T> min_col(num_rows);
        pvxs::shared_array<TimeTableStat::MAX_T> max_col(num_rows);
        pvxs::shared_array<TimeTableStat::MEAN_T> mean_col(num_rows);
        pvxs::shared_array<TimeTableStat::RMS_T> rms_col(num_rows);

        double sub_step_sec = step_sec / num_samp;

        for (size_t row = 0; row < num_rows; ++row) {
            std::vector<double> fast_samples(num_samp);

            double min = std::numeric_limits<double>::max();
            double max = -std::numeric_limits<double>::max();
            double sum = 0.0;
            double sumsq = 0.0;

            for (size_t i = 0; i < num_samp; ++i) {
                double sample = fast_samples[i] = sin((t_*step_sec + i*sub_step_sec)*2*PI);
                min = std::min(min, sample);
                max = std::max(max, sample);
                sum += sample;
                sumsq = std::pow(sample, 2);
            }

            double mean = sum / num_samp;
            double rms = std::sqrt(sumsq / num_samp);

            val_col[row] = mean;
            min_col[row] = min;
            max_col[row] = max;
            mean_col[row] = mean;
            rms_col[row] = rms;

            ++t_;
        }

        auto output = type->create();

        output.set_column(TimeTableStat::VAL_COL, val_col.freeze());
        output.set_column(TimeTableStat::NUM_SAMP_COL, num_samp_col.freeze());
        output.set_column(TimeTableStat::MIN_COL, min_col.freeze());
        output.set_column(TimeTableStat::MAX_COL, max_col.freeze());
        output.set_column(TimeTableStat::MEAN_COL, mean_col.freeze());
        output.set_column(TimeTableStat::RMS_COL, rms_col.freeze());

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

static TimeTable gen_type(const std::vector<std::string> & names, const std::unique_ptr<SimSource> & source, const std::string & label_sep, const std::string & col_sep) {
    std::vector<NTTable::ColumnSpec> data_columns;

    for (const auto & name : names)
        for (const auto & col : source->type->data_columns)
            data_columns.emplace_back(col.type_code, column_name(name, col.name, col_sep), label_name(name, col.label, label_sep));

    return TimeTable(data_columns);
}

struct SimTable {

    // Configuration
    const std::string name;
    const double step_sec;

    // Inner sources
    const std::vector<std::string> source_names;
    std::unique_ptr<SimSource> source;  // Single "real" source (which means all sources will have the same value at the same timestamp)

    // PVXS
    const TimeTable type;
    pvxs::server::SharedPV pv;

    // State
    epicsTimeStamp ts;

    SimTable(const char *name, double step_sec, size_t count, std::unique_ptr<SimSource> && source, const std::string & output_pv_name, const std::string & label_sep, const std::string & col_sep)
    :name(name), step_sec(step_sec), source_names(gen_source_names(output_pv_name, count)), source(std::move(source)),
     type(gen_type(source_names, this->source, label_sep, col_sep)),
     pv(pvxs::server::SharedPV::buildReadonly())
    {
        epicsTimeGetCurrent(&ts);

        pvxs::ioc::server().addPV(output_pv_name, pv);

        auto initial = type.create();
        pv.open(initial.get());

        log_debug_printf(LOG, "Sim[%s]: initialized\n", name);
    }

    void process(size_t num_rows) {
        epicsTimeStamp start, end;
        epicsTimeGetCurrent(&start);

        // Output
        auto output = type.create();

        // Timestamps
        pvxs::shared_array<TimeTable::SECONDS_PAST_EPOCH_T> secondsPastEpoch(num_rows);
        pvxs::shared_array<TimeTable::NANOSECONDS_T> nanoseconds(num_rows);

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
        auto v = source->simulate(num_rows);

        for (auto source_name = source_names.begin(); source_name != source_names.end(); ++source_name) {
            for (auto inner_column : v.type.data_columns) {
                output.set_column(outer_column->name, v.get_column_as<void>(inner_column.name));
                ++outer_column;
            }
        }
        pv.post(output.get());

        epicsTimeAddSeconds(&ts, num_rows*step_sec);

        epicsTimeGetCurrent(&end);
        log_debug_printf(LOG, "Sim[%s]: processed %lu rows in %.3f sec\n", name.c_str(), num_rows, epicsTimeDiffInSeconds(&end, &start));
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
    CHECK_INP(ftc, INPC, LONG);     // Number of compressed samples (if using STAT simulation)
    CHECK_INP(ftd, INPD, DOUBLE);   // Time Step (sec)
    CHECK_INP(fte, INPE, LONG);     // Number of rows in each update
    CHECK_INP(ftf, INPF, STRING);   // Label separator (between PV name and column label)
    CHECK_INP(ftg, INPG, STRING);   // Column separator (PV identifier and column name)
    #undef CHECK_INP

    long num_pvs = *static_cast<long*>(prec->a);
    long config = *static_cast<long*>(prec->b);
    long num_samp = *static_cast<long*>(prec->c);
    double step_sec = *static_cast<double*>(prec->d);
    const char *label_sep = static_cast<const char *>(prec->f);
    const char *col_sep = static_cast<const char *>(prec->g);

    // Assume our name is xxxx_ASUB, chop off the _ASUB suffix to derive the V7 PV name
    std::string output_pv_name(prec->name);
    {
        size_t found = output_pv_name.rfind("_ASUB");
        assert(found != std::string::npos);
        output_pv_name.resize(found);
    }

    log_debug_printf(LOG, "sim_init[%s]: Simulating %ld %s PVs, step=%.3f sec (stat samples=%ld) to output=%s, label separator='%s', column separator='%s'\n",
        prec->name, num_pvs, (config & Config::STAT) != 0 ? "statistics" : "scalar",
        step_sec, num_samp, output_pv_name.c_str(), label_sep, col_sep);

    std::unique_ptr<SimSource> source;

    if ((config & Config::STAT) != 0) {
        source.reset(new SimSourceStat(num_samp, step_sec));
    } else {
        TimeTableScalar::Config scalar_config(
            (config & Config::TIMESTAMP_UTAG) != 0,
            (config & Config::ALARM_SEVERITY) != 0,
            (config & Config::ALARM_CONDITION) != 0,
            (config & Config::ALARM_MESSAGE) != 0
        );

        source.reset(new SimSourceScalar(scalar_config, step_sec));
    }

    SimTable *sim = new SimTable(prec->name, step_sec, num_pvs, std::move(source), output_pv_name, label_sep, col_sep);
    log_debug_printf(LOG, "created simtable%s\n", "");
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

    long num_rows = *static_cast<long*>(prec->e);
    sim->process(num_rows);

    return 0;
}

epicsRegisterFunction(sim_init);
epicsRegisterFunction(sim_proc);