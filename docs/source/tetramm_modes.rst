TetrAMM and FX4 Acquisition Modes
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
The TetrAMM and FX4 can be used in 8 different acquisition modes, depending on the values
of the TriggerMode and AcquireMode records. The following table describes these
modes. Note that AcquireMode=Single is completely equivalent to AcquireMode=Multiple
and NumAcquire=1, so the table does not list AcquireMode=Single.

Note that the TetrAMM supports triggering and gating modes in the firmware, using the
commands shown in the table below.  The FX4 does not support triggering or gating for
ADC acquisition.  However, it does support streaming of a high-speed position counter
at the same rate that it streams the ADC data. 
The position counter is configured to increment on both the rising and falling edge of
the first digital input (D1). Each ADC measurement and the position counter
has a precise timestamp. The EPICS support streams the position counter, and
does the triggering and gating operations in the driver software, using the counter
transitions and the timestamps. This has been tested to work with 2000 point acquisition
with 5 kHz trigger signal and ValuesPerRead=10.

.. cssclass:: table-bordered table-striped table-hover
.. list-table::
  :header-rows: 1
  :widths: auto

  * - TriggerMode
    - AcquireMode
    - Description
    - TetrAMM commands
  * - Free Run
    - Continuous
    - Values are acquired continuously and are averaged each time the AveragingTime is reached.
    - NRSAMP=ValuesPerRead, NAQ=0, TRG:OFF
  * - Free Run
    - Multiple
    - Data is acquired for the AveragingTime. This is repeated NumAcquire times and then
      acquisition stops. The plugins will be called NumAcquire times, each time with NumAverage
      samples.
    - NRSAMP=ValuesPerRead, NAQ=0, TRG:OFF
  * - Ext. Trig.
    - Continuous
    - A fixed number of samples is acquired starting on each leading edge of the external trigger input.
      For the TetrAMM the AveragingTime must be set to a value less than the time between trigger pulses. 
      If the next trigger arrives before the averaging time is complete then that trigger will be ignorred.
      For the FX4 the time between triggers can be less than the averaging time.
      If the next trigger arrives before the averaging time is complete then it will immediately average the
      values received so far, and start accumulating the next time point.  
      It thus effectively runs in Ext. Bulb mode if the triggers are faster than the averaging time.
      ValuesPerRead must be set to a value less than AveragingTime/1e5.
    - NRSAMP=ValuesPerRead, NAQ=AveragingTime/1e5/ValuesPerRead, TRG:ON
  * - Ext. Trig.
    - Multiple
    - This is the same as Ext. Trig. above, except that acquisition stops once NumAcquire
      triggers have been received.
    - NRSAMP=ValuesPerRead, NAQ=AveragingTime*1e5/ValuesPerRead, TRG:ON
  * - Ext. Bulb
    - Continuous
    - Samples are acquired while the external trigger input is asserted. On each trailing
      edge of the external trigger signal the plugins are called. ValuesPerRead must be
      set to a value less than (external trigger asserted time * 1e5). AveragingTime is
      ignored in this mode.
    - NRSAMP=ValuesPerRead, NAQ=0, TRG:ON
  * - Ext. Bulb
    - Multiple
    - This is the same as Ext. Bulb. above, except that acquisition stops once NumAcquire
      triggers have been received.
    - NRSAMP=ValuesPerRead, NAQ=0, TRG:ON
  * - Ext. Gate
    - Continuous
    - Samples are acquired while the external trigger input is asserted. When NumAverage
      samples have been acquired the plugins are called. The actual averaging time between
      calling the plugins will be longer than AverageTime, and is controlled by the duty
      cycle of the external gate signal. The trailing edge of the gate pulse is ignored
      in this mode. ValuesPerRead must be set to a value less than (external trigger asserted
      time * 1e5).
    - NRSAMP=ValuesPerRead, NAQ=0, TRG:ON
  * - Ext. Gate
    - Multiple
    - The is the same as Ext. Gate above except that when the plugins have been called NumAcquire
      times acquisition is stopped. Note that the actual number of gate pulses received
      will be &gt; NumAcquire, and is also controlled by the duty cycle of the external
      gate signal.
    -  NRSAMP=ValuesPerRead, NAQ=0, TRG:ON
