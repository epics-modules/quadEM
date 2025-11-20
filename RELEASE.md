# quadEM Releases

The versions of EPICS base, asyn, ADCore, and other synApps modules used for each release can be obtained from 
the configure/RELEASE file in each release of quadEM.

## Release 9-6 (November XXX, 2025)
- Added a software driver to get data into the quadEM. It is similar to the NDDriverStdArrays for areaDetector.
- Added support for the SenSiC PCR4 picoammeter.  Thanks to Sammy Mosca from STLab for this.
- Added support for the Sydor T4U electrometer.  There are 2 implementations for this:
  - T4UDirect: The quadEM driver communicates directly with the T4U.
  - T4U: The quadEM driver communicates with the T4U via a Sydor software library.
- Supported a variant of the Square geometry, where the diodes are aranged counterclockwise.
- Converted the documentation from raw HTML to Sphinx, published at https://epics-modules.github.io/quadEM.
- Added documentation about scanning with the TetrAMM and improved the documentation about plugins.
- Converted the release notes from raw HTML to RELEASE.md.
- Added a Word document of a study of noise with the TetrAMM.
- Removed support for the APS_EM, since this is no longer available or in use.
  Note that this changes the enum values for all models, and so the new quadEM.* OPI screens are required.
- Load ADCore/iocBoot/commonPlugins.cmd for all example IOCs.
  - The Plugins/ menu on quadEM.adl now has the following 3 menu items:
    - QE common plugins
    - QE FFT plugins
    - AD common plugins
  - AD common plugins provides all standard areaDetector plugins including ROIs, HDF5, etc.
  - It is now possible to stream data an HDF5 file, for example.
    This can be sampling at up to 20 kHz on the TetrAMM.
- Simplify the autosave files to reduce redundancy.

## Release 9-5 (December 7, 2023)
- Moved the documentation to Github pages, https://epics-modules.github.io/quadEM.
- Add 4 independent ranges, rather than just a single range for all 4 channels.
- Call asynNDArrayDriver base class in writeInt32 for base class parameters; fixed problem with EmptyFreeList.
- Change unsigned long long constant definitions in drvTetrAmm.cpp to avoid compiler errors.
- Bug fix in drvAHxxx.cpp.

## Release 9-4 (November 22, 2020)
- Added interlock status and clear register fault on setBiasState on the TetrAMM. Thanks to Diego Omitto for this.
- Improvements to NSLS2_IC driver.
- Added new iocBoot/iocNSLS2_IC directory.
  
## Release 9-3 (March 2, 2020)
- This version of quadEM requires ADCore R3-8 or later.
- Previous versions of quadEM will not work with ADCore R3-8 or later because the
  Acquire parameter is now implemented in the asynNDArrayDriver base class, rather
  than being local to quadEM.
- Added a new drvNSLS2_IC driver which has independent control of each channel for
  use with separate ion chambers, for example, rather than a quad electrometer.
- drvNSLS_EM: Fixed logic error in turning off data transmission from meter when not acquiring.
- iocsh/commonPlugins.iocsh: Fixed names of FFT time series ports.
- Added .bob autoconverted files for Phoebus Display Manager.
  
## Release 9-2-1 (August 9, 2019)
- Updated autoconverted edl files.
- Fixed Makefile to optionally build with SNCSEQ.

##  Release 9-2 (September 13, 2018)
- Added support for an additional averaging method. The driver currently uses the
  NDPluginStats plugin to do the "primary averaging". The NDPluginStats plugin provides
  additional statistics, such as sigma, skewness, histogram, etc. It is typically
  set to run at a fairly long averaging time in the range of 0.1 - 1 second. It can
  be run for much shorter times, but then the sigma and histogram calculations are
  less useful. There is sometimes a need for an additional averaging mechanism that
  can be run independently of the primary averaging, and this has now been added.
  One use case is for running feedback at a faster rate than primary averaging is
  running. The new averaging method is called "fast averaging", though there is actually
  no requirement that it run faster than the primary averaging. The fast averaging
  computes only the mean of each value (currents, sums, differences, and positions),
  and not sigma, etc. It uses the drvAsynFloat64Average device support. asyn R4-34
  or higher is required because that is when I/O Intr scanning for this device support
  was added. The FastAverageScan is typically set to "I/O Intr". In this case the
  FastAveragingTime is used to compute the number of samples to average (NumFastAverage),
  and the fast averaging records will process each time this number of readings has
  been received. The records can also be processed periodically by setting FastAverageScan="1
  second", etc. If fast averaging is not needed it can be disabled by setting FastAverageScan="Passive".
