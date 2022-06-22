# BSAS Manager Software
This **bsasManager** program is responsible for controlling the **Beam Synchronous Acquisition System** at SLAC.

This software is meant to manage the **Beam Synchronous Acquisition software**
(_BSAS_). Meant to run continuously as a daemon process, it starts a **Merger** program to
acquire and coalesce data and another to record the resulting data.  The
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

Recording its actions is an important part of this software, but only 2 weeks
of audit files are maintained along with the current one.

### bsasManager.py Usage
| **Command Line Option** | **Long Option Name** | **Required** | **Purpose** | **Default Value** |
|--:|:--|:-:|:--|---|
|-f| --pvfile | Yes | File containing NTTable PV Names |  |
|-o| --output | Yes | Name of Generated NTTable PV |  |
|-t| --target | Yes | target Beamline name |  |
|-D| --datadir | Yes | Directory to place data subdirectories |  |
|-L| --lockdir | Yes | Directory to place software lock files |  |
|-l| --logfile | No |  Name of daily log (audit) file | Standard output |
|-m| --merger | No |  location of merger application software | from PATH |
|-w| --writer | No |  location of writer application software | from PATH |
|-s| --severity | No |  level of audit file output: info or debug  | info |
|-P| --period | No |  Number of seconds between input file checks | 30 |
|-G| --grace | No |  Number of seconds after last input file modification before merger is signalled | 30 |

