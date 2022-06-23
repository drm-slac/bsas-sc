#!/usr/bin/python3

"""
This software is meant to manage the Beam Synchronous Acquisition software
(BSAS). Meant to run continuously as a daemon process, it starts a program to
acquire and coalesce data and another to record the resulting data.  The
acquisition software learns of the data to acquire from an input file which
names a set of NTTable PVs. It will gather data from those PVs and merge them
together into a larger table.  This larger, merged NTTable is then monitored by
a Writer process which records it in an hierarchical file format for offline
analysis.  Both programs can run on the same processor, but can also run on
separate computers.

At least one of the merger or writer applications shall be managed by this
program.  If the merger program is started, the input file containing the
NTTable PVs is constantly monitored for changes and the merger app restarted
as required.  If the writer program is started by itself, the input file is
not monitored for changes.

This software is intended to run on Linux

The Merge software ignores PVs from the input list which are unresponsive and
will exit when given a termination signal.  The Writer software will wait for
the merged NTTable PV to become available then exit when that PV disconnects,
typically when the Merge application exits.

After starting the Merger application, this program is responsible for
monitoring the input file containing the NTTable PVs to acquire.  When it finds
a change, it stops the Merger software and restarts it, as well as the Writer
application if it was requested.

Recording its actions is an important part of this software, but only 2 weeks
of log files are maintained along with the current logfile.
"""

import os
import sys
import time
import uuid
import signal
import socket
import datetime
import threading
import subprocess

import logging
import logging.handlers

#
# command line options are preferred to environment variables
#
def commandOptions( argv: list) -> list:
        """ Retrieve command line options then return the parsed arguments """

        from argparse import ArgumentParser
        logLevels = [ 'debug', 'info', 'warning', 'error', 'critical' ]

        cmdLine = ArgumentParser( description='Manage BSA Service')

        #
        # basic arguments
        #
        cmdLine.add_argument( '-d', '--dir',       dest='workingDir',     metavar='<BaseWorkingDirectory>',
                                default='.',
                                help='Directory from which BSAS will operate')
        cmdLine.add_argument( '-f', '--pvfile',    dest='inputFile',     metavar='<PVListFile>',
                                default='pvlist',
                                help='file containing input PV table names')
        cmdLine.add_argument( '-o', '--output',    dest='mergedPVName',  metavar='<MergedPVName>',
                                default='SC-HXR-TBL',
                                help='name of the merged NTTable PV made available to record.')
        cmdLine.add_argument( '-t', '--target',    dest='targetBeamline',metavar='<TargetBeamlineName>',
                                default='SC-HXR',
                                help='name of the target to which beam is being directed')
        cmdLine.add_argument( '-D', '--datadir',   dest='dataDirName',   metavar='<OutputDataDirectory>',
                                default='./DATA',
                                help='name of directory where data will be stored by date')
        cmdLine.add_argument( '-L', '--lockdir',   dest='lockDirName',   metavar='<LockFileDirectory>',
                                default='./LOCKS',
                                help='name of directory where lock files will be kept')

        #
        # at least one of these arguments must be present
        #
        cmdLine.add_argument( '-m', '--merger',    dest='useMerger',
                                action='store_true',
                                help='name of the process which merges PV tables')
        cmdLine.add_argument( '-w', '--writer',    dest='useWriter',
                                action='store_true',
                                help='name of the process which saves the merged PV table')
        cmdLine.add_argument( '-M', '--mergerApp', dest='mergerApp',     metavar='<MergerProgram>',
                                default=None,
                                help='name of the process which merges PV tables')
        cmdLine.add_argument( '-W', '--writerApp', dest='writerApp',     metavar='<WriterProgram>',
                                default=None,
                                help='name of the process which saves the merged PV table')

        #
        # log messages go to stdout if no log file specified
        #
        cmdLine.add_argument( '-l', '--logfile',   dest='logFile',       metavar='<LogFileName>',
                                default='./LOGS/SC-HXR.txt',
                                help='name of logfile to record daily progress messages')
        cmdLine.add_argument( '-s', '--severity',  dest='logSeverity',   metavar='<SeverityOfLogMessages - debug or info>',
                                choices=logLevels,
                                default='info',
                                help='level of detail of progress messages to be recorded')

        #
        # determine how often to check for changes to the input PV list or directory service
        #
        cmdLine.add_argument( '-P', '--period',    dest='timeBetweenChecks',     metavar='<SecondsBetweenInputChangeChecks>',
                                type=int,
                                default=30,
                                help='seconds to wait between checks of input file changes')
        cmdLine.add_argument( '-G', '--grace',     dest='editGracePeriod',       metavar='<SecondsAfterLastFileChangeToRestart>',
                                type=int,
                                default=30,
                                help='grace period in seconds to allow input file editing')

        #
        # arguments are not validated here because log messages are not yet available
        #
        return cmdLine.parse_args()

