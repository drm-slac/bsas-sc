# Tabulator

This repository contains a few useful apps. All definitions are under the `tabulator` namespace. All EPICS V7 operations are done using [`pvxs`](https://github.com/mdavidsaver/pvxs).

## nttableApp

`nttableApp` defines the NTTable normative type in terms of `pvxs`. It also has some convenience methods. In a nutshell, an `NTTable` object not only defines the NTTable type, but also part of its contents: the names of the columns, the content of the column labels and the types of each column. A `pvxs::Value` obtained via `NTTable::create` will come with a pre-populated `labels` field.

## commonApp

`commonApp` provides useful definitions for the entire project.

### [`clipp.h`](https://github.com/muellan/clipp)

A convenient, header-only C++11 command line parser. Used in `mergerApp`.

### `tab/util.h`

Convenience functions for timestamp manipulation and alignment.

### `tab/timetable.h`

Defines three classes that are used throughout the project.

* `TimeTable`: a wrapper around `NTTable` that enforces that the first two columns are named `secondsPastEpoch` and `nanoseconds`, respectively, with equally named labels, both of which defined as `uint32_t`. It also has convenience methods to obtain the specifications of the subsequent columns (name, label, type).

* `TimeTableScalar`: inherits from `TimeTable`. It is intended to represente a sequence of readings from a scalar value, along with its metadata. It will enforce not only the same invariants as `TimeTable`, but also that the subsequent columns are a combination of:

| Name and Label | Type       | Notes                         |
|----------------|------------|-------------------------------|
| `value`        | `double`   | Always present                |
| `utag`         | `uint64_t` | (optional) Timestamp User Tag |
| `severity`     | `uint16_t` | (optional) Alarm severity     |
| `condition`    | `uint16_t` | (optional) Alarm condition    |
| `message`      | `string`   | (optional) Alarm message      |

* `TimeTableValue`: a wrapper around `pvxs::Value`, with convenience methods to get/set its columns. Its "type" will be `TimeTable` or `TimeTableScalar`.

## simulatorApp

This App contains a few useful simulators.

### `ioc-sim-scalar`

Simulates a number of scalar PVs via calc records. It provides databases to load 1, 16, 256, 1024 or 4096 individual instances. It simulates a sinusoid with amplitude 1.0 and by default the following alarm configuration:

* `LOLO`: `-0.99`
* `LOW`: `-0.95`
* `HIGH`: `0.95`
* `HIHI`: `0.99`

### `ioc-sim-table-scalar`

Simulates an NTTable with a configurable number of columns. It is meant to simulate aggregated readings from disparate but correlated PVs. The simulation happens in the device support of an `asub` record. The database provides a few macros to configure it:

* `COUNT`: The number of scalar PVs to simulate. All PVs will have the same value at the same timestamp, a sinusoidal equivalent to the one in `ioc-sim-scalar` (with the same alarm limits).
* `CONFIG`: Which metadata columns to simulate. OR together:
    * `0x00`: Value (always included)
    * `0x01`: User Tag (always 0 if included)
    * `0x02`: Alarm severity
    * `0x04`: Alarm condition
    * `0x08`: Alarm message (always empty if included)
* `SCAN`: How often to produce updates.
* `NUM_SAMPLES`: **Ignored**. See below
* `TIME_STEP_SEC`: The time difference between each simulated row, in seconds.
* `NUM_ROWS`: The number of rows to produce at each update.

**Example**:

If the configuration is:

> `P=SIM:`, `R=TABLE`, `COUNT=2`, `COLUMNS=0x02`, `SCAN=1 second`, `TIME_STEP_SEC=0.001`, `NUM_ROWS=1000`

Then every second a V7 NTTable named `SIM:TABLE` with `1000 rows` will be produced. Its columns will be:

| Column             | Label                  | Type       |
|--------------------|------------------------|------------|
| `secondsPastEpoch` | `secondsPastEpoch`     | `uint32_t` |
| `nanoseconds`      | `nanoseconds`          | `uint32_t` |
| `pv0_value`        | `SIM:TABLE:0 value`    | `double`   |
| `pv0_severity`     | `SIM:TABLE:0 severity` | `uint16_t` |
| `pv1_value`        | `SIM:TABLE:1 value`    | `double`   |
| `pv1_severity`     | `SIM:TABLE:1 severity` | `uint16_t` |

The timestamps between rows will be 1 millisecond apart.

### `ioc-sim-table-stat`

Simulates an NTTable with statistics samples. It is meant to simulate statistically compressed readings from disparate but correlated PVs. The simulation happens in the device support of an `asub` record. The database provides a few macros to configure it:

* `COUNT`: The number of "signals" to simulate. All "signals" will have the same value at the same timestamp, a compressed sinusoidal equivalent to the one in `ioc-sim-scalar`. Each row is equivalent to compressing `NUM_SAMPLES` samples.
* `CONFIG`: Indicates that this is a simulated statistics table. The only acceptable value is `0x10`.
* `SCAN`: How often to produce updates.
* `NUM_SAMPLES`: Number of compressed samples in each row.
* `TIME_STEP_SEC`: The time difference between each simulated row, in seconds.
* `NUM_ROWS`: The number of rows to produce at each update.

**Example**:

If the configuration is:

> `P=SIM:`, `R=STAT`, `COUNT=2`, `COLUMNS=0x02`, `SCAN=1 second`, `TIME_STEP_SEC=0.001`, `NUM_ROWS=1000`, `NUM_SAMPLES=10`

Then every second a V7 NTTable named `SIM:STAT` with `1000 rows` will be produced. Its columns will be:

| Column             | Label                  | Type       |
|--------------------|------------------------|------------|
| `secondsPastEpoch` | `secondsPastEpoch`     | `uint32_t` |
| `nanoseconds`      | `nanoseconds`          | `uint32_t` |
| `pv0_num_samp`     | `SIM:STAT:0 num_samp`  | `double`   |
| `pv0_min`          | `SIM:STAT:0 min`       | `double`   |
| `pv0_max`          | `SIM:STAT:0 max`       | `double`   |
| `pv0_mean`         | `SIM:STAT:0 mean`      | `double`   |
| `pv0_std`          | `SIM:STAT:0 std`       | `double`   |
| `pv0_rms`          | `SIM:STAT:0 rms`       | `double`   |
| `pv1_num_samp`     | `SIM:STAT:1 num_samp`  | `double`   |
| `pv1_min`          | `SIM:STAT:1 min`       | `double`   |
| `pv1_max`          | `SIM:STAT:1 max`       | `double`   |
| `pv1_mean`         | `SIM:STAT:1 mean`      | `double`   |
| `pv1_std`          | `SIM:STAT:1 std`       | `double`   |
| `pv1_rms`          | `SIM:STAT:1 rms`       | `double`   |

The timestamps between rows will be 1 millisecond apart.

## stackerApp

The stackerApp "stacks" readings from a **single** V3 scalar PV into an `NTTable`. It does that as the device support of an `asub` record. Its configuration parameters are:

* `INPP`, `INPR`: combined, they define the input scalar PV that will be read: `$(INPP)$(INPR)`.
* `CONFIG`: which columns to include. OR together:
    * `0x00`: Timestamp + Value (always included)
    * `0x01`: User Tag (always 0 if included)
    * `0x02`: Alarm severity
    * `0x04`: Alarm condition
    * `0x08`: Alarm message (always empty if included)
* `PERIOD`: how long (in seconds) to accumulate before publishing a table.

### `ioc-stacker`

`ioc-stacker` contains an IOC that can load a database for stacking 1, 16, 256, 1024 or 4096 PVs. Mainly, this is used in conjunction with `ioc-sim-scalar` so that we can "stack" up to thousands of individial scalars. This is not very performant. Its main use is as input to mergerApp, especially to test time-alignment since each stacked PV will have wildly different timestamps due to slow processing calc records from `ioc-sim-scalar`.

## mergerApp

A standalone executable (not an IOC) that *merges* together different input NTTables, time-aligned.

```
$ ./bin/linux-x86_64/merger
SYNOPSIS
        ./bin/linux-x86_64/merger --pvlist <pvlist> --alignment <alignment> --period <period>
                                  --timeout <timeout> --pvname <pvname>

OPTIONS
        --pvlist    file with list of input NTTable PVs to be merged (newline-separated)
        --alignment time-alignment period, in micro seconds
        --period    update publication period, in seconds
        --timeout   time window to wait for laggards, in seconds
        --pvname    name of the output PV
```

## writerApp

> TODO
