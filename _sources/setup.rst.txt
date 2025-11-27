.. _quadEM_Plugins.cmd:   https://github.com/epics-modules/quadEM/tree/master/iocBoot/quadEM_Plugins.cmd
.. _TetrAMM.cmd:          https://github.com/epics-modules/quadEM/tree/master/iocBoot/iocTetrAMM/TetrAMM.cmd
.. _AH401B.cmd:           https://github.com/epics-modules/quadEM/tree/master/iocBoot/iocAH401B/AH401B.cmd
.. _AH501.cmd:            https://github.com/epics-modules/quadEM/tree/master/iocBoot/iocAH501/AH501.cmd
.. _AHxxx.cmd:            https://github.com/epics-modules/quadEM/tree/master/iocBoot/AHxxx.cmd
.. _NSLS_EM.cmd:          https://github.com/epics-modules/quadEM/tree/master/iocBoot/iocNSLS_EM/NSLS_EM.cmd
.. _NSLS2_EM.cmd:         https://github.com/epics-modules/quadEM/tree/master/iocBoot/iocNSLS2_EM/NSLS2_EM.cmd
.. _PCR4.cmd:             https://github.com/epics-modules/quadEM/tree/master/iocBoot/iocPCR4/PCR4.cmd
.. _T4U.cmd:              https://github.com/epics-modules/quadEM/tree/master/iocBoot/iocT4U_EM/T4U_EM.cmd
.. _T4U_Direct_EM:        https://github.com/epics-modules/quadEM/tree/master/iocBoot/iocT4UDirect_EM/T4UDirect_EM.cmd


Setup
-----
  
All the device-dependent startup scripts invoke quadEM_Plugins.cmd_.
That file loads ADCore/iocBoot/commonPlugins.cmd, and then loads
additional plugins that are configured specifically for quadEM.
  
Each iocBoot/iocXXX directory, e.g. iocBoot/iocTetrAMM, contains an example st.cmd startup script.
Each of these startup scripts invokes the device-dependent startup script(s), e.g.
TetrAMM.cmd, NSLS_EM.cmd, etc.
  
TetrAMM Setup
~~~~~~~~~~~~~
  
These meters communicate via IP, so they must be configured with an IP address reachable
from the host IOC machine. The CAEN ELS Device Manager software must be used to
configure the device IP address and port number.
  
An example startup script is provided in TetrAMM.cmd_.
  
This will need to be edited to set the correct IP address of the meter to be used.

AH401 and AH501 Series Setup
~~~~~~~~~~~~~~~~~~~~~~~~~~~~
  
These meters communicate via IP, so they must be configured with an IP address reachable
from the host IOC machine. The Lantronix module in the meters can be configured
to use either UDP or TCP. UDP can reduce CPU load, however it is more susceptible
to synchronization problems since it does not retransmit dropped packets. The startup
script must include the UDP qualifier on the drvAsynIPPortConfigure command if UDP
is selected.
  
Example startup scripts are provided in AH401B.cmd_ and AH501.cmd_.
  
These will need to be edited to set the correct IP address of the meters to be used.
  
These scripts each invoke a generic script AHxxx.cmd_.
  
NSLS_EM Setup
~~~~~~~~~~~~~
  
These meters communicate via IP. They only support DHCP, they cannot be given a
static IP address. Each module on the subnet must have a unique Module ID, which
is set by a 16-position rotary switch on the device front panel. The driver sends
a broadcast message on the subnet to determine the Module ID and current IP address
of all NSLS_EM modules. If the specified module ID is found then it configures communication
to that module by calling drvAsynIPPortConfigure internally.
  
An example startup script is provided in NSLS_EM.cmd_.
 
This will need to be edited to set the broadcast address of the network the device
is connected to, and the Module ID of the device.

NSLS2_EM Setup
~~~~~~~~~~~~~~

These meters communicate via IP. They run the EPICS IOC software internally on the
Zynq processor.
  
An example startup script is provided in NSLS2_EM.cmd_.
  
This will need to be edited to set the Module ID of the device.