#
# set up logging to stdout or daily logfiles,
# removing those older than 14 days
# 
def beginLogging( options: list) -> logging.Logger:
        """ Set up logging features and return a custom logging object

        Adjust the severity level expected of messages
        Send messages to standard output unless logfile prefix was supplied
        Logfiles will be stopped at midnight and moved to a file having the date's suffix
        Only 14 logfiles will be maintained in addition to the current one, others are removed
        All messages will contain the data and time the message was logged, along with the severity
        The custom logging object is returned
        """
        #
        # Check for desired severity of output log
        # messages, which are all uppercase names.
        #
        logSeverity = getattr( logging, options.logSeverity.upper())

        newLog = logging.getLogger( __name__)
        newLog.setLevel( logSeverity)
        delayError = False;

        #
        # Log to standard output if no logfile specified.  If specified, the
        # logfile will record messages and switch to a new logfile every day,
        # keeping 2 weeks of files then removing outdated ones.
        #
        if options.logFile == None or options.logFile == '-':
                handler = logging.StreamHandler()
        else:
                try:
                        handler = logging.handlers.TimedRotatingFileHandler( filename=options.logFile, when='midnight', interval=1, backupCount=14)
                except:
                        #
                        # don't print an error message yet
                        #
                        handler = logging.StreamHandler()
                        delayError = True;

        #
        # Set message format to include date and time, severity and message text
        #
        _mesgFormat = '%(asctime)s | %(levelname)9.9s | %(message)s'
        _mesgDateFormat = '%Y-%m-%d %H:%M:%S'
        _format = logging.Formatter( _mesgFormat, _mesgDateFormat)

        #
        # setup and install the handler
        #
        handler.setFormatter( _format)
        handler.setLevel( logSeverity)

        newLog.addHandler( handler)

        if logSeverity != logging.INFO:
                newLog.debug( f'Log messages with severity of {logSeverity} ({options.logSeverity}) or higher will be recorded.')

        if delayError:
                newLog.critical( f'Cannot create Log File. Directory is not accessible to allow creation of {options.logFile}.')
                sys.exit( 3)

        return newLog

#
# There is the possibility of a race condition here
#
def stopIfAlreadyWorking( options: list, logger: logging.Logger):
        """ Check to see if another instance of this program is already running

        The problem is that several instances of this program are expected to
        run, one for each Linac and destination beamlines to monitor.  The
        instance we need to guard against is another one naming the same target beamline.
        """

        #
        # build a lock file name using only the target beam line name and the
        # current machine's host name. The same process running on another host
        # is not easily checked because the file system is shared and could be
        # just an instance of a writer with no merger process.
        #
        hostName = socket.gethostname();
        lockFileName = options.lockDirName + '/' + options.targetBeamline + '-' + hostName + '.lock'

        logger.debug( f'Checking for another running BSAS manager process on {hostName} by checking "{lockFileName}".')

        existingPid = None
        lockFileFound = False

        #
        # Check for existing lock file.  Ideally, one will always be found
        # for a specific beamline on this host because they aren't removed.
        #
        try:
                with open( lockFileName, 'r') as fp:
                        pidText = fp.readline()
                        existingPid = int( pidText)
                        lockFileFound = True

        except FileNotFoundError:
                logger.debug( f'No lock file, Creating "{lockFileName}" now.')

        except Exception as e:
                logger.debug( 'Cannot access previous lock file "{}": error {} ({})'.format( lockFileName, e, repr( e)))
                logger.info( 'Problem encountered while checking for existing process. Assuming it is safe to proceed.')

        #
        # lock file was found, continue only if the
        # process indicated by the PID contained in
        # the file is no longer running.
        #
        if lockFileFound:
                lockTime = os.path.getmtime( lockFileName)
                timeSpan = datetime.datetime.fromtimestamp( lockTime)
                try:
                        os.kill( existingPid, 0)
                        logger.critical( 'Another instance of this software has been working for {}. Stopping here to avoid problems.'.format( timeSince( timeSpan)))
                        sys.exit( 2)

                except OSError as e:
                        previousLockTime = time.strftime( '%Y-%m-%d %H:%M:%S', time.localtime( lockTime))
                        logger.debug( f'The previous BSAS process ({existingPid}) started {previousLockTime} but is no longer running.  Continuing.')

                except Exception as e:
                        #
                        # it is possible the effectuve UID of the current
                        # process doesn't have permission to send this signal.
                        # In such a case, we wouldn't want to continue anyway.
                        #
                        logger.debug( 'Problem encountered while trying to stop process {} ({})'.format( e, repr( e)))
                        logger.critical( 'It seems this software has already been working for {}. Stopping now to avoid problems.'.format( timeSince( timeSpan)))
                        sys.exit( 2)

        #
        # To arrive at this point, whether there
        # is a lock file or not, we can proceed.
        #
        try:
                with open( lockFileName, 'w') as fp:
                        fp.write( str( os.getpid()))

        except Exception as e:
                logger.debug( 'Problem encountered while trying to create lock file ({lockFileName}).  Error {} ({})'.format( e, repr( e)))
                logger.warning( 'A lock file could not be created, the possibility exists that another BSAS manager could interfere.')

        return

