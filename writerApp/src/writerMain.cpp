#include <iostream>
#include <iomanip>

#include <pvxs/log.h>
#include <pvxs/client.h>
#include <pvxs/util.h>

#include <epicsThread.h>
#include <epicsStdio.h>
#include <epicsString.h>

#include <clipp.h>

#include <sys/stat.h>
#include <libgen.h>

#include "writer.h"

DEFINE_LOGGER(LOG, "writerMain");

enum StopReason {
    INTERRUPTED,
    TIMEOUT,
    DISCONNECTED,
    ERROR
};

static const char *STOP_REASON_STR[] = {
    "The program was interrupted",
    "Timed out while waiting for PV updates",
    "The PV disconnected",
    "An unexpected error occurred"
};

static bool is_err(StopReason reason) {
    switch (reason) {
        case StopReason::INTERRUPTED:
            return false;

        case StopReason::TIMEOUT:
        case StopReason::DISCONNECTED:
        case StopReason::ERROR:
        default:
            return true;
    }
}

static double seconds_since(epicsTimeStamp & ts) {
    epicsTimeStamp now;
    epicsTimeGetCurrent(&now);
    return epicsTimeDiffInSeconds(&now, &ts);
}

int main (int argc, char *argv[]) {

    pvxs::logger_config_env();

    std::string input_pv;
    std::string base_directory;
    std::string file_prefix;
    double timeout_sec;
    double max_duration_sec = 0;
    size_t max_size_mb = 0;
    std::string label_sep = ".";
    std::string col_sep = "_";

    auto cli = (
        clipp::required("--input-pv")
            .doc("Name of the input PV")
            & clipp::value("input_pv", input_pv),

        clipp::required("--base-directory")
            .doc("Path to the base directory for HDF5 files")
            & clipp::value("base_directory", base_directory),

        clipp::required("--file-prefix")
            .doc("Prefix for generated HDF5 files")
            & clipp::value("file_prefix", file_prefix),

        clipp::required("--timeout-sec")
            .doc("If no updates are received within timeout (in seconds), close the file and exit. A value of 0 means wait forever")
            & clipp::value("timeout_sec", timeout_sec),

        clipp::option("--max-duration-sec")
            .doc("Maximum time, in seconds, to collect data for in a single HDF5 file. If 0, don't limit files by time. Default: 0")
            & clipp::value("max_duration_sec", max_duration_sec),

        clipp::option("--max-size-mb")
            .doc("Maximum size, in MB, to collect data for in a single HDF5 file. If 0, don't limit files by size. Default: 0")
            & clipp::value("max_size_mb", max_size_mb),

        clipp::option("--label-sep")
            .doc(std::string("separator between PV name and column name in labels. Default: '") + label_sep + "'")
            & clipp::value("label_sep", label_sep),

        clipp::option("--column-sep")
            .doc(std::string("separator between PV identifier and original column name. Default: '") + col_sep + "'")
            & clipp::value("col_sep", col_sep)
    );

    std::stringstream ss;
    ss << clipp::make_man_page(cli, argv[0]);
    std::string man_page = ss.str();

    if (!clipp::parse(argc, argv, cli)) {
        fputs(man_page.c_str(), stderr);
        return 1;
    }

    // Validate arguments
    #define CHECK_ARG(COND, FMT, ARG)\
        do {\
            if ((COND)) {\
                log_err_printf(LOG, FMT, ARG);\
                fputs(man_page.c_str(), stderr);\
                return 1;\
            }\
        } while(0)

    CHECK_ARG(input_pv.empty(), "Input PV must not be empty %s\n", "");
    CHECK_ARG(base_directory.empty(), "Base directory path must not be empty%s\n", "");
    CHECK_ARG(file_prefix.empty(), "File prefix must not be empty%s\n", "");
    CHECK_ARG(timeout_sec < 0.0, "Invalid timeout: %f seconds\n", timeout_sec);
    CHECK_ARG(max_duration_sec < 0.0, "Invalid duration: %f seconds\n", max_duration_sec);

    struct stat base_dir_stat;
    int base_dir_stat_res = stat(base_directory.c_str(), &base_dir_stat);

    CHECK_ARG(base_dir_stat_res < 0, "Failed to stat base directory %s\n", base_directory.c_str());
    CHECK_ARG(!S_ISDIR(base_dir_stat.st_mode), "Path %s is not a directory\n", base_directory.c_str());

    #undef CHECK_ARG

    log_info_printf(LOG, "Starting%s\n", "");
    log_info_printf(LOG, "  input_pv=%s\n", input_pv.c_str());
    log_info_printf(LOG, "  output=%s/YYYY/MM/DD/%s_YYYYMMDD_hhmmss.h5\n", base_directory.c_str(), file_prefix.c_str());
    log_info_printf(LOG, "  timeout=%f s%s\n", timeout_sec, timeout_sec == 0.0 ? " (wait forever)" : "");
    log_info_printf(LOG, "  max duration=%f s%s\n", max_duration_sec, max_duration_sec == 0.0 ? " (no time limit)" : "");
    log_info_printf(LOG, "  max size=%lu MB%s\n", max_size_mb, max_size_mb == 0 ? " (no size limit)" : "");
    log_info_printf(LOG, "  label separator='%s'\n", label_sep.c_str());
    log_info_printf(LOG, "  column separator='%s'\n", col_sep.c_str());

    if (timeout_sec == 0.0)
        timeout_sec = std::numeric_limits<double>::max();

    if (max_duration_sec == 0.0)
        max_duration_sec = std::numeric_limits<double>::max();

    if (max_size_mb == 0)
        max_size_mb = std::numeric_limits<size_t>::max();

    // Setup signal handler
    epicsEvent event;
    bool interrupted = false;

    pvxs::SigInt handle([&event, &interrupted]() {
        interrupted = true;
        event.trigger();
    });

    // Setup monitor
    pvxs::client::Context client(pvxs::client::Context::fromEnv());

    auto subscription = client
        .monitor(input_pv)
        .event([&event](pvxs::client::Subscription &) { event.signal(); })
        .maskDisconnected(false)
        .exec();

    enum StopReason stop_reason;

    try {
        bool done = false;

        while (!done) {

            epicsTimeStamp start;
            epicsTimeGetCurrent(&start);

            // Generate full path to output file
            struct tm start_tm;
            epicsTimeToTM(&start_tm, NULL, &start);

            int year = 1900 + start_tm.tm_year;
            int month = start_tm.tm_mon;
            int day = start_tm.tm_mday;
            int hour = start_tm.tm_hour;
            int minute = start_tm.tm_min;
            int second = start_tm.tm_sec;

            char output_file[4096];
            epicsSnprintf(output_file, sizeof(output_file),
                "%s/%d/%02d/%02d/%s_%d%02d%02d_%02d%02d%02d.h5",
                base_directory.c_str(), year, month, day,
                file_prefix.c_str(), year, month, day,
                hour, minute, second);

            // Create directory
            std::stringstream output_dir;
            output_dir.fill('0');
            output_dir << base_directory;

            std::vector<int> components = {year, month, day};
            for (int component : components) {
                output_dir << "/" << std::setw(2) << component;

                std::string out_dir = output_dir.str();

                log_debug_printf(LOG, "Creating '%s'\n", out_dir.c_str());

                if (mkdir(out_dir.c_str(), 0777) < 0 && errno != EEXIST)
                    throw std::runtime_error(std::string("Failed to mkdir ") + out_dir + ": " + strerror(errno));
            }

            tabulator::Writer writer(input_pv, output_file, label_sep, col_sep);

            for(;;) {
                double elapsed_sec = seconds_since(start);

                // Wait for something to happen
                if (!event.wait(std::min(timeout_sec, max_duration_sec > 0 ? max_duration_sec - elapsed_sec : timeout_sec))) {
                    if (elapsed_sec > timeout_sec) {
                        // We timed-out waiting for a PV update. We are done, exit.
                        done = true;
                        stop_reason = StopReason::TIMEOUT;
                        break;

                    } else {
                        // We reached the maximum duration, exit the inner loop so a new file can be generated
                        log_info_printf(LOG, "File %s has duration of %.0f sec, which meets or exceeds maximum duration of %.0f sec\n",
                            output_file, elapsed_sec, max_duration_sec);
                        break;
                    }
                }

                // We were interrupted (CTRL+C), exit
                if (interrupted) {
                    done = true;
                    stop_reason = StopReason::INTERRUPTED;
                    break;
                }

                // There are updates, drain update queue
                try {
                    while (auto v = subscription->pop())
                        writer.write(v);

                } catch (pvxs::client::Disconnect & ex) {
                    done = true;
                    stop_reason = StopReason::DISCONNECTED;
                    break;
                }

                // Check if the file is larger than the max size
                struct stat s = {};
                if (stat(output_file, &s) < 0)
                    throw std::runtime_error(std::string("Failed to stat output file ") + output_file);

                size_t file_size_mb = s.st_size / 1024 / 1024;

                if (file_size_mb >= max_size_mb) {
                    // We reached the maximum file size, exit the inner loop so a new file can be generated
                    log_info_printf(LOG, "File %s has size %lu MB, which meets or exceeds maximum size of %lu MB\n", 
                        output_file, file_size_mb, max_size_mb);
                    break;
                }
            }
        }
    } catch (std::exception & ex) {
        log_err_printf(LOG, "Exception: %s\n", ex.what());
        stop_reason = StopReason::ERROR;
    } catch (const char *ex) {
        log_err_printf(LOG, "Exception: %s\n", ex);
        stop_reason = StopReason::ERROR;
    } catch(...) {
        log_err_printf(LOG, "Exception: (unknown)%s\n", "");
        stop_reason = StopReason::ERROR;
    }

    log_printf(LOG, is_err(stop_reason) ? pvxs::Level::Err : pvxs::Level::Info, "Ending. Reason: %s\n", STOP_REASON_STR[stop_reason]);
    return is_err(stop_reason) ? 1 : 0;
}
