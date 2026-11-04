TetrAMM and FX4 Trigger Modes
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
The TetrAMM and FX4 can be used in 4 different acquisition modes. 
The following table describes these modes. 

For each trigger mode AcquireMode can be Continuous, Multiple, or Single.
When AcquireMode=Multiple acquisition stops after NumAcquire acquisition are complete.
Note that AcquireMode=Single is completely equivalent to AcquireMode=Multiple
and NumAcquire=1.

Note that the TetrAMM supports triggering and gating modes in the firmware. 
The nominal maximum trigger rate is 2000 Hz.  However, with ValuesPerRead=10
I have found that it reliably triggers at 1500 Hz and is not reliable at 1800 Hz. 

The FX4 does not support triggering or gating for ADC acquisition.  
However, it does support streaming of a high-speed position counter
at the same rate that it streams the ADC data. 
The position counter is configured to increment on both the rising and falling edge of
the first digital input (D1). Each ADC measurement and the position counter
has a precise timestamp. The EPICS support streams the position counter, and
does the triggering and gating operations in the driver software, using the counter
transitions and the timestamps. This has been tested to work with 10000 point acquisition
with 5 kHz trigger signal and ValuesPerRead=10.

.. cssclass:: table-bordered table-striped table-hover
.. list-table::
  :header-rows: 1
  :widths: auto

  * - TriggerMode
    - Description
  * - Free Run
    - Values are acquired continuously and are averaged each time the AveragingTime is reached.
  * - Ext. Trig.
    - NumAcquire samples are acquired starting on each leading edge of the external trigger input.
      NumAcquire is determined by the AveragingTime and ValuesPerRead.
      If the next trigger arrives before the averaging time is complete then it will immediately average the
      values received so far, and start accumulating the next time point.  
      This means that Ext. Trig functions the same as Ext. Bulb mode if the trigger period is less
      than the averaging time.
  * - Ext. Bulb
    - Samples are acquired while the external trigger input is asserted. On each trailing
      edge of the external trigger signal the plugins are called. AveragingTime is
      ignored in this mode.
  * - Ext. Gate
    - Samples are acquired while the external trigger input is asserted. When NumAverage
      samples have been acquired the plugins are called. The actual averaging time between
      calling the plugins will be longer than AverageTime, and is controlled by the duty
      cycle of the external gate signal. The trailing edge of the gate pulse is ignored
      in this mode.
