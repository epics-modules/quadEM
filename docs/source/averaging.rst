Averaging
---------

The signals from the quadEM devices are averaged at a number of places in the 
device and processing pipeline.

.. cssclass:: table-bordered table-striped table-hover
.. list-table::
  :header-rows: 1
  :widths: auto

  * - Description
    - Record
         
  * - Averaging in the device.  This is device-dependent.
    - ValuesPerRead is used on the TetrAMM.  The TetrAMM sampling frequency 
      is 100 kHz and the minimum value of ValuesPerRead is 5, resulting in
      a maximum sampling rate of 20 kHz at the quadEM driver.
  * - Primary averaging in quadEM driver.  The NDPlugStats plugins are
      used to average the signals for this averaging, providing 
      additional statistics such as sigma, skewness, and kurtosis.
    - AveragingTime
  * - Secondary averaging in quadEM driver.  The asynFloat64Average
      device support is used to do this.  It provides only the mean.
      This can be used for feedback, for example, and can operate
      at a different averaging time than the primary averaging which
      might be used for data collection.
    - FastAveragingTime
  * - Averaging in the time-series plugin.  The time-series plugin
      is passed data at the rate at which it arrives from the device.
      It can do averaging on the time-series data.  This can be used
      to improve the signal/noise at the expense of bandwidth upstream
      of the FFT plugin.
    - TS:TSAveragingTime
  * - Averaging in the FFT plugin.  The FFT plugin receives data each
      time the time-series plugin receives the requested number of samples.
      The resulting FFTs can be averaged to improve their signal to noise.  
    - FFT:[signal]:FFTNumAverage
