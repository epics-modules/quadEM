An [EPICS](http://www.aps.anl.gov/epics/) 
module that supports quad electrometers/picoammeters, typically used for photodiode-based
x-ray beam position monitors, or split ion chambers. They can also be used for any
low-current measurement that requires high speed digital input. There is support
for two models:</p>

* The TetrAMM picoammeter from [CAENels](http://www.caenels.com/products).
This device communicates using TCP/IP over 1 Gbit/s Ethernet. 
It provides 4-channel current measurements at up to 20000 Hz.

* The AH401B, AH401D, AH501, AH501C, and AH501D picoammeters originally designed
[Synchrotron Trieste (elettra)](http://ilo.elettra.trieste.it/index.php?page=_layout_prodotto&amp;id=54&amp;lang=en)
They are now sold commercially by [CAENels](http://www.caenels.com/products).
These devices communicate using TCP/IP over 100 Mbit/s Ethernet or high-speed serial. 
They provide 4-channel current measurements at up to 1000 Hz (AH401 series) or 6510 Hz (AH501 series).

* The Quad Electrometer built by Steve Ross from the APS (called APS_EM in this document). 
This device consists of a 4-channel digital electrometer unit and 2 VME boards. 
The device provides 2 readings per diode at up to 813 Hz.

Additional information:
* [Home page](http://cars.uchicago.edu/software/epics/quadEM.html)
* [Documentation](http://cars.uchicago.edu/software/epics/quadEMDoc.html)
* [Release notes](http://cars.uchicago.edu/software/epics/quadEMReleaseNotes.html)
