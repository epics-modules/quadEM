
from iocbuilder import Device, AutoSubstitution, Architecture, Xml
from iocbuilder.arginfo import *

from iocbuilder.modules.seq import Seq
from iocbuilder.modules.ADCore import ADCore, NDStats, ADBaseTemplate, makeTemplateInstance, includesTemplates
from iocbuilder.modules.asyn import Asyn, AsynPort, AsynIP
from iocbuilder.modules.mca import Mca

class statPlugins(Xml):
    """This plugin instantiates 11 stats plugins for Current, Sum, Diff and Pos"""
    TemplateFile = 'statPlugins.xml'
statPlugins.ArgInfo.descriptions["NDARRAY_PORT"] = Ident("The quadEM.AH501 or quadEM.401 port to connect to", NDStats)
#statPlugins.ArgInfo.descriptions["STAT_NCHAN"] = Ident("Number of elements in the waveforms which accumulate statistics", NDStats)
#statPlugins.ArgInfo.descriptions["STAT_XSIZE"] = Ident("Maximum size of X histogram (must be <= RINGBUFFERSIZE)", NDStats)

class _QuadEMTemplate(AutoSubstitution):
    """Template containing the base records of any quedEM driver"""
    TemplateFile="quadEM.template"

class _QuadEM(AsynPort):
    """This Base class creates an quadEM.template and stores the arguments
    in __dict__. It is meant to be subclassed by specific quadEM driver"""
    # This the the Base template to use
    _BaseTemplate = _QuadEMTemplate
    # This is the template class to load
    _SpecificTemplate = None
    Dependencies = (ADCore,Seq,Mca)

    def __init__(self, PORT, **args):
        # Init the asyn port class
        self.__super.__init__(PORT)
	self.PORT = PORT
        # Init the base class
        makeTemplateInstance(self._BaseTemplate, locals(), args)
        # Init the template class
        if self._SpecificTemplate:
	  makeTemplateInstance(self._SpecificTemplate, locals(), args)

            #self.template = self._SpecificTemplate(**filter_dict(args,
                #self._SpecificTemplate.ArgInfo.Names()))
            #self.__dict__.update(self.template.args)
        # Update args
        #self.__dict__.update(self.base.args)
        #self.__dict__.update(args())

    LibFileList = ['quadEM']

    ArgInfo = _QuadEMTemplate.ArgInfo + makeArgInfo(__init__,
        PORT = Simple('Asyn port for QuadEM driver'),
    )
    # This tells xmlbuilder to use PORT instead of name as the row ID
    UniqueName = "PORT"

class _TetrAMMTemplate(AutoSubstitution, Device):
    TemplateFile = "TetrAMM.template"
    DbdFileList = ['drvTetrAMM']
    SubstitutionOverwrites = [_QuadEMTemplate]

class TetrAMM(_QuadEM):
    _SpecificTemplate = _TetrAMMTemplate
    def __init__(self, QSIZE = 20, RING_SIZE = 10000,
                 IP = "164.54.160.241:10001", **args):
         # # Make an asyn IP port
        self.ip = AsynIP(IP, name = args["PORT"] + "ip",
            input_eos = "\r\n", output_eos = "\r\n")
        self.__super.__init__(**args)
        self.__dict__.update(locals())

    # __init__ arguments
    ArgInfo = _QuadEM.ArgInfo + _SpecificTemplate.ArgInfo + makeArgInfo(__init__,
        QSIZE = Simple('..', int),
        RING_SIZE = Simple('..', int),
        IP = Simple('..'))

    def Initialise(self):
        print '# drvTetrAMMConfigure(portName, IPportName, RingSize)'
        print 'drvTetrAMMConfigure("%(PORT)s", "%(PORT)sip", ' \
            '%(RING_SIZE)d)' % self.__dict__

class _AH501Template(AutoSubstitution, Device):
    TemplateFile="AH501.template"
    DbdFileList = ['drvAHxxx']
    SubstitutionOverwrites = [_QuadEMTemplate]