- Changed iocBoot/commonPlugins.cmd to be compatible with ADCore R3-3 where the
  time series support in NDPluginStats was changed. Also changed the port names so
  that multiple quadEM instances should be able to be run in the same IOC.
  
## Release 9-1 (January 31, 2018)
- Fixed a bug in the drvQuadEM base class when ring buffer overflow occurred. It
  was not decrementing a counter when it should, which would cause extra attempts
  to read the ring buffer, generating error messages. This could cause the TetrAMM
  to not recover from brief network outages, particularly on vxWorks.
- Removed the unlock()/lock() calls around doCallbacksGenericPointer. These are
  not needed and can cause problems because the thread needs to hold the lock here.
- Fixed the Reset function on the TetrAMM. It could hang the IOC application.
- Fixed turning acquisition off on the TetrAMM. It could get into an infinite loop
  and hang the IOC if the TetrAMM was not responding.
- Improved support for the AH501B from Elettra. This model has different firmware
  from the AH501B from CaenELS.
  - In External Gate mode it sends ACK\r\n on the falling edge of the gate signal.
    The driver has been changed to look for this string in the data stream in External
    Gate mode, and if it is found then it does callbacks immediately. This allows each
    acquisition to just be the samples collected when the external gate is high. The
    AveragingTime must be set to 0 in this mode.
  - The NAQ command used when AcquireMode=Single actually starts acquisition. The
    driver therefore must not send ACQ because the acknowlegement of that command would
    mess up the data stream.
  - It sends AK rather than ACK to acknowledge the HVS command. This is a firmware
    bug, but the driver has been changed to accept it.
  - This model has been added to the list of available models, and is called AH501BE,
    where the "E" mean Elettra. This model name is specified in the drvAHxxxConfigure
    command.
- Fixed medm adl files to improve the autoconversion to other display manager files.
- Added op/Makefile to automatically convert adl files to edl, ui, and opi files.
- Updated the edl, ui, and opi autoconvert directories to contain the conversions
  from the most recent adl files.
- Fixed vxWorks dbd dependency rule
- Removed testBusyAsynApp which was added in R7-1. It never belonged in this module.
  The busy module has now been made to optionally depend on autosave, so the equivalent
  test application has been added to the busy module and is no longer needed here.
- Added .iocsh files to simplify startup scripts. Thanks to Keanan Lang for this.

## Release 9-0 (September 18, 2017)
- Fixed a problem with PingPong on the NSLS_EM. If ValuesPerRead&gt;1 then it now
  forces PingPong=Both. Previously if ValuesPerRead was even and PingPong was Phase0
  or Phase1 then the driver might not update because the device was only sending the
  other phase. Even if ValuesPerRead was odd the phase information was not correct.
- Finished documentation for NSLS2_EM.
- Changed the quadEM.template file to load NDArrayDriver.template, i.e. the database
  for the asynNDArrayDriver base class. This ensures that records for the base class
  are present. This change requires changing the startup scripts to pass ADDR=0 and
  TIMEOUT=1 when loading the device template file, because these macros are required
  by NDArrayDriver.template. Because this is not backwards compatible this is major
  release.
- Updated quadEMTestAppMain.cpp to the version in the template files in EPICS base
  3.15.5. This includes a call to epicsExit() after the iocsh() returns. This is needed
  for epicsAtExit to work correctly on some platforms, including Windows.

## Release 8-0 (May 23, 2017)
- Minor changes to be compatible with areaDetector/ADCore R3-0. The quadEM base
  class constructor has changed its arguments, so this is a major release. There are
  no changes required to startup scripts or OPI displays.
  
## Release 7-1 (May 23, 2017)
- Added support for a new electrometer from Brookhaven National Laboratory. This
  is called NSLS2_EM in the documentation. This device consists of a 4-channel digital
  electrometer unit using a transconductance amplifier and built-in EPICS IOC running
  this software. The device provides 4-channel current measurements at up to 10 kHz.
  This model is capable of measuring larger currents than the NSLS Quad Electrometer
  (NSLS_EM). By compiling drvNSLS2_EM.cpp with the flags "-DSIMULATION_MODE -DPOLLING_MODE"
  the driver is simulator for quadEM, allowing testing with no hardware.
