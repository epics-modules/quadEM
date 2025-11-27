Using Feedback
--------------
  
The quadEM devices can be used as the readback device for feedback loops. 
They are often used, for example, to control monochromator pitch and roll
using the signals from a quad beam position monitor (BPM).
Any of the quadEM signals can be used, depending on current source and desired behavior.
The epid record in the synApps std module can be used for this.

The following table shows some examples of the possible signals and uses:

.. cssclass:: table-bordered table-striped table-hover
.. list-table::
  :header-rows: 1
  :widths: auto

  * - Signal
    - Description
    - Possible use
         
  * - Current[1-4]:MeanValue_RBV
    - Currents using "primary averaging"
    - Intensity feedback on an individual photodiode or ion chamber
  * - Current[1-4]Ave
    - Currents using "fast averaging"
    - Intensity feedback on an individual photodiode or ion chamber
  * - SumAll:MeanValue_RBV
    - Sum of all 4 readings using "primary averaging"
    - Intensity feedback on a quad BPM
  * - SumAllAve
    - Sum of all 4 readings using "fast averaging"
    - Intensity feedback on a quad BPM
  * - Pos[X,Y]:MeanValue_RBV
    - X or Y position feedback using "primary averaging"
    - Position feedback on a quad BPM
  * - Position[X,Y]Ave
    - X or Y position feedback using "fast averaging"
    - Position feedback on a quad BPM

  
This is an example of an epid record being used for controlling the vertical beam position
from a monochromator.

The readback signal is PositionYAve, the vertical beam position from a quad BPM using the "fast averaging".
The PositionScaleY PV has been set so that the measured position is in microns.
The readback link has the CP attribute, so the PID record processes each time
there is a new reading from the quadEM.  

The output is a D/A converter controlling the voltage to a piezo actuator that controls
the pitch of the second crystal of the monochromator.  

The setpoint is 0 in this case, which is the desired beam position.

.. figure:: mono_pitch_feedback.png
    :align: center

|

This is the medm screen for the PID parameters in the epid record.
These are adjusted to tune the feedback for the desired response and stability.
The units of the integral (I) term are approximately Hz.

.. figure:: mono_pitch_feedback_parameters.png
    :align: center

|

This is the medm screen that plots the setpoint and readback of the feedback loop
as a function of time.

.. figure:: mono_pitch_feedback_plot.png
    :align: center
