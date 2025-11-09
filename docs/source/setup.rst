Setup
-----
  
.. |br| raw:: html

    <br>

All of the device-dependent startup scripts invoke <a href="quadEM_commonPlugins_cmd.html">
quadEM/iocBoot/commonPlugins.cmd</a>. This file can be edited to add additional
plugins, such as the TIFF, HDF5, or Nexus file writers, etc.
  
Each iocBoot/iocXXX directory, e.g. iocBoot/iocTetrAMM, contains an example startup
script for Linux and Windows (st.cmd) and another for vxWorks (st.cmd.vxWorks).
Each of these startup scripts invokes the device-dependent startup script(s), e.g.
TetrAMM.cmd, NSLS_EM.cmd, etc.
  
TetrAMM Setup
~~~~~~~~~~~~~
  
These meters communicate via IP, so they must be configured with an IP address reachable
from the host IOC machine. The CAEN ELS Device Manager software must be used to
configure the device IP address and port number.
  
An example startup script is provided in |br|
<a href="quadEM_cmd_TetrAMM.html">quadEM/iocBoot/iocTetrAMM/TetraAMM.cmd</a>
  
This will need to be edited to set the correct IP address of the meter to be used.

AH401 and AH501 Series Setup
~~~~~~~~~~~~~~~~~~~~~~~~~~~~
  
These meters communicate via IP, so they must be configured with an IP address reachable
from the host IOC machine. The Lantronix module in the meters can be configured
to use either UDP or TCP. UDP can reduce CPU load, however it is more susceptible
to synchronization problems since it does not retransmit dropped packets. The startup
script must include the UDP qualifier on the drvAsynIPPortConfigure command if UDP
is selected.
  
Example startup scripts are provided in |br|
<a href="quadEM_cmd_AH401B.html">quadEM/iocBoot/iocAH401B/AH401B.cmd</a> |br|
and |br|
<a href="quadEM_cmd_AH501.html">quadEM/iocBoot/iocAH501/AH501.cmd</a>
  
These will need to be edited to set the correct IP address of the meters to be used.
  
These scripts each invoke a generic script: |br|
<a href="quadEM_cmd_AHxxx.html">quadEM/iocBoot/AHxxx.cmd</a>.
  
NSLS_EM Setup
~~~~~~~~~~~~~
  
These meters communicate via IP. They only support DHCP, they cannot be given a
static IP address. Each module on the subnet must have a unique Module ID, which
is set by a 16-position rotary switch on the device front panel. The driver sends
a broadcast message on the subnet to determine the Module ID and current IP address
of all NSLS_EM modules. If the specified module ID is found then it configures communication
to that module by calling drvAsynIPPortConfigure internally.
  
An example startup script is provided in |br|
<a href="quadEM_cmd_NSLS_EM.html">quadEM/iocBoot/iocNSLS_EM/NSLS_EM.cmd</a>
  
This will need to be edited to set the broadcast address of the network the device
is connected to, and the Module ID of the device.

NSLS2_EM Setup
~~~~~~~~~~~~~~

These meters communicate via IP. They run the EPICS IOC software internally on the
Zynq processor.
  
An example startup script is provided in |br|
<a href="quadEM_cmd_NSLS2_EM.html">quadEM/iocBoot/iocNSLS2_EM/NSLS2_EM.cmd</a>
  
This will need to be edited to set the Module ID of the device.

APS Electrometer Setup
~~~~~~~~~~~~~~~~~~~~~~
  
The APS_EM VME card cannot generate interrupts, but it can output a TTL pulse each
time new data is available, at up to 815 Hz. If this pulse is input to an Ip-Unidig
(or other asyn digital I/O device with interrupt and callback capabilities), then
the ipUnidig interrupt routine will call the APS_EM driver each time new data is
available. The Ip-Unidig channel where the APS_EM pulse is connected is specified
in the unidigChan argument to drvAPS_EMConfigure command in the startup script.
If an Ip-Unidig or other interrupt source is not being used then the APS_EM driver
will poll for new data at the system clock rate, typically 60Hz.
  
An example startup script is provided in |br|
<a href="quadEM_cmd_APS_EM.html">quadEM/iocBoot/iocAPS_EM/APS_EM.cmd</a>. |br|
<a href="quadEM_st_cmd_vxWorks.html">quadEM/iocBoot/iocAPS_EM/st.cmd.vxWorks</a>.
