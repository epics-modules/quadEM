Plugins
~~~~~~~
  
The example IOCs provided with quadEM load a file called commonPlugins.cmd, which
loads the following set of plugins from the areaDetector module. For more information
see the documentation in the links in the table below. Other plugins can also be
loaded, for example the TIFF, HDF5 or Nexus file writing plugins, etc.

.. cssclass:: table-bordered table-striped table-hover
.. list-table::
  :header-rows: 1
  :widths: auto

  * - Plugin type
    - Record prefixes
    - Description
        
  * - NDPluginStats
    - $(P)$(R)Current[1-4]:
    - Statistics for the current values. The average value is $(P)$(R)Current[1-4]:MeanValue_RBV.
      Many other statistics are available, including the minimum, maximum, standard deviation,
      and histogram of values.
  * - NDPluginStats
    - $(P)$(R)Sum[X,Y,All]:
    - Statistics for the sum of currents 1+2, 3+4, and 1+2+3+4. The average value is $(P)$(R)Sum[X,Y,All]:MeanValue_RBV.
      Many other statistics are available, including the minimum, maximum, standard deviation,
      and histogram of values.
  * - NDPluginStats
    - $(P)$(R)Diff[X,Y]:
    - Statistics for the differences of current 2-1 and 4-3. The average value is $(P)$(R)Diff[X,Y]:MeanValue_RBV.
      Many other statistics are available, including the minimum, maximum, standard deviation,
      and histogram of values.
  * - NDPluginStats
    - $(P)$(R)Pos[X,Y]:
    - Statistics for the positions. The average value is $(P)$(R)Pos[X,Y]:MeanValue_RBV.
      Many other statistics are available, including the minimum, maximum, standard deviation,
      and histogram of values.
  * - NDPluginStdArrays
    - $(P)$(R)image1:
    - Plugin that receives NDArray callbacks of dimension [11,NumAveraged_RBV] and puts
      this data into an EPICS waveform record. This can be used to provide access to all
      of the data from quadEM to any Channel Access client.
  * - NDFileNetCDF
    - $(P)$(R)netCDF1:
    - Plugin that receives NDArray callbacks of dimension [11,NumAveraged_RBV] and writes
      this data into a netCDF file. This can be done in Single mode, writing one array
      per file. o It can also be done in Stream mode, which continuously appends arrays
      to a single netCDF file.
  * - NDPluginTimeSeries
    - $(P)$(R)TS:
    - Plugin that receives NDArray callbacks of dimension [11,NumAveraged_RBV] and forms
      11 different time-series arrays. This plugin provides a time-history (like a digital
      scope) of the current, sum, difference and position at speeds up to 20000Hz (TetrAMM),
      6510 Hz (AH501 series), 1000Hz (AH401 series) or 813 Hz (APS_EM). The time per point
      can be greater than the sampling time, in which case it does averaging. The time
      series can operate in a fixed-length mode where acquisition stops after the specified
      number of time points have been collected. This mode is suited for data-acquisition
      applications, such as on-the-fly scanning. The time series plugin can also operate
      in a circular buffer mode with continuous acquisition and display of the most recent
  * - NDPluginFFT
    - $(P)$(R)FFT[1-11]:
    - Plugin that receives the time-series arrays from the NDPluginTimeSeries plugin and
      computes the frequency power-spectrum of each signal. This plugin can also average
      the FFTs to improve the signal/noise ratio.

Note that the first time the IOC is started all of the plugins will have EnableCallbacks=Disable.
It is necessary to enable each of the plugins that will be used. The plugins will
also initially start with CallbacksBlock=No. Setting CallbacksBlock=Yes can reduce
CPU load on slow processors like the MVME2100 (see the performance tables below).
The values of EnableCallbacks and CallbacksBlock are saved by autosave, and will
be restored the next time the IOC is started.
  
    This is the medm screen for all of the plugins defined in commonPlugins.cmd.

.. figure:: QECommonPlugins.png
    :align: center

    This is the medm screen for the Current1: NDPluginStats plugin loaded by commonPlugins.cmd.

.. figure:: QENDStats.png
    :align: center

This is the medm screen for the netCDF1: NDFileNetCDF plugin loaded by commonPlugins.cmd.

.. figure:: QENetCDF.png
    :align: center

This is the medm screen to control the NDPluginTimeSeries plugin. In this example
the time per point from the TetrAMM is 50 microseconds, and averaging time for the
time series plugin is 1 millisecond, or 20 points. The plugin has 2000 time points,
and is operating in circular buffer mode.
 
.. figure:: quadEM_TimeSeries.png
    :align: center

This is a plot of the time series for Current 1. It shows the last 2 seconds of
data in circular buffer mode.

.. figure:: quadEM_TimeSeriesPlot.png
    :align: center

This is a plot of the time series for the horizontal currents, sum, and position.
It shows the last 2 seconds of data in circular buffer mode.

.. figure:: quadEM_HorizontalTimeSeriesPlot.png
    :align: center

This is a plot of the FFT for Current 1. DC offset suppression is enabled and 10
FFTs are being averaged to improve the signal/noise ratio.

.. figure:: quadEM_NDFFTFreqSpectrumPlot.png
    :align: center

This is an medm screen for control of all 11 FFT plugins.

.. figure:: QEFFTPlugins.png
    :align: center


This is an medm screen that displays the FFTs of the Current, Sum, and Position
for the time-series data above. 100 FFTs are being averaged to improve the signal/noise
ratio.

.. figure:: quadEM_HorizontalFFTPlot.png
    :align: center