#
# Start the software
# Watch for changes
#
class Watcher():

        """ A Class to monitor a list of PVs contained in a file

        Check the PV list for changes to the file's modification time on a
        regular basis. Check PVs when the file is updated and restart the
        acquisition and recording software if they have changed.  A SIGALRM
        signal is sent at an optional time interval and a signal.pause() call
        in the main routine will return, then a method is called to inspect the
        changes.

        """

        def __init__( self, options: list, logger: logging.Logger):
                """ Constructor; prepare to watch for changes

                set up signal handlers, validate arguments, check for an input file and set the first alarm
                """
                signal.signal( signal.SIGALRM, self._alarmTriggered)
                signal.signal( signal.SIGTERM, self._askedToQuit)
                signal.signal( signal.SIGINT, self._askedToQuit)
                signal.signal( signal.SIGHUP, self._askedToQuit)

                self._unableToContinue = False
                self._previousPVSet = set()
                self._mergerAppPath = None
                self._writerAppPath = None
                self._mergerThread = None
                self._writerThread = None
                self._askedToStop = False

                self._opt = options
                self._log = logger

                self._workingDir = self._opt.workingDir

                #
                # Change directory early so relative paths will be recognized.
                #
                try:
                        os.chdir( self._opt.workingDir)
                        self._log.info( f'Ready to work from "{self._opt.workingDir}".')
                except OSError as e:
                        logger.critical( f'Unable to work in the directory named {self._opt.workingDir}: {e}.')
                        self._unableToContinue = True

                #
                # Validate arguments here now that logging is available.
                # Ensure access to software for merging and saving NTTable PVs.
                # One can specify that the merge feature needs to exist on
                # one machine, while the write will execute on another
                # Don't quit after finding one issue, try and report all of
                # them as early as possible.
                #
                if self._opt.useMerger and self._opt.mergerApp == None:
                        self._opt.mergerApp = 'mergerApp'

                if self._opt.useWriter and self._opt.writerApp == None:
                        self._opt.writerApp = 'writerApp'

                #
                # Specifying the software location
                # implies the software needs to be used
                #
                if self._opt.mergerApp != None:
                        self._opt.useMerger = True
                        self._mergerAppPath = self._findSoftwareLocation( self._opt.mergerApp, description='merger')
                        if self._mergerAppPath == None:
                                self._log.critical( 'The merge software has been requested but it cannot be found.')
                                self._unableToContinue = True
                        else:
                                self._opt.mergerApp = os.path.basename( self._mergerAppPath)
                                self._log.debug( f'The {self._opt.mergerApp} software was successfully located.')

                if self._opt.writerApp != None:
                        self._opt.useWriter = True
                        self._writerAppPath = self._findSoftwareLocation( self._opt.writerApp, description='writer')
                        if self._writerAppPath == None:
                                self._log.critical( 'The writer software has been requested but it cannot be found.')
                                self._unableToContinue = True
                        else:
                                self._opt.writerApp = os.path.basename( self._writerAppPath)
                                self._log.debug( f'The {self._opt.writerApp} software was successfully located.')

                if not self._opt.useMerger and not self._opt.useWriter:
                        self._log.critical( 'Neither the merger nor the writer software have been specified.  At least one must be provided.')
                        self._unableToContinue = True

                if self._opt.targetBeamline == None:
                        self._log.critical( 'The name of the PV to be recorded cannot be used.')
                        self._unableToContinue = True

                #
                # the given DATA and LOCK directories must already exist.  The
                # Writer app will create all DATA directories, but we want the
                # user to explicitly create it here, to avoid typos sending
                # data to some other place.  The LOCK directory is only used by
                # instances of this process on different machines, probably
                # using a distributed filesystem.
                # Error messages are generated within that function.
                #
                if not self._checkForAccessibility( dirName=self._opt.dataDirName, dirPurpose='data will be recorded'):
                        self._unableToContinue = True

                if not self._checkForAccessibility( dirName=self._opt.lockDirName, dirPurpose='Lock files will be kept'):
                        self._unableToContinue = True

                #
                # Finally deal with any critical problems
                #
                if self._unableToContinue:
                        self._askedToQuit( signum=-1, frame=None)
                        return

                #
                # At this point, we know that the merger, writer or both have
                # been requested, and executables have been found.
                # Check if the writer is the only software requested to start
                #
                if not self._opt.useMerger and self._opt.useWriter:
                        #
                        # no need to start watching the input file
                        # for changes, just watch the writer process
                        # and start it now.
                        #
                        self._startWriterProcess()
                        return

                #
                # enforce a check-up time between 1 second and 6 hours
                #
                requestedTime = self._opt.timeBetweenChecks
                if requestedTime <= 0:
                        self._opt.timeBetweenChecks = 1
                elif requestedTime > 6 * 60 * 60:
                        self._opt.timeBetweenChecks = 6 * 60 * 60

                if requestedTime != self._opt.timeBetweenChecks:
                        self._log.warning( f'Time between file checks was changed from {requestedTime} seconds to {self._opt.timeBetweenChecks} seconds.')

                #
                # enforce a time to edit of 5 seconds to 5 minutes
                #
                requestedTime = self._opt.editGracePeriod
                if requestedTime < 5:
                        self._opt.editGracePeriod = 5
                elif requestedTime > 5 * 60:
                        self._opt.editGracePeriod = 5 * 60

                if requestedTime != self._opt.editGracePeriod:
                        self._log.warning( f'Grace time between file saves when editing was changed from {requestedTime} seconds to {self._opt.editGracePeriod} seconds.')

                self._log.debug( f'Only restarting daemons after {self._opt.editGracePeriod} seconds of final file edits.')
                return

        def startWatching( self):
                """ Check for the required input file but wait if it doesn't yet appear.

                This can change to a directory service mechanism in the future.
                Only start watching if the merger softwarehas been requested.
                """

                if not self._opt.useMerger and self._opt.useWriter:
                        self._log.info( 'The input file is not used when only the Writer software was requested.')
                        return

                #
                # Determine when the input file was last changed,
                # and take a guess if the file doesn't exist yet.
                #
                self._lastUpdateTime = 0
                self._editSessionFinished = True
                try:
                        pvListExists = os.path.exists( self._opt.inputFile)
                        if pvListExists == False:
                                self._log.debug( f'Cannot access the file containing the PV list ({self._opt.inputFile}).  Waiting for it.')
                                self._lastModTime = time.time()
                        else:
                                self._lastModTime = os.path.getmtime( self._opt.inputFile)
                                if self._opt.logSeverity == 'debug':
                                        _timeLastChanged = time.strftime( '%Y-%m-%d %H:%M:%S', time.localtime( self._lastModTime))
                                        self._log.debug( f'Found PV list file from command line: {self._opt.inputFile} was last changed {_timeLastChanged}')
                except:
                        self._log.debug( f'PV file exception from path.exists( {self._opt.inputFile}).')

                #
                # take a first look then schedule subsequent checks
                #
                self._alreadyNotedFileMissing = False
                self.lookForChanges( firstLook=True)
                signal.alarm( self._opt.timeBetweenChecks)
                return

        def _askedToQuit( self, signum: int, frame: object):
                """ Handler for various methods of termination including asynchronous signals """

                runTime = timeSinceStart()

                #
                # This version of python is missing the signal.strsignal() method
                #       self._log.debug( 'Received signal {} ({}).'.format( signum, signal.strsignal( signum)))
                #
                if signum < 0:
                        #
                        # already presented info level message for internal errors
                        #
                        self._log.debug( f'STOPPING after {runTime}: quit because of software error. Cannot continue.')
                else:
                        self._log.info( f'STOPPING after {runTime}: Requested to quit. Interrupted by signal ({signum}).')

                #
                # let the threads know that they don't want to keep their
                # processes alive, then terminate those processes.  Attempt
                # to join both then exit.
                #
                self._askedToStop = True

                if self._mergerThread != None:
                        self._mergerThread.signalProcess()
                if self._writerThread != None:
                        self._writerThread.signalProcess()

                if self._mergerThread != None:
                        self._mergerThread.join( 30)
                if self._writerThread != None:
                        self._writerThread.join( 30)

                sys.exit( 1)

        def _alarmTriggered( self, signum: int, frame: object):
                """ Interrupt handler for regular alarm wakeup call
                Meant to be a minimal method, work is done later in
                lookForChanges.  Here we field the alarm signal. The
                signal.pause() in the main routine will call lookForChanges(),
                We only need to schedule the next alarm here
                """

                #
                # get the most recent modification time of the file
                #
                if self._opt.logSeverity == 'debug':
                        _timeLastChanged = time.strftime( '%Y-%m-%d %H:%M:%S', time.localtime( self._lastModTime))
                        self._log.debug( f'No change to PV list {self._opt.inputFile}: last modified {_timeLastChanged}')

                #
                # wake ourselves up from pause() after the
                # required delay, then go back to pausing
                #
                signal.alarm( self._opt.timeBetweenChecks)
                return

        def _checkForAccessibility( self, dirName: str, dirPurpose: str) -> bool:
                """ Check that the named directory already exists and is available for creating files.
                """

                try:
                        fname = dirName + '/' + str( uuid.uuid4())
                        with open( fname, 'w') as f:
                                f.write( "testing accessibility")
                        os.remove( fname)
                except Exception as e:
                        self._log.critical( f'The directory in which {dirPurpose} is not accessible for our purposes ({dirName}).')
                        self._log.debug( 'Runtime exception from open/write/remove when checking accessibility of {}: {}.'.format( dirName, repr( e)))
                        return False
                return True

        def _findSoftwareLocation( self, softwareToFind: str, description: str) -> str:
                """ Check for named software in current PATH

                Check to see if the named program exists and is executable
                directly, or from within the PATH environment variable
                """
                from shutil import which

                foundSoftware = None
                try:
                        foundSoftware = which( softwareToFind)
                        if foundSoftware != None:
                                self._log.debug( f'Found {description} software at {foundSoftware}')
                        else:
                                self._log.debug( f'No {description} software ({softwareToFind}) found in current paths')
                except:
                        self._log.critical( f'Software error; Unable to find {description} software.')

                return foundSoftware

        def lookForChanges( self, firstLook: bool):
                """ Check the modification time of the input file as a first check for changes.

                Called after an alarm signal was received, but not within the
                signal interrupt method.  A grace period is granted, so
                multiple changes to the file within a reasonable window are
                ignored before detailed changes are checked.
                """

                #
                # get the time of the most recent change to the PV list, but if
                # the file is inaccessible, keep using the current list until
                # it is available again.
                #
                try:
                        self._newModTime = os.path.getmtime( self._opt.inputFile)
                except:
                        if self._alreadyNotedFileMissing == False:
                                if firstLook:
                                        self._log.warning( f'The input file containing PV names ({self._opt.inputFile}) is not currently available.  Waiting for it.')
                                else:
                                        self._log.warning( f'Can no longer access the file containing PV names ({self._opt.inputFile}).  Waiting for it to become accessible.')
                                #
                                # only display this error once
                                # until the file is accessible
                                #
                                self._alreadyNotedFileMissing = True
                        return

                #
                # if we were unable to access the file, we
                # know at this point it is accessible again
                #
                if self._alreadyNotedFileMissing == True:
                        self._log.info( f'The file containing PV names ({self._opt.inputFile}) is now accessible.  Watching for changes.')
                        self._alreadyNotedFileMissing = False

                #
                # If the file hasn't changed, check to see if it was being
                # updated earlier and enough time has passed
                #
                if self._newModTime == self._lastModTime:
                        if self._editSessionFinished == True and time.time() > self._lastUpdateTime + self._opt.editGracePeriod:
                                self._editSessionFinished = False
                                self._checkForChangedPVs()
                        return

                #
                # At this point, we know the file has been modified, but let's not
                # restart software with each write during an editing session.
                #
                if self._newModTime >= self._lastModTime + self._opt.editGracePeriod:
                        self._editSessionFinished = False
                        self._checkForChangedPVs()
                else:
                        #
                        # not enough time has passed, just mark
                        # as a part of a larger editing session
                        #
                        if self._opt.logSeverity == 'debug':
                                _timeLastChanged = time.strftime( '%Y-%m-%d %H:%M:%S', time.localtime( self._lastModTime))
                                self._log.debug( f'File with list of PVs has changed at {_timeLastChanged}.  Waiting for edits to finish.')
                        self._editSessionFinished = True
                        self._lastUpdateTime = self._newModTime 

                self._lastModTime = self._newModTime 
                return

        def _checkForChangedPVs( self):
                """ Input file has been modified, report on specific changes and restart software

                Called after the the file's modification time
                has changed, within a grace period for editing
                """

                _timeLastChanged = time.strftime( '%Y-%m-%d %H:%M:%S', time.localtime( self._lastModTime))
                self._log.info( f'The list of PVs was modified on {_timeLastChanged}. Checking for changes to the list.')

                #
                # get the new PV list, strip off newlines
                #
                try:
                        fp = open( self._opt.inputFile)
                        PVList = fp.readlines()
                except:
                        self._log.warning( f'Input file ({self._opt.inputFile}) cannot be accessed. Attempting to continue with previously used PVs.')
                        fp.close()
                        return
                fp.close()

                #
                # if the file is empty, try to continue
                # with the existing list
                #
                if len( PVList) == 0:
                        self._log.warning( f'No PVs found in file ({self._opt.inputFile}). Attempting to continue with previously used PVs.')
                        return

                #
                # Get rid of extra white space, then check for duplicate PVs.
                #
                PVList = [pv.strip() for pv in PVList]
                dups = list()
                for singlePV in PVList:
                        if PVList.count( singlePV) > 1 and dups.count( singlePV) < 1:
                                dups.append( singlePV)
                for singlePV in dups:
                        self._log.warning( f'The PV named {singlePV} has duplicate entries in the PV list ({self._opt.inputFile})')
                        
                #
                # Convert the list to a set to determine changes.
                #
                newPVset = set( PVList)

                if self._opt.logSeverity == 'debug':
                        plen = len( newPVset)
                        self._log.debug( f'Found set of {plen} PVs.')
                        for pv in newPVset:
                                self._log.debug( f'        found PV {pv}')

                #
                # The exclusive-or (^) of the old and new sets will contain the differences
                #
                changedPVSet = newPVset ^ self._previousPVSet
                numChanges = len( changedPVSet)

                self._previousPVSet = newPVset

                if numChanges == 0:
                        self._log.info( f'The contents of the PV list used by the Merger software has not changed.')
                        return

                self._log.info( f'The PV list used by the Merger software has {numChanges} changes:')
                for pv in changedPVSet:
                        if pv in newPVset:
                                self._log.info( f'  The PV named {pv} has been ADDED')
                        else:
                                self._log.info( f'  The PV named {pv} has been REMOVED')

                #
                # If this is the first change notice, then this program has
                # just started, so the merger and writer are started here.  If
                # only the writer was requested (i.e. the merger is running on
                # another machine), then it was started earlier in the
                # constructor and the PV list input file is ignored.
                #
                # At this point, we know that (1) the merger has been
                # requested, possibly with a writer, and (2) the contents of
                # the input file has changed from its previous state, or (3)
                # the contents have been seen for the first time.
                #
                # If the merger software is already working, we need to restart
                # only it.  A writer process running on this same or any other
                # machine will stop when the NTTable it is monitoring
                # disconnects, then the thread in which it was started will
                # restart it.
                #
                if self._opt.useMerger:
                        if self._mergerThread == None:
                                self._startMergerProcess()
                        else:
                                self._mergerThread.signalProcess()

                if self._opt.useWriter:
                        if self._writerThread == None:
                                self._startWriterProcess()
                return

        #
        # internal class to manage subprocesses
        # with the ability to send them signals.
        # the parent argument is given explicitly.
        #
        class _BSAThread( threading.Thread):
                """ Inner class to manage separate threads, each of which will start a subprocess
                """
                def __init__( self, parent, argList):
                        threading.Thread.__init__( self)

                        self._parent = parent
                        self._argList = argList
                        self._processHandle = None
                        self._mutex = threading.Lock()

                        self._parent._log.debug( f'Setting up the {argList[0]} software management thread')
                        return

                #
                # Common thread method to start subprocesses
                #
                def run( self):
                        """ This is the common thread code which starts a new process
                        """

                        processName = self._argList[0]
                        processArgs = self._argList[1]

                        totalProcRunTime = datetime.timedelta()
                        numStarts = 0

                        self._parent._log.debug( f'NEW THREAD: Starting the {processName} software:')
                        if self._parent._opt.logSeverity == 'debug':
                                i = 0
                                for arg in processArgs:
                                        self._parent._log.debug( f'   {processName} ARG-> {i}:{arg}')
                                        i += 1

                        #
                        # keep this subprocess running for as long
                        # as the manager is not being asked to stop
                        #
                        while not self._parent._askedToStop:

                                self._parent._log.info( 'STARTING the {} software at {} into this management session.'.format( processName.upper(), timeSinceStart()))

                                procStartTime = datetime.datetime.now()
                                try:
                                        self._processHandle = subprocess.Popen( processArgs, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, universal_newlines=True)

                                        numStarts += 1
                                        for logMesg in self._processHandle.stdout:
                                                logStr = logMesg.rstrip()
                                                self._parent._log.debug( f'[{processName}]:{logStr}')
                                except:
                                        self._parent._log.error( f'Unable to start the {processName} software.  Will try again in 1 minute.')

                                        #
                                        # sleep won't be interrupted
                                        # by signal to quit
                                        #
                                        for t in range( 0, 60, 2):
                                                time.sleep( 2)
                                                if self._parent._askedToStop:
                                                        break
                                        continue

                                self._processHandle.wait()

                                #
                                # A possible race condition here with signalProcess.
                                # We'll block here if in the midst of a termination signal.
                                #
                                self._mutex.acquire()
                                self._processHandle = None
                                self._mutex.release()

                                diffTime = datetime.datetime.now() - procStartTime

                                if self._parent._askedToStop:
                                        self._parent._log.info( 'The {} process has stopped after working for {}.'.format( processName, duration( diffTime)))
                                        continue

                                totalProcRunTime += diffTime
                                totalSeconds = totalProcRunTime.total_seconds()

                                if numStarts >= 3 and totalSeconds < 120:
                                        self._parent._log.error( 'The {} software has quit after working for {}.  That makes {} times in {}.  Waiting 5 minutes to try again.'.format( processName, duration( diffTime), numStarts, duration( totalProcRunTime)))

                                        #
                                        # reset the counters
                                        #
                                        totalProcRunTime = datetime.timedelta()
                                        numStarts = 0

                                        #
                                        # sleep won't be interrupted
                                        # by signal to quit
                                        #
                                        for t in range( 0, 5 * 60, 2):
                                                time.sleep( 2)
                                                if self._parent._askedToStop:
                                                        break
                                        continue

                                self._parent._log.info( 'The {} software has quit after working for {}.  Restarting in 10 seconds, {} into this session.'.format( processName, duration( diffTime), timeSinceStart()))
                                time.sleep( 10)
                                for t in range( 0, 10, 2):
                                        time.sleep( 2)
                                        if self._parent._askedToStop:
                                                break

                        return

                def signalProcess( self):
                        """ Send the terminate signal to the process started by this thread, which our run method will then restart
                        """

                        #
                        # Race condition.
                        # Guard against reseting the processHandle before
                        # we can use it to signal.  This is only likely to
                        # happen if the subprocess stops prematurely on its
                        # own, since normally the handle would only be reset
                        # as a result of this termination signal.
                        #
                        self._mutex.acquire()
                        if self._processHandle != None:
                                try:
                                        self._processHandle.send_signal( signal.SIGTERM)
                                except OSError as e:
                                        self._parent._log.debug( 'Cannot terminate a process: Error {}: {}'.format( e, repr( e)))
                        else:
                                self._parent._log.debug( 'The process has already stopped, no need to send signal.')
                        self._mutex.release()

                        return

        def _startMergerProcess( self):
                """ Start a thread to manage the Merger process
                A thread is started which will spawn the Merger process to
                acquire IOC NTTable PVs and merge them into a single table.
                When the process stops for any reason, the thread will attempt
                to restart and the process will reread its input list of PVs to
                merge.
                """
                mergerArgs = [
                                f'{self._mergerAppPath}',
                                '--pvlist',
                                f'{self._opt.inputFile}',
                                '--period-sec',
                                '1',
                                '--timeout-sec',
                                '30',
                                '--label-sep',
                                '.',
                                '--column-sep',
                                '_',
                                '--pvname',
                                f'{self._opt.mergedPVName}'
                                ]

                self._log.debug( 'Starting the thread to manage the merger software.')

                try:
                        self._mergerThread = self._BSAThread( self, argList=( self._opt.mergerApp, mergerArgs, ))
                        self._mergerThread.start()
                except Exception as e:
                        self._log.debug( 'Merger thread exception;', e)
                        self._log.error( 'Cannot start a new thread to manage the Merger software.')
                        time.sleep( 10)
                        self._mergerThread = None
                return

        def _startWriterProcess( self):
                """ Start the Writer process to acquire the merged NTTable PV and write it.
                """
                writerArgs = [
                                f'{self._writerAppPath}',
                                '--input-pv',
                                f'{self._opt.mergedPVName}',
                                '--base-directory',
                                f'{self._opt.dataDirName}',
                                '--file-prefix',
                                f'{self._opt.targetBeamline}',
                                '--root-group',
                                f'{self._opt.targetBeamline}',
                                '--timeout-sec',
                                '0',
                                '--max-duration-sec',
                                '300',
                                '--max-size-mb',
                                '200',
                                '--label-sep',
                                '.',
                                '--column-sep',
                                '_'
                                ]

                self._log.debug( 'Starting the thread to manage the writer software.')

                try:
                        self._writerThread = self._BSAThread( self, argList=( self._opt.writerApp, writerArgs, ))
                        self._writerThread.start()
                except Exception as e:
                        self._log.debug( 'Writer thread exception;', e)
                        self._log.error( 'Cannot start a new thread to manage the Writer software.')
                        time.sleep( 10)
                        self._writerThread = None
                return