- Changed configure/CONFIG_SITE to not force static build on Linux.
- Added new testBusyAsynApp which is designed to test some problems with the asyn
  device support with the busy record. This does not really belong in quadEM, but
  it cannot be put in the busy or asyn modules because those don't include autosave,
  which is required to test the problem.
- Fixed a bug in External Gate mode for the AH501. External Gate mode previously
  did not work.
- Fixed a problem with the NSLS_EM. Recompute scaleFactor whenever integration time,
  range, or values per read change; previously it was only computed when acquisition
  started, which is not intuitive and led to amperes values that were incorrect.
- This release is compatible with ADCore R2-6.

## Release 7-0 (October 31, 2016)
- Changed the time-series and FFT support. Previously the time-series used the drvFastSweep
  driver from the synApps "mca" module. FFTs were done by an SNL program in the quadEM
  module. Now the time-series is done by the NDPluginTimeSeries plugin in areaDetector/ADCore.
  The FFT is now done by the NDPluginFFT plugin, also in ADCore. These changes mean
  that quadEM no longer depends on the mca or seq modules. They also add some significant
  new capabilities. For example the drvFastSweep driver is limited to epicsInt32 data,
  while the NDPluginTimeSeries plugin can handle any data type, including epicsFloat64
  which is what the quadEM driver produces.
- This release requires at least R2-5 of ADCore because the NDPluginFFT and NDPluginTimeSeries
  were not present in previous releases.
- Added support for TriggerPolarity on the TetrAMM.
- Improved the support for data acquisition applications, such as step-scan and
  on-the-fly scans using ion-chamber and photodiode detectors. This is particularly
  true for the TetrAMM model, using the new firmware version 2.9.XXX. This firmware
  version or later is required, because it adds features and fixes bugs present in
  earlier versions.
- Changed the AcquireMode choices from Continuous/One-shot to Continuous/Multiple/Single.
  Multiple will collect NumAcquire acquisitions and then stop. Single is the same
  as the previous One-shot, but using the same terminology as areaDetector.
- Changed the TriggerMode choices from Internal/ExternalTrigger/ExternalGate to
  FreeRun/Software/ExternalTrigger/ExternalBulb/ExternalGate. These are explained
  in the <a href="quadEMDoc.html">quadEMDoc documentation.</a>
- Changed the units of the current values on the NSLS_EM from A/D units to amperes.
  This is the same as the TetrAMM. The AH401 and AH501 are still in A/D units, but
  these may also be changed to amperes in a future release.
- Added Precision PVs to control the number of digits displayed for each current,
  sum, difference, and position. Current1 controls SumX, Current2 controls SumAll,
  and Current3 controls SumY.

## Release 6-0 (January 21, 2016)
- Added support for the CaenEls TetrAMM picoammeter. This device uses Gbit Ethernet
  without a built-in Ethernet to serial converter, so it is significantly faster than
  the AH501 and AH401 series. It supports on-board digital averaging. Note that TetrAMM
  firmware 2.0.09 or later is required because earlier versions have problems with
  the network stack.
- The base class and derived classes have changed somewhat to add this TetrAMM support,
  some of the database record names and types have changed, and changes are required
  to startup scripts. This is thus a new major release number. Some of the changes
  include:
  - The Trigger record has been renamed to TriggerMode.
  - The TriggerMode record choices are now device-dependent.
  - The Firmware record changed from stringin to waveform to handle longer strings.
  - New records were added for the TetrAMM (BiasInterlock, HVSReadback, HVVReadback,
    HVIReadback, and Temperature).
- Added support for the NSLS electrometer from Peter Siddons. It is called NSLS_EM
  in this software. It is quite similar to the AH401B and APS electrometer, using
  a similar current integrator chip. It has a minimum integration time of 400 microseconds,
  and thus a maximum sample rate of 2500 Hz. It uses Ethernet communication, UDP to
  search for modules on the subnet and TCP to send commands and read data.
- The CurrentScaleN and ReadStatus.SCAN records were added to save/restore, they
  were previously missing.
- Added support for reading the AH401, AH501, and TetrAMM meters in ASCII mode,
  as well as the binary mode that was previously supported. Binary mode can occassionally
  get out of sync, because there are no terminators between readings. ASCII mode is
  slower, but should not have a problem getting out of sync. The new ReadFormat record
  is used to select Binary or ASCII.
- Template files in $(TOP)/quadEMApp/Db are now installed into $(TOP)/db when building
  and the example IOC now loads them from the install directory.
