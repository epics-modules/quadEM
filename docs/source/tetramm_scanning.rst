Scanning with the TetrAMM or FX4
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

With the above modes it is possible to use the TetrAMM or FX4 for data acquisition during various types of scans.

The following is screen shot of the TetrAMM screen in the free-running continuous aquisition state with 0.01 second averaging time.
Each scan described below used most of these settings, with the changes in the settings described for that scan type.

.. figure:: sscan_tetramm_base.png
    :align: center

Step scan with quadEM in free-run mode
.......................................
This scan type uses a step-scan for the positioner and runs the quadEM in free running mode.
The quadEM averaging time is set a small amount less than the time it takes the motors to move
each step.  This mode is faster than stopping and starting the quadEM at each point. The error
due to the quadEM not being strictly synchronized to the motor position are likely to be acceptable
in many cases.

The following is screen shot of the sscan record moving a single motor and reading the 4 currents from the quadEM.
There is no detector trigger.

.. figure:: sscan_tetramm_free_run.png
    :align: center

The scan is 201 points and took 11.7 seconds.  The time per point is thus 0.058 ms.

The quadEM was configured as follows:

- AcquireMode=Continuous
- ValuesPerRead=5
- AveragingTime=.05 s.  This is a little less than the time per point in the scan, so there will be a new reading at each point.
- The sscan record detectors are Current1:MeanValue_RBV, Current2:MeanValue_RBV, etc.

This is a plot of Current1 after the scan.

.. figure:: sscan_tetramm_free_run_plot.png
    :align: center

|

Step scan with quadEM in single acquire mode
.............................................
This scan type uses a step-scan for the positioner and runs the quadEM in single cycle mode.
This guarantees that the quadEM was only counting when the motor was at the target position
for each step.

The following is screen shot of the sscan record moving a single motor and reading the 4 currents from the quadEM.
The detector trigger is the quadEM Acquire PV.

.. figure:: sscan_tetramm_triggered.png
    :align: center

The scan is 201 points and took 31.3 seconds.
This scan took 20 seconds longer than the scan where the quadEM AcquireMode is Continuous.
10 seconds of this are because the scan needs to wait for the quadEM averaging for 50 ms at each point.
The additional 10 seconds is due to overhead with restarting the quadEM at each point.

The quadEM was configured as follows:

- AcquireMode=Single
- ValuesPerRead=50
- AveragingTime=.05 s

This is a plot of Current1 after the scan.

.. figure:: sscan_tetramm_triggered_plot.png
    :align: center

|

Fly scan with quadEM in free-run mode
.............................................
This scan type uses a fly-scan for the positioner and runs the quadEM in free-run continuous mode.
The scan is configured by setting the following:

- The total time for the scan is controlled by the number of points (.NPTS) and the detector settling time (.DDLY).
  In the example below .DDLY=.01 s, so that the 201 point scan will complete in 2 seconds.
- The motor speed must be set to so that it will reach the end position in the total time for the scan.
  In this case the motor travel is set to 2 mm, and hence the velocity is set to 1.0 so it will complete
  the move in 2.0 seconds.  This neglects the acceleration and deceleration times, which are 0.2 seconds.
- In Fly mode the sscan record starts the motor moving continuously to the final position.  While the motor
  is moving the sscan record records the motor readback position and the detector values continuously, with
  a delay of .DDLY.  This means that the detector values and motor positions recorded are synchronized within the
  accuracy and precision of their respective update rates.

The following is screen shot of the sscan record moving a single motor continuously and reading the 4 currents from the quadEM.

.. figure:: sscan_tetramm_fly_free_run.png
    :align: center

The scan is 201 points and took 2.038 seconds as shown on the readback for the second positioner = TIME.

.. figure:: sscan_tetramm_fly_free_run_positioners.png
    :align: center

The quadEM was configured as follows:

- AcquireMode=Continuous
- TriggerMode=Free run
- ValuesPerRead=5
- AveragingTime=.01 s

This is a plot of Current1 after the scan.

.. figure:: sscan_tetramm_fly_free_run_plot.png
    :align: center

|

Triggered scan using quadEM time series plugin
...............................................
This scan type uses triggers from the positioner to collect the quadEM data in the NDPluginTimeSeries plugin.
This guarantees that the positioner and detector data are perfectly synchronized.

For this test the triggers came from a Measurement Computing USB-CTR08 pulse generator, but they could
have been coming from position based triggers from a motor controller.

The scan is configured by first determining the period at which trigger pulses will be arriving.
For a motor scan this will be calculated from the distance interval per trigger and the motor velocity.
For this test it was set by the following:

