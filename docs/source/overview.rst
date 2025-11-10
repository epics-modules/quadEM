.. _quadEM:             https://github.com/epics-modules/quadEM
.. _CAENels:            http://www.caenels.com/products
.. _Elettra:            http://www.elettra.eu/technology/industry/elettra-for-industry.html
.. _Sydor:              http://sydortechnologies.com/files/Data-Sheet-SI-EP-B4.pdf
.. _SenSiC:             https://www.sensic.ch/products/electronic-readout
.. _ADCore:             https://github.com/areaDetector/ADCore
.. _std:                https://github.com/epics-modules/std
.. _asynNDArrayDriver:  https://areadetector.github.io/areaDetector/ADCore/NDArray.html#asynndarraydriver
.. _asynPortDriver:     https://epics-modules.github.io/asyn/asynPortDriver.html
.. _NDPluginTimeSeries: https://areadetector.github.io/areaDetector/ADCore/NDPluginTimeSeries.html
.. _NDPluginFFT:        https://areadetector.github.io/areaDetector/ADCore/NDPluginFFT.html


Overview
--------
  
quadEM_ supports quad electrometers/picoammeters, typically used for photodiode-based
x-ray beam position monitors, or split ion chambers. They can also be used for any
low-current measurement that requires high speed digital input. There is support
for several models:
  
- The AH401 series (AH401B, AH401D) and AH501 series (AH501, AH501C, AH501D) picoammeters
  originally designed by Elettra_. They are now sold commerically by CAENels_. 
  These devices communicate using TCP or UDP over 100 Mbit/s Ethernet
  or high-speed serial. They provide 4-channel current measurements at up to 6510
  Hz (AH501 series) or 1000 Hz (AH401 series).
- The TetrAMM picoammeter sold by CAENels_.
  This device communicates using TCP/IP over 1 Gbit/s Ethernet. It provides 4-channel
  current measurements at up to 20 kHz.
- The NSLS Quad Electrometer (called NSLS_EM in this document). This device consists
  of a 4-channel digital electrometer unit with Ethernet communication. The device
  provides 4-channel current measurements at up to 2500 Hz. 
  The Sydor_ SI-EP-B4 is based on this design and is software comptabible.
- The NSLS2 Quad Electrometer (called NSLS2_EM in this document). This device consists
  of a 4-channel digital electrometer unit with Ethernet communication. The device
  provides 4-channel current measurements at up to 10,000 Hz. This design is based
  on a transconductance amplifier and can measure currents into the mA range, unlike
  the NSLS_EM which is based on a current integration chip and is limited to currents
  less than about 1 micro-amp. This unit is packaged with a Zynq processing and run
  this EPICS IOC software internally.
- The Quad Electrometer built by Steve Ross from the APS (called APS_EM in this
  document). This device consists of a 4-channel digital electrometer unit and 2 VME
  boards. The device provides 2 readings per diode at up to 813 Hz. This device appears
  to no longer be in use on any APS beamlines, so the support is now deprecated and
  will be removed in a future release.
- The PCR4 picoammeter from SenSiC_.
  This device communicates using TCP/IP over 1 Gbit/s Ethernet.
  It provides 4-channel current measurements at up to 53,000 Hz.
  
The AH401 series, NSLS_EM, and APS_EM are based on the same principle of an op-amp
run as a current amplifier with a large feedback capacitor, and a high resolution
ADC. The AH501 series, TetrAMM, and NSLS2_EM are based on a transimpedance input
stage for current sensing, combined with analog signal conditioning and filtering
stages. The AH501C, AH501D, and TetrAMM have an integrated programmable bias supply.
  
The quadEM_ software includes asyn drivers that provide support for the following:
  
