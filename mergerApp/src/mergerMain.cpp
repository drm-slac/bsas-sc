#include <string>
#include <iostream>
#include <fstream>
#include <deque>
#include <utility>

#include <epicsEvent.h>
#include <epicsMutex.h>
#include <epicsThread.h>
#include <epicsStdio.h>

#include <pvxs/log.h>
#include <pvxs/util.h>
#include <pvxs/client.h>
#include <pvxs/server.h>
#include <pvxs/sharedpv.h>

#include <clipp.h>

#include <tab/nttable.h>
#include <tab/util.h>

#include <iostream>

#include "taligntable.h"

DEFINE_LOGGER(LOG, "merger");
DEFINE_LOGGER(LISTENER_LOG, "merger.listener");
DEFINE_LOGGER(REACTOR_LOG, "merger.reactor");

using tabulator::nt::NTTable;
using tabulator::TimeSpan;
using tabulator::TimeBounds;
using tabulator::TimeAlignedTable;

static const size_t QUEUE_SIZE = 1024u;

class Runnable : public epicsThreadRunable {
protected:
    epicsThread thread_;

    Runnable(const char *name)
    : thread_(*this, name, epicsThreadGetStackSize(epicsThreadStackMedium), epicsThreadPriorityMedium)
    {}

public:
    void start() {
        thread_.start();
    }

    virtual ~Runnable() {};
};

class Listener : public Runnable {
private:
    pvxs::client::Context client_;
    pvxs::MPMCFIFO<std::pair<size_t, std::shared_ptr<pvxs::client::Subscription>>> queue_;
    std::vector<std::shared_ptr<pvxs::client::Subscription>> subscriptions_;
    std::shared_ptr<TimeAlignedTable> taligned_table_;

public:
    Listener(const std::vector<std::string> & pvlist, const std::shared_ptr<TimeAlignedTable> & taligned_table)
    : Runnable(typeid(Listener).name()),
      client_(pvxs::client::Context::fromEnv()), queue_(QUEUE_SIZE),
      subscriptions_(), taligned_table_(taligned_table)
    {
        // Create subscriptions
        size_t col_idx = 0;
        for (auto pvname : pvlist) {
            subscriptions_.emplace_back(
                client_
                    .monitor(pvname)
                    .event([this, col_idx](pvxs::client::Subscription &sub) {
                        this->queue_.push(std::make_pair(col_idx, sub.shared_from_this()));
                    })
                    .exec());
            ++col_idx;
        }
    }

    void run() {
        log_info_printf(LISTENER_LOG, "Starting%s\n", "");
        log_info_printf(LISTENER_LOG, "  # subscriptions=%lu\n", subscriptions_.size());

        for (;;) {
            auto item = queue_.pop();
            auto col_idx = item.first;
            auto sub = item.second;

            if (!sub)
                break;

            try {
                auto value = sub->pop();

                if (!value)
                    continue;

                //std::cout << sub->name() << std::endl << update.get();
                taligned_table_->push(col_idx, value);

            } catch (std::exception &e) {
                // TODO: handle disconnection gracefully
                log_err_printf(LISTENER_LOG, "Error: %s\n", e.what());
            }

            queue_.push(std::make_pair(col_idx, sub));
        }
    }

    virtual ~Listener() {}
};

class Reactor : public Runnable {
private:
    std::shared_ptr<TimeAlignedTable> taligned_table_;
    double period_;
    double timeout_;
    pvxs::server::SharedPV pv_;

public:
    Reactor(
        const std::shared_ptr<TimeAlignedTable> & taligned_table, double period, double timeout,
        pvxs::server::SharedPV & pv)
    : Runnable(typeid(Reactor).name()),
      taligned_table_(taligned_table), period_(period), timeout_(timeout), pv_(pv)
    {
        assert(period > 0.0);
        assert(timeout > period);
    }

