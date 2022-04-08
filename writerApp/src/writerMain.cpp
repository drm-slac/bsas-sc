#include <iostream>

#include <pvxs/log.h>
#include <pvxs/client.h>
#include <pvxs/util.h>

#include <epicsThread.h>

#include <clipp.h>

#include "writer.h"

DEFINE_LOGGER(LOG, "writerMain");

enum StopReason {
    DURATION,
    INTERRUPTED,
    TIMEOUT,
    DISCONNECTED,
    ERROR
};

static const char *STOP_REASON_STR[] = {
    "Total execution duration was reached",
    "The program was interrupted",
    "Timed out while waiting for PV updates",
    "The PV disconnected",
    "An unexpected error occurred"
};

static bool is_err(StopReason reason) {
    switch (reason) {
        case DURATION:
        case INTERRUPTED:
            return false;

        case TIMEOUT:
        case DISCONNECTED:
        case ERROR:
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
    std::string output_file;
    double timeout;
    double duration;

    auto cli = (
        clipp::required("--input-pv")
            .doc("Name of the input PV")
            & clipp::value("input_pv", input_pv),

        clipp::required("--output-file")
            .doc("Path to the output HDF5 file")
            & clipp::value("output_file", output_file),

        clipp::required("--timeout")
            .doc("If no updates are received within timeout seconds, close the file and exit. A value of 0 means wait forever")
            & clipp::value("timeout", timeout),

        clipp::required("--duration")
            .doc("Total time, in seconds, to collect data for. If 0, collect until the program is killed")
            & clipp::value("duration", duration)
    );

    auto man_page = clipp::make_man_page(cli, argv[0]);

    if (!clipp::parse(argc, argv, cli)) {
        std::cerr << man_page;
        return 1;
    }

    // Validate arguments
    if (input_pv.empty()) {
        log_err_printf(LOG, "Input PV must not be empty%s\n", "");
        std::cerr << man_page;
        return 1;
    }

    if (output_file.empty()) {
        log_err_printf(LOG, "Output file path must not be empty%s\n", "");
        std::cerr << man_page;
        return 1;
    }

    if (timeout < 0.0) {
        log_err_printf(LOG, "Invalid timeout: %f seconds\n", timeout);
        std::cerr << man_page;
        return 1;
    }

    if (duration < 0.0) {
        log_err_printf(LOG, "Invalid duration: %f seconds\n", duration);
        std::cerr << man_page;
        return 1;
    }

    log_info_printf(LOG, "Starting%s\n", "");
    log_info_printf(LOG, "  input_pv=%s\n", input_pv.c_str());
    log_info_printf(LOG, "  output_file=%s\n", output_file.c_str());
    log_info_printf(LOG, "  timeout=%f s%s\n", timeout, timeout == 0.0 ? " (wait forever)" : "");
    log_info_printf(LOG, "  duration=%f s%s\n", duration, duration == 0.0 ? " (run forever)" : "");

    if (timeout == 0.0)
        timeout = std::numeric_limits<double>::max();

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

    epicsTimeStamp start;
    epicsTimeGetCurrent(&start);

    enum StopReason stop_reason;

    try {
        tabulator::Writer writer(output_file);

        for(;;) {
            double elapsed = seconds_since(start);

            // Check if we are done
            if (duration > 0 && elapsed > duration) {
                stop_reason = StopReason::DURATION;
                break;
            }

            // Wait for something to happen
            if (!event.wait(std::min(timeout, duration > 0 ? duration - elapsed : timeout))) {
                stop_reason = duration > 0 && seconds_since(start) >= duration ? StopReason::DURATION : StopReason::TIMEOUT;
                break;
            }

            if (interrupted) {
                stop_reason = StopReason::INTERRUPTED;
                break;
            }

            // Drain queue
            try {
                while (auto v = subscription->pop())
                    writer.write(v);

            } catch (pvxs::client::Disconnect & ex) {
                stop_reason = StopReason::DISCONNECTED;
                break;
            }
        }

    } catch (std::exception & ex) {
        log_err_printf(LOG, "Exception: %s\n", ex.what());
        stop_reason = StopReason::ERROR;
    } catch (const char *ex) {
        log_err_printf(LOG, "Exception: %s\n", ex);
        stop_reason = StopReason::ERROR;
    }


    log_printf(LOG, is_err(stop_reason) ? pvxs::Level::Err : pvxs::Level::Info, "Ending. Reason: %s\n", STOP_REASON_STR[stop_reason]);
    return is_err(stop_reason) ? 1 : 0;
}

//https://github.com/BlueBrain/HighFive