- Analog input records using the NDPluginStats plugin from the synApps 
  areaDetector ADCore_ module. This provides digitally averaged readings of the current,
  sum, difference and position at speeds that are determined by the AveragingTime
  record described below. The quadEM drivers do the callbacks on the asynGenericPointer
  interface for NDArray objects required to use the areaDetector plugins. The NDPluginStats
  module is used to compute the averaged readings. It also provides additional statistics
  including standard deviation, min, max, and a histogram of values. This averaging
  is termed the "primary averaging".
- Analog input records using the asynFloat64Average device support from asyn. These
  records can either process at the standard EPICS scan rates, normally 10Hz to 0.1
  Hz. Beginning with asyn R4-34 they can also use SCAN=I/O Intr, which allows them
  average and process at any integer divisor of the device sampling rate. This averaging
  is termed "fast averaging", though it does not necessarily run faster than the primary
  averaging described above.
- The NDPluginStats provide time-series of the statistics. These can be used to
  for on-the-fly data acquisition applications, where the meter is triggered by an
  external pulse source, such as one derived from a motor motion.
- One could also use the standard asynFloat64 device support with ai records and
  scan=I/O Intr, in which case the record would update with every reading from the
  device. This is likely to overwhelm the EPICS IOC if the electrometer is run at
  very fast sampling times.
- Streaming data to disk at the full rate from the device. This is done using the
  file plugins from areaDetector.
- Time series data (like a digital scope) of the current, sum, difference and position
  at speeds up to 20000 Hz (TetrAMM), 6510 Hz (AH501 series), 1000 Hz (AH401 series)
  2500 Hz (NSLS_EM), or 813 Hz (APS_EM). The data is available in standard EPICS waveform
  records, using the NDPluginTimeSeries_ plugin ADCore_. The time per point can be greater, in which case it does
  averaging.
- FFTs of the time series data, providing the power spectrum of each signal as another
  EPICS waveform record. This uses the NDPluginFFT_ plugin from ADCore_.
- epid record support.
  
  - This can provide "fast feedback" via an asyn D/A converter (e.g. dac128V), also
    at speeds up to 20000 Hz, 6510 Hz, 1000Hz, 2500 Hz, or 813Hz. If it is run slower
    it does signal averaging. This support is provided in the synApps 
    std_ module. The quadEM drivers do the callbacks on the asynFloat64 interface
    required to use the epid fast feedback device support. This fast feedback is limited
    to controlling devices in the same IOC as the quadEM driver, because it uses asyn
    links.
  - The epid record can also be used in "slow feedback" mode using the averaging provided
    by the primary averaging or fast averaging records described above. In this mode
    it can use Channel Access links, and so it not constrained to controlling devices
    in the same IOC.
  
The following manuals provide detailed information on these devices:
  
- :download:`APS Electrometer Users Guide <Electrometer_Users_Guide_01_22_2007.pdf>`
- :download:`AH401B Users Manual <AH401B_UsersManual_V1.0.pdf>`
- :download:`AH401D Users Manual <AH401D_UsersManual_V1.2.pdf>`
- :download:`AH501C Users Manual <AH501C_UsersManual_V1.0.pdf>`
- :download:`AH501D Users Manual <AH501D_UsersManual_V1.3.pdf>`
- :download:`TetrAMM Users Manual <TetrAMM_UsersManual_V1.5.pdf>`
  
  
The support is based on asynNDArrayDriver_ from ADCore_, which in turn is based on asynPortDriver_. 
It consists of a base class drvQuadEM, which is device-independent. 
There are device-dependent classes for the TetrAMM, AH401 and AH501 series, NSLS electrometer, 
NSLS2 electrometer, and the APS electrometer.

The quadEM driver works as follows:

- The data from the device-dependent drivers are first placed into a ring buffer
  whose size is defined in the constructor and configuration function. The driver
  provides 4 current readings. 7 additional values are computed from these 4 values:
  SumX, SumY, SumAll, DiffX, DiffY, PositionX, PositionY. The meanings of X and Y
  are discussed in the geometry section below.