def printStartMessage( options, logger):
        """ Print a unique log message when the server starts

        The intent is to provide an easily distinguished message
        within the logfile each time this process starts.
        """
        interval = duration( diff=datetime.timedelta( seconds=options.timeBetweenChecks))
        logger.info( '-------------------')

        suffix = ''
        if not options.useMerger and options.useWriter:
                suffix = ', starting only the Writer software locally'
        else:
                suffix = ', checking {} every {}'.format( options.inputFile, interval)

        logger.info( 'STARTING: Managing BSAS Software as "{}"{}'.format( os.path.basename( sys.argv[0]), suffix))
        return

def timeSinceStart() -> str:
        """ Find the time since this software started and return as a readable string
        """
        return timeSince( StartTime)

def timeSince( startingTime: datetime.datetime) -> str:
        """ Find the time that has passed since the given start time
        """
        stopTime = datetime.datetime.now()
        return duration( stopTime - startingTime)

def duration( diff: datetime.timedelta) -> str:
        """ Build a text string containing a formatted amount of time duration
        """
        secondsPerMinute = 60
        secondsPerHour = 60 * 60
        secondsPerDay = 60 * 60 * 24

        numSeconds = int( diff.total_seconds())

        numDays, numSeconds = divmod( numSeconds, secondsPerDay)
        numHours, numSeconds = divmod( numSeconds, secondsPerHour)
        numMinutes, numSeconds = divmod( numSeconds, secondsPerMinute)

        interval = ''
        if numDays > 0:
                interval += f'{numDays} day{"s" if numDays != 1 else ""} '
        if numHours > 0:
                interval += f'{numHours} hour{"s" if numHours != 1 else ""} '
        if numMinutes > 0:
                interval += f'{numMinutes} minute{"s" if numMinutes != 1 else ""} '
        if numSeconds > 0:
                interval += f'{numSeconds} second{"s" if numSeconds != 1 else ""}'
        if interval == '':
                interval = '0 seconds'
        return interval

def main( argv: list):
        """ Gather command line options, start the logging mechanism then continuously watch for changes. """

        global StartTime
        StartTime = datetime.datetime.now()

        argOptions = commandOptions( argv)
        logger = beginLogging( argOptions)

        printStartMessage( argOptions, logger)
        stopIfAlreadyWorking( argOptions, logger)

        manager = Watcher( argOptions, logger)
        manager.startWatching()

        #
        # Signals are only processed within this main thread.
        # If only the writerApp is being managed, we won't be
        # monitoring the PV input file for changes.
        #
        while True:
                signal.pause()
                manager.lookForChanges( firstLook=False)
        return

if __name__ == '__main__':
        main( sys.argv[1:])
