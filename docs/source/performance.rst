Performance
-----------

AH401B
~~~~~~
  
The following table shows the CPU utilization of a Linux machine (Xeon E5630 2.53GHz,
8 cores), and vxWorks MVME5100, with the AH401B electometer. The CPU load was measured
under the following conditions. This is the maximum data rate of the AH401B and
the time series waveform records and FFTs are processing at 1 Hz.

- ValuesPerRead=1
- IntegrationTime=0.001
- PingPoing=Yes
- AveragingTime=0.1
- NumChannels=4
- ReadFormat=Binary4
- Time series plugin
      
  - Plugin enabled
  - TSNumPoints=2048
  - TSAveragingTime=0.001
  - TSRead.SCAN=1 second
  - TSAcquireMode=Circular Buffer
      
- FFT plugins enabled

.. cssclass:: table-bordered table-striped table-hover
.. list-table::
   :header-rows: 1
   :widths: auto

   * - System
     - %CPU time
   * - Linux Xeon
     - 6%
   * - LMVME5100
     - 15%
  
It can be seen that the load on the Linux machine is only 6% of a single core, while
the load on the MVME5100 is 15%.

TetrAMM
~~~~~~~
  
The following table shows the CPU utilization of the Linux machine and vxWorks MVME5100,
with the TetrAMM electometer. The CPU load was measured under the following conditions.
ValuesPerRead=5 is the maximum data rate of the AH401B and the time series waveform
records and FFTs are processing at 1 Hz.
  
- ValuesPerRead=5, 10, 20, 50, 100
- AveragingTime=0.1
- NumChannels=4
- ReadFormat=Binary4
- Time series plugin
      
  - Plugin enabled
  - TSNumPoints=2048
  - TSAveragingTime=0.001
  - TSRead.SCAN=1 second
  - TSAcquireMode=Circular Buffer
  
- FFT plugins enabled

.. cssclass:: table-bordered table-striped table-hover
.. list-table::
   :header-rows: 1
   :widths: auto

   * - System
     - ValuesPerRead
     - %CPU time
   * - Linux Xeon
     - 5
     - 14%
   * - Linux Xeon
     - 10
     - 9%
   * - Linux Xeon
     - 20
     - 6%
   * - Linux Xeon
     - 50
     - 4%
   * - MVME5100
     - 5
     - 100%
   * - MVME5100
     - 10
     - 49%
   * - MVME5100
     - 20
     - 22%
   * - MVME5100
     - 50
     - 11%
   * - MVME5100
     - 100
     - 5%

It can be seen that the worst-case load on the Linux machine is only 14% of a single
core, while the load on the MVME5100 is 100% when ValuesPerRead=5. Using ValuesPerRead=20
or greater uses less than 22% of the CPU on the MVME5100, which is probably reasonable
in practice. That value still produces 5 kHz updates for time-series and fast feedback.
