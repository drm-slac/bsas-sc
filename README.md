# Tabulator

This repository contains a few useful apps. All definitions are under the `tabulator` namespace. All EPICS V7 operations are done using [`pvxs`](https://github.com/mdavidsaver/pvxs).

**Cloning**

```
git clone --recursive https://github.com/brunoseivam/tabulator
```

**Dependencies**

* `libhdf5-dev` (Debian systems) or equivalent
* [EPICS](https://github.com/epics-base/epics-base/) @ 7.0.3.1 or later
* [pvxs](https://github.com/mdavidsaver/pvxs) @ 344a96207f93583ff4804601c5563d3ec479cb2a or later


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

* `TimeTableScalar`: inherits from `TimeTable`. It is intended to represent a sequence of readings from a scalar value, along with its metadata. It will enforce not only the same invariants as `TimeTable`, but also that the subsequent columns are a combination of:

| Name and Label | Type       | Notes                         |
|----------------|------------|-------------------------------|
| `value`        | `double`   | Always present                |
| `utag`         | `uint64_t` | (optional) Timestamp User Tag |
| `severity`     | `uint16_t` | (optional) Alarm severity     |
| `condition`    | `uint16_t` | (optional) Alarm condition    |
| `message`      | `string`   | (optional) Alarm message      |

* `TimeTableStat`: inherits from `TimeTable`. It is intended to represent a sequence of statistically compressed readings from a scalar value. It will enforce not only the same invariants as `TimeTable`, but also that the subsequent columns are:

| Name and Label | Type     | Notes                         |
|----------------|----------|-------------------------------|
| `VAL`          | `double` | Representative sample         |
| `CNT`          | `double` | Number of compressed samples  |
| `MIN`          | `double` | Minimum value in samples      |
| `MAX`          | `double` | Maximum value in samples      |
| `AVG`          | `double` | Mean value of samples         |
| `RMS`          | `double` | Standard deviation of samples |

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

* `NUM_TABLES`: The number of output NTTable PVs to simulate. All rows in all tables will have the same value.
* `NUM_SIGNALS`: The number of scalar PVs in each NTTable to simulate. All PVs will have the same value at the same timestamp, a sinusoidal equivalent to the one in `ioc-sim-scalar` (with the same alarm limits).
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
* `LABEL_SEP`: The separator to use when building labels. Default: `.`.
* `COL_SEP`: The separator to use when building column names. Default: `_`.

**Example**:

If the configuration is:

> `P=SIM:`, `R=TABLE`, `NUM_TABLES=1`, `NUM_SIGNALS=2`, `COLUMNS=0x02`, `SCAN=1 second`, `TIME_STEP_SEC=0.001`, `NUM_ROWS=1000`, `LABEL_SEP=.`, `COL_SEP=_`

Then every second a V7 NTTable named `SIM:TABLE:0` with `1000 rows` will be produced. Its columns will be:

| Column             | Label                  | Type       |
|--------------------|------------------------|------------|
| `secondsPastEpoch` | `secondsPastEpoch`     | `uint32_t` |
| `nanoseconds`      | `nanoseconds`          | `uint32_t` |
| `pv0_value`        | `SIM:SIG:0.value`      | `double`   |
| `pv0_severity`     | `SIM:SIG:0.severity`   | `uint16_t` |
| `pv1_value`        | `SIM:SIG:1.value`      | `double`   |
| `pv1_severity`     | `SIM:SIG:1.severity`   | `uint16_t` |

The timestamps between rows will be 1 millisecond apart.

### `ioc-sim-table-stat`

Simulates an NTTable with statistics samples. It is meant to simulate statistically compressed readings from disparate but correlated PVs. The simulation happens in the device support of an `asub` record. The database provides a few macros to configure it:

* `NUM_TABLES`: The number of output NTTable PVs to simulate. All rows in all tables will have the same value.
* `NUM_SIGNALS`: The number of "signals" to simulate. All "signals" will have the same value at the same timestamp, a compressed sinusoidal equivalent to the one in `ioc-sim-scalar`. Each row is equivalent to compressing `NUM_SAMPLES` samples.
* `CONFIG`: Indicates that this is a simulated statistics table. The only acceptable value is `0x10` (or `16` decimal).
* `SCAN`: How often to produce updates.
* `NUM_SAMPLES`: Number of compressed samples in each row.
* `TIME_STEP_SEC`: The time difference between each simulated row, in seconds.
* `NUM_ROWS`: The number of rows to produce at each update.

**Example**:

If the configuration is:

> `P=SIM:`, `R=STAT`, `NUM_TABLES=2`, `NUM_SIGNALS=2`, `COLUMNS=0x02`, `SCAN=1 second`, `TIME_STEP_SEC=0.001`, `NUM_ROWS=1000`, `NUM_SAMPLES=10`, `LABEL_SEP=.`, `COL_SEP=_`

Then every second two V7 NTTable named `SIM:STAT:0` and `SIM:STAT:1` with `1000 rows` will be produced. Their columns will be:

#### `SIM:STAT:0`

| Column             | Label                  | Type       |
|--------------------|------------------------|------------|
| `secondsPastEpoch` | `secondsPastEpoch`     | `uint32_t` |
| `nanoseconds`      | `nanoseconds`          | `uint32_t` |
| `pv0_VAL`          | `SIM:SIG:0.VAL`        | `double`   |
| `pv0_CNT`          | `SIM:SIG:0.CNT`        | `uint32_t` |
| `pv0_MIN`          | `SIM:SIG:0.MIN`        | `double`   |
| `pv0_MAX`          | `SIM:SIG:0.MAX`        | `double`   |
| `pv0_AVG`          | `SIM:SIG:0.AVG`        | `double`   |
| `pv0_RMS`          | `SIM:SIG:0.RMS`        | `double`   |
| `pv1_VAL`          | `SIM:SIG:1.VAL`        | `double`   |
| `pv1_CNT`          | `SIM:SIG:1.CNT`        | `uint32_t` |
| `pv1_MIN`          | `SIM:SIG:1.MIN`        | `double`   |
| `pv1_MAX`          | `SIM:SIG:1.MAX`        | `double`   |
| `pv1_AVG`          | `SIM:SIG:1.AVG`        | `double`   |
| `pv1_RMS`          | `SIM:SIG:1.RMS`        | `double`   |

#### `SIM:STAT:1`

| Column             | Label                  | Type       |
|--------------------|------------------------|------------|
| `secondsPastEpoch` | `secondsPastEpoch`     | `uint32_t` |
| `nanoseconds`      | `nanoseconds`          | `uint32_t` |
| `pv0_VAL`          | `SIM:SIG:2.VAL`        | `double`   |
| `pv0_CNT`          | `SIM:SIG:2.CNT`        | `uint32_t` |
| `pv0_MIN`          | `SIM:SIG:2.MIN`        | `double`   |
| `pv0_MAX`          | `SIM:SIG:2.MAX`        | `double`   |
| `pv0_AVG`          | `SIM:SIG:2.AVG`        | `double`   |
| `pv0_RMS`          | `SIM:SIG:2.RMS`        | `double`   |
| `pv1_VAL`          | `SIM:SIG:3.VAL`        | `double`   |
| `pv1_CNT`          | `SIM:SIG:3.CNT`        | `uint32_t` |
| `pv1_MIN`          | `SIM:SIG:3.MIN`        | `double`   |
| `pv1_MAX`          | `SIM:SIG:3.MAX`        | `double`   |
| `pv1_AVG`          | `SIM:SIG:3.AVG`        | `double`   |
| `pv1_RMS`          | `SIM:SIG:3.RMS`        | `double`   |


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
        ./bin/linux-x86_64/merger --pvlist <pvlist> --period-sec <period_sec> [--timeout-sec
                                  <timeout_sec>] --pvname <pvname> [--label-sep <label_sep>]
                                  [--column-sep <col_sep>]

OPTIONS
        --pvlist    File with list of input NTTable PVs to be merged (newline-separated).
        --period-sec
                    Update publication period, in seconds.

        --timeout-sec
                    Time window to wait for laggards, in seconds. Default: 0 (wait forever).

        --pvname    Name of the output PV.
        --label-sep Separator between PV name and column name in labels. Default: '.'.
        --column-sep
                    Separator between PV identifier and original column name. Default: '_'.
```

## highfiveApp

[BlueBrain/HighFive](https://github.com/BlueBrain/HighFive): C++ wrapper library for HDF5, included here as a submodule.

## writerApp

A standalone executable (not an IOC) that writes NTTables to HDF5 files.

```
$ ./bin/linux-x86_64/writer --help
SYNOPSIS
        ./bin/linux-x86_64/writer --input-pv <input_pv> --base-directory <base_directory>
                                  --file-prefix <file_prefix> --root-group <root_group>
                                  --timeout-sec <timeout_sec> [--max-duration-sec
                                  <max_duration_sec>] [--max-size-mb <max_size_mb>] [--label-sep
                                  <label_sep>] [--column-sep <col_sep>]

OPTIONS
        --input-pv  Name of the input PV
        --base-directory
                    Path to the base directory for HDF5 files

        --file-prefix
                    Prefix for generated HDF5 files

        --root-group
                    Name of the HDF5 group at the root of the file structure

        --timeout-sec
                    If no updates are received within timeout (in seconds), close the file and exit.
                    A value of 0 means wait forever

        --max-duration-sec
                    Maximum time, in seconds, to collect data for in a single HDF5 file. If 0, don't
                    limit files by time. Default: 0

        --max-size-mb
                    Maximum size, in MB, to collect data for in a single HDF5 file. If 0, don't
                    limit files by size. Default: 0

        --label-sep separator between PV name and column name in labels. Default: '.'
        --column-sep
                    separator between PV identifier and original column name. Default: '_'
```

This progam exits on any of these conditions:

* If output file already exists (error)
* After `timeout_sec` seconds since the last update
* If `input_pv` disconnects

The `input_pv` is assumed to conform to `TimeTable`. The resulting HDF5 structure is as follows:

```
/meta/                  Group
/meta/labels            Dataset<string>: the list of labels: e.g. ["secondsPastEpoch", "nanoseconds", "SIM:STAT:000.MIN", "SIM:STAT:000.MAX", ...].
/meta/columns           Dataset<string>: the list of columns: e.g. ["secondsPastEpoch", "nanoseconds", "pv000", "pv000_MIN", "pv000_MAX", ...].
/meta/pvxs_types        Dataset<uint8_t>: the list of PVXS type codes for each column: e.g. [46, 46, 75, 75, ...].
/meta/pvnames           Dataset<string>: the list of "signals": e.g. ["SIM:STAT:000", "SIM:STAT:001", ...]. Extracted from /meta/columns.
/meta/column_prefixes   Dataset<string>: the list of column prefixes: e.g. ["pv000", "pv001", ...]. Extracted from /meta/labels.

Note: the input PV NTTable shape can be reconstructed from /meta/{labels,columns,pvxs_types}

/data/                  Group
/data/secondsPastEpoch  Dataset<T>: timestamp for each row
/data/nanoseconds       Dataset<T>: timestamp for each row
/data/pv000/            Group
/data/pv000/MIN         Dataset<T>: data for the "MIN" column for pv000
/data/pv000/...         Dataset<T>: other data columns for pv000
/data/...               Groups for other signals
...
```

Datasets are created with chunk size set to the number of rows of the first update.