class AH501(_QuadEM):
    """Create a AH501 quadEM detector"""
    _SpecificTemplate = _AH501Template
    def __init__(self, IPPORTNAME="", RINGBUFFERSIZE=10000, **args):
        self.__super.__init__(**args)
        self.__dict__.update(locals())

    # __init__ arguments
    ArgInfo = _QuadEM.ArgInfo + _SpecificTemplate.ArgInfo + makeArgInfo(__init__,
        IPPORTNAME = Simple('The name of the asyn communication IP port to the AH501', str),
        RINGBUFFERSIZE  = Simple('The number of samples to hold in the input ring buffer', int))

    def Initialise(self):
        print '# drvAHxxxConfigure(QEPortName, IPPortName, RingBufferSize)'
        print 'drvAHxxxConfigure("%(PORT)s", %(IPPORTNAME)s, %(RINGBUFFERSIZE)d)' % self.__dict__

class _AH401BTemplate(AutoSubstitution, Device):
    TemplateFile="AH401B.template"
    DbdFileList = ['drvAHxxx']
    SubstitutionOverwrites = [_QuadEMTemplate]

class AH401B(_QuadEM):
    """Create a AH401B quadEM detector"""
    _SpecificTemplate = _AH401BTemplate
    def __init__(self, IPPORTNAME="", RINGBUFFERSIZE=10000, **args):
        self.__super.__init__(**args)
        self.__dict__.update(locals())

    # __init__ arguments
    ArgInfo = _QuadEM.ArgInfo + _SpecificTemplate.ArgInfo + makeArgInfo(__init__,
        IPPORTNAME = Simple('The name of the asyn communication IP port to the AH401B', str),
        RINGBUFFERSIZE  = Simple('The number of samples to hold in the input ring buffer', int))

    def Initialise(self):
        print '# drvAHxxxConfigure(QEPortName, IPPortName, RingBufferSize)'
        print 'drvAHxxxConfigure("%(PORT)s", %(IPPORTNAME)s, %(RINGBUFFERSIZE)d)' % self.__dict__


class _quadEM_TimeSeriesTemplate(AutoSubstitution):
    "Template containing time series records"
    TemplateFile="quadEM_TimeSeries.template"

class quadEM_TimeSeries(AsynPort):
    """Create a time series using initFastSweep and quadEM_TimeSeries.Template"""
    #_BaseTemplate = _quadEM_TimeSeriesTemplate
    _SpecificTemplate = _quadEM_TimeSeriesTemplate
    Dependencies = (Mca,ADCore,Seq)

    def __init__(self,  QEPORTNAME="", MAXSIGNALS=11, MAXPOINTS=2048, DATASTR="QE_INT_ARRAY_DATA", INTERVALSTR="QE_SAMPLE_TIME", **args):
        # Init the asyn port class
        #self.__super.__init__(**args)
        self.__super.__init__(args["PORT"])
        # Init the base class
        #self.base = self._BaseTemplate(**filter_dict(args,
        #    self._BaseTemplate.ArgInfo.Names()))
        # Init the template class
        if self._SpecificTemplate:
            self.template = self._SpecificTemplate(**filter_dict(args,
                self._SpecificTemplate.ArgInfo.Names()))
            self.__dict__.update(self.template.args)
        # Update args
        #self.__dict__.update(self.base.args)
        self.__dict__.update(locals())

    # __init__ arguments
    #ArgInfo = _quadEM_TimeSeriesTemplate.ArgInfo
    # This tells xmlbuilder to use PORT instead of name as the row ID
    UniqueName = "PORT"

    #ArgInfo = _SpecificTemplate.ArgInfo + makeArgInfo(__init__,
    ArgInfo =  _SpecificTemplate.ArgInfo + makeArgInfo(__init__,
        #PORT = Simple('The name of the quadEM time series port', str),
        QEPORTNAME = Simple('The name of the quadEM port', str),
        MAXSIGNALS  = Simple('The maximum number of signals (spectra), usually 11', int),
        MAXPOINTS  = Simple('The maximum number of points per spectrum', int),
        DATASTR  = Simple('The drvInfo string for current and position data', str),
        INTERVALSTR  = Simple('The drvInfo string for time interval per point', str))

    def Initialise(self):
        print '# initFastSweep("PORT", "QEPORTNAME", MAXSIGNALS(11), MAXPOINTS, "DATASTR", "INTERVALSTR")'
        print 'initFastSweep("%(PORT)s", "%(QEPORTNAME)s", "%(MAXSIGNALS)d", "%(MAXPOINTS)d", "%(DATASTR)s", "%(INTERVALSTR)s")' % self.__dict__

    def PostIocInitialise(self):
        print '# Start the FFT seq program' 
        print 'seq("quadEM_SNL", "P=%(P)s, R=%(R)s, NUM_CHANNELS=%(NUM_TS)s")' % self.__dict__