    virtual void run() {
        double sleepPeriod = period_ / 5.0;

        log_info_printf(REACTOR_LOG, "Starting%s\n", "");
        log_info_printf(REACTOR_LOG, "  period=%.6f s\n", period_);
        log_info_printf(REACTOR_LOG, "  timeout=%.6f s\n", timeout_);
        log_info_printf(REACTOR_LOG, "  refresh=%.6f s\n", sleepPeriod);

        // Wait until all PVs have at least 1 update
        epicsTimeStamp start_ts, now_ts;
        epicsTimeGetCurrent(&start_ts);
        epicsTimeGetCurrent(&now_ts);

        log_info_printf(REACTOR_LOG, "Waiting until all PVs have at least one update%s\n", "");

        while (epicsTimeDiffInSeconds(&now_ts, &start_ts) < timeout_ && !taligned_table_->initialized()) {
            epicsThreadSleep(sleepPeriod);
            continue;
        }

        if (!taligned_table_->initialized()) {
            log_err_printf(REACTOR_LOG, "Failed to connect to all PVs... Exiting.%s\n", "");
            exit(0);
        }

        // Now that all PVs are connected, extract NTTable
        auto initial = taligned_table_->create();
        pv_.open(initial);

        /*std::string s;
        s << initial;
        log_info_printf(REACTOR_LOG, "Opened PV\n%s\n", initial);*/

        for (;;) {
            TimeBounds bounds = taligned_table_->get_timebounds();

            if (!bounds.valid) {
                epicsThreadSleep(sleepPeriod);
                continue;
            }

            TimeSpan shortest(bounds.earliest_start, bounds.earliest_end);
            TimeSpan longest(bounds.earliest_start, bounds.latest_end);

            log_debug_printf(REACTOR_LOG, "Considering timespans shortest=%.6f s, longest=%.6f\n",
                shortest.span_sec(), longest.span_sec());

            if (shortest.span_sec() < period_ && longest.span_sec() < timeout_) {
                epicsThreadSleep(sleepPeriod);
                continue;
            }

            taligned_table_->dump();
            /*
            printf("V=%d S=%u.%u  EE=%u.%u  LE=%u.%u\n",
                timespan.valid,
                timespan.start.secPastEpoch, timespan.start.nsec,
                timespan.earliest_end.secPastEpoch, timespan.earliest_end.nsec,
                timespan.latest_end.secPastEpoch, timespan.latest_end.nsec
            );*/

            epicsTimeStamp start = bounds.earliest_start;
            epicsTimeStamp end = bounds.earliest_start;
            epicsTimeAddSeconds(&end, period_);

            log_info_printf(REACTOR_LOG, "Extracting merged table spanning %.3f sec: %u.%u -- %u.%u\n",
                epicsTimeDiffInSeconds(&end, &start), start.secPastEpoch, start.nsec,
                end.secPastEpoch, end.nsec);

            auto value = taligned_table_->extract(start, end);
            pv_.post(value);
        }
    }

    virtual ~Reactor() {}
};

class Server : public Runnable {
private:
    pvxs::server::Server server_;

public:
    Server(const std::string & pvname, pvxs::server::SharedPV & pv)
    : Runnable(typeid(Server).name()),
      server_(pvxs::server::Config::fromEnv().build())

    {
        server_.addPV(pvname, pv);
    }

    virtual void run() {
        server_.run();
    }

    virtual ~Server() {}
};

static std::vector<std::string> pvlist_from_file(const std::string & filename) {
    // Open input file and fetch PV names
    std::ifstream filestream(filename);
    std::string line;
    std::vector<std::string> pvlist;

    while(std::getline(filestream, line))
        pvlist.push_back(line);

    return pvlist;
}

int main (int argc, char *argv[]) {
    std::string pvlist_file;
    uint32_t alignment;
    double period;
    double timeout;
    std::string pvname;

    pvxs::logger_config_env();

    auto cli = (
        clipp::required("--pvlist")
            .doc("file with list of input NTTable PVs to be merged (newline-separated)")
            & clipp::value("pvlist", pvlist_file),

        clipp::required("--alignment")
            .doc("time-alignment period, in micro seconds")
            & clipp::value("alignment", alignment),

        clipp::required("--period")
            .doc("update publication period, in seconds")
            & clipp::value("period", period),

        clipp::required("--timeout")
            .doc("time window to wait for laggards, in seconds")
            & clipp::value("timeout", timeout),

        clipp::required("--pvname")
            .doc("name of the output PV")
            & clipp::value("pvname", pvname)
    );

    auto man_page = clipp::make_man_page(cli, argv[0]);

    if (!clipp::parse(argc, argv, cli)) {
        std::cerr << man_page;
        return 1;
    }

    // Validate arguments
    if (alignment == 0) {
        log_err_printf(LOG, "Invalid alignment: %u micro-seconds\n", alignment);
        std::cerr << man_page;
        return 1;
    }

    if (period <= 0.0) {
        log_err_printf(LOG, "Invalid period: %.6f seconds\n", period);
        std::cerr << man_page;
        return 1;
    }

    if (timeout <= 0.0 || timeout < period) {
        log_err_printf(LOG, "Invalid timeout: %.6f seconds\n", timeout);
        std::cerr << man_page;
        return 1;
    }

    std::vector<std::string> pvlist(pvlist_from_file(pvlist_file));

    // Create

    /*epicsEvent sigint_evt;
    pvxs::SigInt handle([&sigint_evt]() {
        std::cout << "SIG" << std::endl;
        sigint_evt.trigger();
    });*/

    log_info_printf(LOG, "Starting%s\n", "");
    log_info_printf(LOG, "  pvlist=%s [%lu PVs]\n", pvlist_file.c_str(), pvlist.size());
    log_info_printf(LOG, "  alignment=%u us\n", alignment);
    log_info_printf(LOG, "  period=%.6f s\n", period);
    log_info_printf(LOG, "  timeout=%.6f s\n", timeout);
    log_info_printf(LOG, "  pvname=%s\n", pvname.c_str());

    // Shared objects
    auto taligned_table(std::make_shared<TimeAlignedTable>(pvlist, alignment));
    pvxs::server::SharedPV pv(pvxs::server::SharedPV::buildReadonly());

    // Prepare workers
    Listener listener(pvlist, taligned_table);
    Reactor updater(taligned_table, period, timeout, pv);
    Server server(pvname, pv);

    // Run workers
    listener.start();
    updater.start();
    server.start();

    //listener.wait();
    //sigint_evt.wait();

    // TODO: handle exit gracefully
    for(;;);
    log_info_printf(LOG, "Exiting%s\n", "");

    return 0;
}
