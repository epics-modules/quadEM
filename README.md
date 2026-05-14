An [EPICS](http://www.aps.anl.gov/epics/) 
module that supports quad electrometers/picoammeters, typically used for photodiode-based
x-ray beam position monitors, or split ion chambers. They can also be used for any
low-current measurement that requires high speed digital input. There is support
for several models:
  * The FX4 electrometer from Pyramid Technical Consultants (https://pyramid.tech/products/fx4).
    This is sold by XDSOxford for the synchrotron market (https://xds-oxford.com/products/detectors-diagnostics/control-modules/fx4-4-channel-precision-electrometer)
    This device communicates using HTTP (REST API), Websockets, and a built-in EPICS Channnel Access server.
    It provides 4-channel current measurements at up to 10000 Hz to EPICS.
    It has additional digital I/O, analog outputs, and built-in servo control capabilities.
  * The AH401B, AH401D, AH501, AH501C, and AH501D picoammeters originally designed
    [Synchrotron Trieste (elettra)](http://ilo.elettra.trieste.it/index.php?page=_layout_prodotto&amp;id=54&amp;lang=en)
    They are sold commercially by [CAENels](http://www.caenels.com/products).
    These devices communicate using TCP/IP over 100 Mbit/s Ethernet or high-speed serial. 
    They provide 4-channel current measurements at up to 1000 Hz (AH401 series) or 6510 Hz (AH501 series).
  * The TetrAMM picoammeter from [CAENels](http://www.caenels.com/products).
    This device communicates using TCP/IP over 1 Gbit/s Ethernet. 
    It provides 4-channel current measurements at up to 20000 Hz.
  * The NSLS Quad Electrometer (called NSLS_EM in the documentation).
    This device consists of a 4-channel digital electrometer unit 
    using a current integrator with Ethernet communication. 
    The device provides 4-channel current measurements at up to 2500 Hz. 
    The [Sydor SI-EP-B4](http://sydortechnologies.com/files/Data-Sheet-SI-EP-B4.pdf)
    is based on this design and is software comptabible.
  * The NSLS2 Quad Electrometer (called NSLS2_EM in the documentation).
    This device consists of a 4-channel digital electrometer unit 
    using a transconductance amplifier and built-in EPICS IOC running this software. 
    The device provides 4-channel current measurements at up to 10,000 Hz. This model is capable
    of measuring larger currents than the NSLS Quad Electrometer above.
  * The PCR4 picoammeter from [SenSiC](https://www.sensic.ch/products/electronic-readout).
    This device communicates using TCP/IP over 1 Gbit/s Ethernet.
    It provides 4-channel current measurements at up to 53,000 Hz.
  * The T4U Electrometer from [Sydor Technologies](https://sydortechnologies.com/x-ray-beam-monitors/sydor-t4u-readout-electrometer/).
    This device provides 4-channel current measurement at up to 10 kHz.


Additional information:
* [Home page](https://epics-modules.github.io/quadEM)
* [Documentation](https://epics-modules.github.io/quadEM/index.html)
* [Release notes](RELEASE.md)