- The pulse generator is configured to output 201 pulses at 100 Hz, i.e. .01 second period.

The quadEM was configured as follows:

- AcquireMode=Multiple
- NumAcquire=201
- TriggerMode=Ext. Trig.
- ValuesPerRead=5
- AveragingTime=.0099 s, 1% (0.1 ms) less than the trigger period.

This is the configuration of the quadEM in this mode.

.. figure:: tetramm_fly_triggered.png
    :align: center

When TriggerMode=Ext. Trig. the driver tells the quadEM to collect the number of samples determined by the
AveragingTime (i.e. NumAverage_RBV) on receipt of each trigger pulse.  NumAverage_RBV is 198 in the example above.
The AveragingTime must be slightly less than the trigger pulse period to allow for slight clock frequency differences
between the trigger source and the quadEM.  In this case the AveragingTime is 1% (0.1 ms) less than the pulse period.
The only consequence of this is that there is 1% during each acquire period (0.1 ms) when the quadEM is not collecting data.

The quadEM TimeSeries plugin is configured as follows:

- AveragingTime=0.0099 s
- NumPoints=201

This is the configuration of the TimeSeries plugin:

.. figure:: tetramm_fly_triggered_time_series.png
    :align: center

The AveragingTime of the time series plugin must be the same as that for the quadEM.  This guarantees that each
external trigger will move to the next point in the time series.

The following steps are used to perform as scan in this mode:

- Set the TimeSeries TSAcquire (labeled Erase/Start) PV to 1
- Set the quadEM Acquire PV to 1
- Start the trigger source.  In this test that is the pulse generator, but it would normally be the motor
  generating the trigger pulses.
- Wait for the motion to complete and then read the time series waveform records, e.g. TS:Current1, etc.

This is a plot of Current1 after the scan.  The retrace to 0 on the last point is a bug in medm.

.. figure:: tetramm_fly_triggered_time_series_plot.png
    :align: center

Note while the scan actually took 2.01 seconds (201 points at 100 Hz) the last point horizontal scale
of this plot is 1.98 seconds.
This is because the time between triggers is 1% more than the averaging time, but the
plugin has no way to know what the actual time between trigger pulses is.

Triggered scan using quadEM in bulb mode with the statistics plugin
....................................................................
The time series plugin used in the previous example above can only be used when each trigger generates the same
number of ADC samples. If the time between triggers is not know or is variable then bulb mode.
This will average all samples between the leading and trailing edge of the trigger pulse.
When using bulb mode or Ext. Trigger mode on the FX4 with triggers faster than the averaging time,
the time series feature of the statistics plugin must be used.  It computes the statistics at the end 
of each trigger period, regardless of the number of samples collected.

For this test the triggers came from a Measurement Computing USB-CTR08 pulse generator, but they could
have been coming from position based triggers from a motor controller.


The pulse generator is configured to output 201 pulses at 100 Hz, i.e. .01 second period.  However, this will work
even if the trigger period is changing, e.g. during the acceleration phase of a motor.

The quadEM was configured as follows:

- AcquireMode=Multiple
- NumAcquire=201
- TriggerMode=Ext. Trig.
- ValuesPerRead=5
- AveragingTime is irrelevant and is not used. It is set to 1 second to illustrate this.

This is the configuration of the quadEM in this mode.

.. figure:: tetramm_fly_bulb.png
    :align: center

Open in Plugins/QE common plugins. Press More for Current 1, which opens NDStats.adl for Current 1.
Open Statistics/Time series plots/Mean. That is this screen:

.. figure:: tetramm_fly_bulb_statistics_mean.png
    :align: center

The number of points is set to 201.

The following steps are used to perform as scan in this mode:

- Set the TSAcquire PV to 1 (press EraseStart in above screen)
- Set the quadEM Acquire PV to 1
- Start the trigger source.  In this test that is the pulse generator, but it would normally be the motor
  generating the trigger pulses.
- Wait for the motion to complete and then read the time series waveform records, e.g. TSMeanValue, etc.

This is a plot of Current1:TSMeanValue after the scan.  The retrace to 0 on the last point is a bug in medm.

.. figure:: tetramm_fly_bulb_time_series_plot.png
    :align: center

Note that this scan actually took 2 seconds but the horizontal scale in this plot is incorrect (20 seconds) 
because it has no way of knowing the actual time between triggers.  Each point in the plot is the mean
of all ADC values between the leading and trailing edge of the trigger pulse.