- The device-specific template files now load the base class quadEM.template file
  using an "include" statement, so the base file no longer needs to be explicitly
  loaded in the startup script. This means that $(QUADEM)/db must be added to EPICS_DB_INCLUDE_PATH.
- This release requires areaDetector ADCore R2-4 or later.

## Release 5-0 (October 14, 2014)
- Supported a new geometry where the current sources are arranged in a square array
  with 2 on the top and 2 on the bottom, in addition to the previous diamond geometry
  of top, bottom, left right. The sum, difference and position calculations are done
  differently in this geometry.
- Added a new Geometry PV that controls which geometry (Diamond, Square) is used
  in the calculations.
- Because the diodes that contribute to the sum, difference and position calculations
  depend on the geometry, the names of the Sum, Diff and Position PVs have changed.
  - Sum12, Sum34 have changed to SumX, SumY
  - Diff12, Diff34 have changed to DiffX, DiffY
  - Position12, Position34 have changed to PositionX, PositionY
  - Sum1234 has changed to SumAll

## Release 4-1 (April 14, 2014)
- Added new AcquireMode record to support both Continuous and One-shot acquisition
  modes. Previously only continuous acquisition was supported. One-shot acquires the
  data from one AveragingTime and then stops acquisition. One-shot mode can be used
  with either triggered or free-running acquisition.<br />
  Note: Correct operation of the One-shot mode and triggered modes on the AH501D requires
  firmware version 2.0 or later. Previous firmware versions have bugs that prevent
  correct operation in these modes. The firmware can be updated in the field.
- The Acquire record was changed from a bo to a busy record to support use with
  the sscan record when AcquireMode=One-shot.
- Added optional "modelName" parameter to the constructors and configuration commands
  for the startup scripts. This allows one to specify what model of AH401x or AH501x
  is being used. The driver attempts to detect this automatically from the firmware
  version it reads from the device, but some of the Elettra units have strange firmware
  version strings so this can fail.
- Fix to allow fast feedback to work on APS_EM.
- Added new Firmware stringin record containing the firmware version from the device.
- Changed to use R2-0 of areaDetector. It only uses the ADCORE and ADBINARIES modules.
  
## Release 4-0 (March 12, 2013)
- Major update of the module. The device-dependent classes (AHxxx, APS_EM) have
  not changed signficantly. However the base quadEM class has been rewritten. In R3-x
  the quadEM base class worked as follows:
  - The ai records for the 11 parameters (Current[1-3],Sum[12,34,1234],Diff[12,34],
    and Position[12,34]) were computed using the asynInt32Average device support.
  - This meant that there were 11 callbacks each time a new value came from the electrometer,
    or more than 70,000 callbacks per second under some circumstances. This was a significant
    CPU load.
  - This approach produced average values, but it did not produce other useful statistics
    like the standard deviation, histogram of values, etc.
  - There was no good way to stream all of the readings to a disk file for an arbitrarily
    long time. The TimeSeries support was limited to a fixed time length, limited by
    available memory.
- R4-0 addresses these problems as follows:
  - The quadEM base class is now derived from the asynNDArray class in areaDetector,
     rather than directly from asynPortDriver.
  - The data from the device-dependent drivers are now first placed into a ring buffer
    whose size is defined in the constructor.
  - There is a new PV called AveragingTime that determines the time period over which
    to average the readings. The AveragingTime divided by the SampleTime determines
    the number of samples to average, NumAverage_RBV. When this number of samples have
    been accumulated in the ring buffer a separate thread copies them to a set of NDArrays
    and calls any registered plugins. 
  - There is a separate NDPluginStats plugin loaded for each of the 11 data values.
    This plugin receives an array of dimensions [NumAverage_RBV]. This plugin computes
    not only the mean (as in the previous version), but also the standard deviation,
    histogram of values, etc.
  - One of the NDArrays contains all of the data values, and has dimensions [11,NumAverage_RBV].
    This array can be passed to any of the file writing plugins, which can thus stream
    all of the data to disk for arbitrarily long time periods.
  - An NDStdArrays plugin is also loaded. This can be used to pass all of the data
    [11,NumAverage_RBV] or any of the individual data arrays to any channel access client.
  - This new approach provides much more information and flexibility. But it also
    significantly reduces the number of callbacks. For example if the averaging time
    is 1 second the number of callbacks is reduced from thousands per second to 12 per
    second, because the data are being passed in arrays.
  - The computationally intensive work of calculating the statistics is now being
    done in plugins, so can be done in different threads, each potentially running in
    a separate core on modern CPUs.

