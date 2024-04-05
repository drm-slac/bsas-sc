<html lang="en">
<div id="pagehead">
    <a href="https://www.slac.stanford.edu/">
        <img alt="SLAC logo" src="https://www.slac.stanford.edu/grp/ad/model/images/SLAC-lab-hires.png" width="283"/>      
    </a>
</div>

<hr/>

# Beam Synchronous Acquisition Service
    
Doug Murray, SLAC, May 2022<br />
Revision 1.0, 04-May-2022, Initial Version.

<hr />

BSAS is the **Beam Synchronous Acquisition Service**, which acquires data synchronized with each beam pulse from devices in SLAC's LCLS Accelerator, and records it in files using the Hierarchical Data Format (HDF).

BSAS-SC refers to the system associated with the superconducting Linac (*LCLS-II*), while BSAS-NC is associated with the normal conducting Linac (*LCLS-I*).

## Installing

This software is typically installed on a supported Linux system at SLAC, such as a Redhat Enterprise Linux server, version 7 or higher.

> **NOTE:**
> This software will not currently build on Windows or standard MacOS systems having case-insensitive file systems.

### Development

Download the files to start development work.  Use **git** to retrieve the source code and documentation:

```shell
git clone --recursive https://github.com/drm-slac/bsas-sc.git
```

This will retrieve the software with standard EPICS directories already configured for use at SLAC.  The *recursive* flag is required because the HDF template library is included as a submodule.
The resulting directory is named *bsas-sc* by default and contains several entries:

<center><caption>*Table 1*.  **Files and Directories**</caption></center>

| File or Directory Name | Purpose |
|---:|:---|
| **commonApp**/     | Template and Code libraries used in more than one program |
| **configure**/     | Files used to build software in the EPICS environment |
| **documentation**/ | Documents in their raw format, used to build final documents in the *doc* directory, created when the **make** command runs |
| **highfiveApp**/   | The submodule suporting the HDF5 format for generated data files |
| Makefile           | Used by the **make** tool to build the software |
| **managerApp**/    | Software to manage the running processes which acquire or record the BSAS data |
| **mergerApp**/     | Software which acquires data from the EPICS IOCs and makes it available in a single, merged table |
| **nttableApp**/    | Software which support the NTTable type in EPICS v7, and provides a template for the merged output table's format |
| README.md          | This file |
| RELEASE_SITE       | Used by the EPICS system to define site-specific details of the build and operating environments at SLAC. |
| **test**/          | Contains several programs capable of testing various aspects of the EPICS environment |
| **writerApp**/     | Software which reads the merged NTTable from the **mergerApp**, either locally or across a network, and records that data in HDF5 format |

### Building

From any account having the standard SLAC development environment set, just type **make** to build the software.  The EPICS build system is used to build the software.

A *bin* directory will be created and software will be installed within an architecture specific subdirectory.  For example, on a Redhat Enterprise 7 machine, software will be installed under *bin/rhel7-x86_64/*.  On a Redhat 6 machine, the software will be placed under *bin/rhel6-x86_64/*.
A *doc* directory will also be created with documentation installed.

### Deploying / Publishing

The **mergerApp** and **writerApp** are primary components which are started and restarted as necessary by the **managerApp**.  When working on different host computers, as they typically are, the **mergerApp** and **writerApp** each have their own instance of the **managerApp** monitoring them.  If the merger and writer are working on the same host computer, for testing scenarios for example, a single manager instance can monitor them both.jj

## Features

The system can acquire data from several IOCs, gathered to one server then transmit that data to another computer to record the data is a specific format.

## Configuration

The **mergerApp**, **writerApp** and **managerApp** can be configured by editing a simple ascii file.
More details can be found in the **BSAS-SC Users Manual** located in the *doc* directory.
