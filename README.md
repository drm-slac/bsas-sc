# BSAS Manager Software
This **bsasManager** program is responsible for controlling the **Beam Synchronous Acquisition System** for superconducting beamline devices at SLAC.

This software is meant to manage the **Beam Synchronous Acquisition software**
(_BSAS_), specifically for the superconducting LCLS-II accelerator beamline. Meant to run continuously as a daemon process, it starts a **Merger** program to
acquire and coalesce data and a second process to record the resulting data.  The
acquisition software learns of the data to acquire from an input file which
names a set of NTTable PVs. It will gather data from those PVs and merge them
together into a larger table.  This larger, merged NTTable is then monitored by
a **Writer** process which records it in an hierarchical file format for offline
analysis.  Both programs can run on the same processor, but can also run on
separate computers.

At least one of the **merger** or **writer** applications shall be managed by this
program.  If the **merger** program is started, the input file containing the
NTTable PVs is constantly monitored for changes and the **merger** app restarted
as required.  If the **writer** program is started by itself, the input file is
not monitored for changes.

This software is intended to run on Linux.

The **merger** software ignores PVs from the input list which are unresponsive and
will exit when given a termination signal.  The **writer** software will wait for
the merged NTTable PV to become available then exit when that PV disconnects,
typically when the **merge** application exits.
After starting the **merger** application, this program is responsible for
monitoring the input file containing the NTTable PVs to acquire.  When it finds
a change, it stops the **merger** software and restarts it, as well as the **writer**
application if it was requested.

Recording its actions in an audit (log) file is an important part of this software, but only 2 weeks
of audit files are maintained along with the current one.

## bsasManager.py Usage
The bsasManager software uses defaults for most settings, which will be incorrect
for anything except use with the superconducting hard x-ray line instrumentation.

At least one of the **-m** or **-w** options must be set because the _bsasManager_ must start one or both of the **merger** or **writer** software.  Also, the **data** and **lock**
directories specified with the **-D** and **-L** options respectively, must exist
prior to starting the software, the _bsasManager_ will not create them.  It will
create everything within those directories.

There is a special testing option available for the audit log; if **-l** is given
the value of "**-**", messages will be sent to the standard output rather than a
disk file.

### Command Option Summary

| **Short Option** | **Long Option** | **Parameter** | **Purpose** | **Default Value** |
|--:|:--|:--|---|---|
|-d| --dir       | directory       | Directory from which BSAS will work | '.' (current directory) |
|-f| --pvfile    | filename        | File containing input NTTable PV Names | pvlist |
|-o| --output    | PV name         | Name of Generated NTTable PV | SC-HXR-TBL |
|-m| --merger    |                 | start and manage the Merger software on this computer | do not start |
|-w| --writer    |                 | start and manage the Writer software on this computer | do not start |
|-t| --target    | dataset name    | target Beamline name | SC-HXR |
|-l| --logfile   | logfile name    | Name of daily audit log file | ./LOGS/SC-HXR.txt |
|-s| --severity  | info or debug   | level of audit file output: info or debug  | info |
|-P| --period    | seconds         | Number of seconds between input file checks | 30 |
|-G| --grace     | seconds         | Number of seconds after last input file modification before merger is signalled | 30 |
|-D| --datadir   | directory       | Directory to place data subdirectories | ./DATA |
|-L| --lockdir   | directory       | Directory to place software lock files | ./LOCKS |
|-M| --mergerApp | path to program | location of merger application software | from PATH |
|-W| --writerApp | path to program | location of writer application software | from PATH |

### Examples
The following command will attempt to start the BSA Service from /tmp/bsaTest, using only the Merger software:
	`bsasManager.py -m -d /tmp/bsaTest `

This next command will start both Merger and Writer applications on the current computer, with audit log messages coming to the standard output:
	`bsasManager.py -m -w -l- `

The following (lengthy) command can be used to acquire and store data related to the soft x-ray line:
	`bsasManager.py -d /nfs/slac/g/bsd/BSAService -f ./SC-SXR.pvlist -o SC-SXR-TBL -t SC-SXR -l ./LOGS/SC-SXR.txt -D ./DATA -L ./LOCKS -M '/afs/slac/g/lcls/package/bsasMerger' -W '/afs/slac/g/lcls/package/bsasWriter' --severity=info --period=10 --grace=120`

