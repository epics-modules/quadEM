TetrAMM Acquisition Modes
~~~~~~~~~~~~~~~~~~~~~~~~~
  
The TetrAMM can be used in 8 different acquisition modes, depending on the values
of the TriggerMode and AcquireMode records. The following table describes these
modes. Note that AcquireMode=Single is completely equivalent to AcquireMode=Multiple
and NumAcquire=1, so the table does not list AcquireMode=Single.

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
    - A fixed number of samples is acquired starting on each rising edge of the external
      trigger input. AveragingTime must be set to a value less than the time between trigger
      pulses.
    - NRSAMP=ValuesPerRead, NAQ=AveragingTime/1e5/ValuesPerRead, TRG:ON
  * - Ext. Trig.
    - Multiple
    - A fixed number of samples is acquired starting on the first rising edge of the external
      trigger input. This repeats NumAcquire times and then acquisition stops. ValuesPerRead
      must be set to a value less than AveragingTime/1e5.
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
    - Samples are acquired while the external trigger input is asserted. On each trailing
      edge of the external trigger signal the plugins are called. This is repeated NumAcquire
      times and then acquisition is stopped. ValuesPerRead must be set to a value less
      than (external trigger asserted time * 1e5). AveragingTime is ignored in this mode.
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
    - Samples are acquired while the external trigger input is asserted. When NumAverage
      samples have been acquired the plugins are called. The actual averaging time between
      calling the plugins will be longer than AverageTime, and is controlled by the duty
      cycle of the external gate signal. When the plugins have been called NumAcquire
      times then acquisition is stopped. Note that the actual number of gate pulses received
      will be &gt; NumAcquire, and is also controlled by the duty cycle of the external
      gate signal. The trailing edge of the gate pulse is ignored in this mode. ValuesPerRead
      must be set to a value less than (external trigger asserted time * 1e5).
    -  NRSAMP=ValuesPerRead, NAQ=0, TRG:ON