## Release 3-3 (December 20, 2012)
- Fixed problem with mutex locking on AH401 (and perhaps AH501). The lock was not
  being released for in the read task for long enough to allow EPICS to quickly stop
  acquisition.

## Release 3-2 (December 6, 2012)
- Fixed bug in reading binary data from AH501 meters. The manual incorrectly documented
  the binary data format.
- New version of the AH501D manual that correctly documents the binary data format.
- Minor changes to avoid compiler warnings.
  
## Release 3-1 (September 13, 2012)
- Added support for AH401D and the AH501 series (AH501, AH501C, AH501D) picoammeters
  from CaenEls. The AH501 series are significantly faster than the AH401 series. They
  work differently than the AH401 or APS_EM units, and do not support the concept
  of integration time or ping-pong. They have programmable resolution (16-bit or 24-bits),
  programmable number of active input channels (1, 2, or 4), and programmable bias
  power supply (AH501C and AH501D only).
- Added new record NumAverage. This applies to all models, and can be used to average
  NumAverage readings from the meter before doing the callbacks. On the AH501 series
  this also reduces the number of read operations by a factor of NumAverage, by increasing
  the number of messages read in a single operation. This reduces CPU time in direct
  proportion to NumAverage, which is particularly important with slow computers (e.g.
  older VME cards) with the AH501 series.
- Tested using UDP rather than TCP for communication with the AH401B and AH501 meters.
  This is more efficient than TCP, reducing CPU time by up to 10%. The example startup
  scripts now use UDP. The Lantronix DeviceInstaller must be used to configure the
  electrometer to use UDP, since the default is TCP.
- Added new record Model. This provides the model index and name of the device.
  This is determined automatically by the driver.
- Modified the medm screens so they only display the parameters that apply to the
  specific model being used.
- Modified startup scripts to simplify and support testing all models on any OS
  (for AH401 and AH501 series), and vxWorks for APS_EM.
  
## Release 3-0 (September 6, 2012)
- Complete re-write of the module. The new version is based on asynPortDriver. It
  consists of a base class, drvQuadEM.cpp, and two derived classes:
  1. drvAH401B.cpp which is new support for the AH401B picoammeter designed and sold
    by Synchrotron Trieste (elettra). It is also sold by CAENels. 
  2. drvAPS_EM.cpp, which is improved support for the APS electrometer designed by Steve Ross.

  The new version is not completely backwards compatible with the previous version,
  and will require modification of startup scripts and medm screens. 
- Added support for power-spectra calculations using FFTs on time-series arrays.
  These now run in the IOC, rather than requiring a separate Channel Access client,
  which previous releases required.
  
## Release 2-6 (November 2, 2011)
- Added support for the remote reboot capability of the electrometer. This makes
  initialization much simpler and more robust, since it can be done with software
  commands rather than power-cycling and toggling switches. It is only supported on
  newer electrometer firmware, but does not cause ill effects when sent to older electrometers.
  Added a Reboot record to the database, and modified the Initialize seq record to
  process the Reboot record as the first thing it does.
- Added an additional optional parameter to initQuadEM. This is the drvInfo field
  for the ipUnidig module. Previously the driver hardcoded pasynUser->reason=0
  when using the asynUInt32Digital interface. Now it uses drvUser->create, which
  is the correct way to do it.
- Created new documentation file, quadEMDoc.html.
  
## Release 2-5 (Sept 9, 2011)
- Modified RELEASE; deleted RELEASE.arch.
- Added .opi display files, for CSS-BOY

## Release 2-4-1 (March 30, 2010)
- Support parallel build.
  
## Release 2-4 (May 19, 2008)
- Removed unused functions in driver.
- Added new adl file quadEM_less.adl.
- Fixed bug in quadEM_settings.req.
  
## Release 2-3 (Sept. 6, 2006)
- Fixed driver so that configuration routines can be called from iocsh.
  
## Release 2-2 (March 29, 2005)
- Changed from using hardcoded stack size in epicsThreadCreate to generic stack size
  
## Release 2-1 (March 24, 2005)
- Converted from MPF to ASYN.
- Converted from specialized device support to generic device support from ASYN and MCA.
- Converted from C++ to C
  
## Release 2-0 (March 3, 2004)
- First release for EPICS 3.14.
- Converted from vxWorks functions to OSI functions
  
## Release 1-1 (November 3, 2003)
- Initial release, for EPICS 3.13.