- The PV called AveragingTime determines the time period over which to average the
  readings. The AveragingTime divided by the SampleTime determines the number of samples
  to average, NumAverage_RBV. When this number of samples have been accumulated in
  the ring buffer a separate thread copies them to a set of NDArrays and calls any
  registered plugins. 
- There is a separate NDPluginStats plugin loaded for each of the 11 data values.
  This plugin receives an array of dimensions [NumAverage_RBV]. This plugin computes
  not only the mean, but also the standard deviation, histogram of values, etc.
- One of the NDArrays contains all of the data values, and has dimensions [11,NumAverage_RBV].
  This array can be passed to any of the file writing plugins, which can thus stream
  all of the data to disk for arbitrarily long time periods. 
- The [11,NumAverage_RBV] NDArray is also passed to the NDPluginTimeSeries plugin
  which is used to generate time-series arrays. 
- The time-series plugin output is used by the NDPluginFFT plugin to compute FFTs,
  producing arrays containing the frequency power-spectrum of the time series data.
- An NDStdArrays plugin is also loaded. This can be used to pass all of the data
  [11,NumAverage_RBV] or any of the individual data arrays to any channel access client.
- The computationally intensive work of calculating the statistics is done in plugins,
  so can be done in different threads (if CallbacksBlock is set to No), each potentially
  running in a separate core on modern CPUs.
- In addition to placing the data from each time point into the ring buffer, the
  driver does callbacks on the asynFloat64 interface for each data value. This is
  used for two things:
  
  - Fast feedback support with the epid record.
  - Fast averaging support with ai records. These records can be processed periodically
    with SCAN=1 second, 0.1 second, etc., or they can have SCAN=I/O Intr. In this case
    the ai record device support sums the callback readings until NumAverage readings
    have been received, at which point the average is computed and the record is processed.
    The quadEM database computes NumAverage from the FastAveragingTime record and writes
    NumAverage to the .SVAL field in each ai record.

Note: This version of the driver requires a minimum firmware version on some models

- AH501D Firmware version 2.0.
- TetrAMM Firmware version 2.9.11
  

Prior to R5-0 the quadEM driver assumed the following geometry for the 4 current::

             4
             
          1     2
             
             3

  
The PV for the computed quantities were called Sum12, Sum34, Sum1234, Diff12, Diff34,
Position12, and Position34. The differences, and hence positions, were computed
as 2-1 and 4-3, which would correspond to Position12 being positive to the right
and Position34 being positive up in the above diagram.

For R5-0 and later two geometries are supported, and the names of the Sum, Diff
and Position PVs were changed. The computed quantities are called SumX, SumY, SumAll,
DiffX, DiffY, PositionX, and PositionY.

The first geometry is the same as that illustrated above, and is called Diamond.
For this geometry only 2 diodes are used for the position calculation in each direction.
SumX=(1+2), SumY=(3+4), DiffX=(2-1), DiffY=(4-3). This geometry is identical to
the geometry assumed prior to R5-0. The X diodes are 1&2 rather than 1&3 so that
it is possible to use just the first 2 inputs on the AH501 series to increase readout
speed in cases where all 4 diodes are not used. This would not be possible if diodes
1 and 3 were used for the X calculations.
  
The second geometry is::


          1     2
       
             
          4     3

  
This geometry is called Square. For this geometry all 4 diodes are used for the
position calculation in each direction. SumX=SumY=(1+2+3+4), DiffX=(2+3)-(1+4),
DiffY=(1+2)-(3+4).
  
R9-5 added a variant of the square geometry, SquareCC::

          1     4


          2     3

- It is similar to Square, but the 4 diodes are arranged counterclockwise.
  SumX=SumY=(1+2+3+4), DiffX=(3+4)-(1+2), DiffY=(1+4)-(2+3).
  
For all geometries SumAll=(1+2+3+4), PositionX=DiffX/SumX, and PositionY=DiffY/SumY.
X positive is to the right and Y positive is up for both geometries.
