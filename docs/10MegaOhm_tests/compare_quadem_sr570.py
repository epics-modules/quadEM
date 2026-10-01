import epics
import time
import math
import numpy as np
from numpy.polynomial import Polynomial
import matplotlib.pyplot as plt
from scipy.optimize import curve_fit

class compare_quadem_sr570():
    def __init__(self, num_points=1000, time_per_point=0.1):
        self.usb1808_prefix  = '13MCBOX:USB1808:'
        self.dac_pv          = self.usb1808_prefix + 'Ao1'
        self.wavegen_prefix  = self.usb1808_prefix + 'WaveGen'
        self.wavegen1_prefix = self.usb1808_prefix + 'WaveGen1'
        self.wavedig_prefix  = self.usb1808_prefix + 'WaveDig'
        self.tetramm_prefix  = 'QE1:TetrAMM:'
        self.fx4_prefix      = 'QE1:FX4:'
        self.mcs_prefix      = '13MCBOX:MCS1:'
        self.num_points = num_points
        self.time_per_point = time_per_point
        self.wavegen_freq = 1/(self.num_points * self.time_per_point)
        self.constant_fraction = 0.01
        self.ramp_fraction = 1.0
        self.sin_fraction = 0.98
        self.current_mode = 0
 
    def configure_current(self, mode, range):
        # Configures USB1808 to generate current for tests
        # mode = 'Constant' for constant current at 1% of vmax
        # mode = 'Ramp' for linear ramp from 0 to vmax
        # mode = 'Sin; for 1 cycle sin wave from 2.5% to 97.5% of vmax
        # range = '100nA' for 100 nA current
        # range = '100uA' for 100 uA current
        # vmax = 1 for 100 nA test (10M Ohm) resistor
        # vmax = 10 for 100 uA test with 100k Ohm resistor
        self.current_mode = mode
        self.range = range
        if (self.range == '100nA'):
          vmax = 1
        else:
          vmax = 10
        epics.caput(self.wavegen_prefix + 'IntFrequency', self.wavegen_freq)
        epics.caput(self.wavegen_prefix + 'IntDwell', self.time_per_point)
        epics.caput(self.wavegen_prefix + 'IntNumPoints', self.num_points)
        match mode:
          case 'Constant': 
            epics.caput(self.wavegen1_prefix + 'Type', 'Sawtooth')
            epics.caput(self.wavegen1_prefix + 'Amplitude', 0)
            epics.caput(self.wavegen1_prefix + 'Offset', self.constant_fraction * vmax)
            initial_value = self.constant_fraction * vmax
          case 'Ramp':
            epics.caput(self.wavegen1_prefix + 'Type', 'Sawtooth')
            epics.caput(self.wavegen1_prefix + 'Amplitude', vmax)
            epics.caput(self.wavegen1_prefix + 'Offset', vmax/2.)
            initial_value = 0
          case 'Sin':
            epics.caput(self.wavegen1_prefix + 'Type', 'Sin wave')
            epics.caput(self.wavegen1_prefix + 'Amplitude', self.sin_fraction*vmax)
            epics.caput(self.wavegen1_prefix + 'Offset', vmax/2.)
            initial_value = vmax/2.
        epics.caput(self.usb1808_prefix + 'Ao1', initial_value)
        time.sleep(0.1)
        epics.caput(self.wavedig_prefix + 'Dwell', self.time_per_point)
        epics.caput(self.wavedig_prefix + 'NumPoints', self.num_points)

    def start_current(self):
      epics.caput(self.wavegen_prefix + 'Run', 1)
      epics.caput(self.wavedig_prefix + 'Run', 1)
      
    def read_wavedig(self):
      return epics.caget(self.wavedig_prefix + '1VoltWF')

    def configure_mcs(self):
      epics.caput(self.mcs_prefix + 'Dwell', self.time_per_point)
      epics.caput(self.mcs_prefix + 'ChannelAdvance', 'Internal')
      epics.caput(self.mcs_prefix + 'NuseAll', self.num_points)
    
    def start_mcs(self):
      epics.caput(self.mcs_prefix + 'EraseStart', 1)
      
    def read_mcs(self):
      return epics.caget(self.mcs_prefix + 'mca2')
      
    def wait_mcs(self):
      while (epics.caget(self.mcs_prefix + 'Acquiring') != 0):
        time.sleep(0.1)

    def configure_quadem(self, model):
      if (model == 'TetrAMM'):
        prefix = self.tetramm_prefix
        if (self.range == '100nA'):
          range = '+- 120 nA'
          epics.caput(prefix + 'CurrentScale1', '1e9')
        else:
          range = '+- 120 uA'
          epics.caput(prefix + 'CurrentScale1', '1e6')
      else:
        prefix = self.fx4_prefix
        if (self.range == '100nA'):
          range = '100 nA slow'
          epics.caput(prefix + 'CurrentUnits', 'nA')
        else:
          range = '100 uA'
          epics.caput(prefix + 'CurrentUnits', 'uA')
      self.quadem_prefix = prefix
      epics.caput(prefix + 'ValuesPerRead', 100)
      epics.caput(prefix + 'Range', range)
      epics.caput(prefix + 'TS:TSAveragingTime', self.time_per_point)
      epics.caput(prefix + 'TS:TSAcquireMode', 'Fixed length')
      epics.caput(prefix + 'TS:TSNumPoints', self.num_points)
   
    def start_quadem(self):
      epics.caput(self.quadem_prefix + 'TS:TSAcquire', 1)
 
    def read_quadem(self):
      return epics.caget(self.quadem_prefix + 'TS:Current1:TimeSeries')

    def wait_quadem(self):
      while (epics.caget(self.quadem_prefix + 'TS:TSAcquiring') != 0):
        time.sleep(0.1)
   
    def save_data(self, current_data, mode, range, model):
      wavedig_data = self.read_wavedig()
      data = np.column_stack((wavedig_data, current_data))
      filename = model + '_' + range + '_' + mode + '_' + str(self.time_per_point) + 's_.csv'
      np.savetxt(filename, data, header='Voltage,Current', fmt='%.5f')

    def test_sr570(self, mode, range):
      self.configure_current(mode, range)
      self.configure_mcs()
      self.start_current()
      self.start_mcs()
      time.sleep(0.1)
      self.wait_mcs()
      time.sleep(0.1)
      current_data = self.read_mcs()
      print(current_data)
      self.save_data(current_data, mode, range, 'SR570')
      
    def test_quadem(self, mode, range, model, delay=0.02):
      self.configure_current(mode, range)
      self.configure_quadem(model)
      self.start_current()
      time.sleep(delay)
      self.start_quadem()
      time.sleep(0.1)
      self.wait_quadem()
      time.sleep(0.1)
      current_data = self.read_quadem()
      print(current_data)
      self.save_data(current_data, mode, range, model)

def sine_model(x, amplitude, frequency, phase, offset):
    return amplitude * np.sin(frequency * 2. * math.pi * x + phase) + offset

class analyze_quadem_sr570():
      def __init__(self):
        self.junk = 0

      def read_file(self, filename):
        data = np.genfromtxt(filename, delimiter=' ', dtype=None, skip_header=1)
        # Throw away first column
        data = data[:,1]
        # Throw away first 2 and last 2 values because of timing jitter
        data = data[2:-2]
        if 'SR570' in filename:  # For SR570 convert counts to current
          if 'VF#1' in filename:
            if 'nA' in filename:
              offset = 2204.
              scale = 100. / (722404. - offset)
            else:
              offset = 653.
              scale = 100 / (750966. - offset)
          else:
            if 'nA' in filename:
              offset = 4449.
              scale = 100. / (496075. - offset)
            else:
              offset = 1298.
              scale = 100. / (504893. - offset)
          data = (data - offset) * scale
        return data

      def analyze_constant(self, filename):
        data = self.read_file(filename)
        time = np.arange(data.size)*0.1
        range = 'uA' if 'uA' in filename else 'nA'
        mean = np.mean(data)
        stddev = np.std(data)
        title = f'Mean:{mean:.4f}, sigma:{stddev:.5f} {range}'
        print(title)
        plt.plot(time, data)
        plt.ylim(mean*.99, mean*1.01)
        plt.ylabel('Current (' + range + ')')
        plt.xlabel('Time (s)')
        plt.suptitle(filename)
        plt.title(title)
        plotfile = filename.replace('.csv', '.png')
        plt.savefig(plotfile)
        plt.show()

      def analyze_ramp(self, filename):
        data = self.read_file(filename)
        time = np.arange(data.size)*0.1
        range = 'uA' if 'uA' in filename else 'nA'
        linear_model = Polynomial.fit(time, data, deg=1)
        b, m = linear_model.convert().coef
        predicted = b + m*time
        errors = data - predicted
        mean = np.mean(errors)
        stddev = np.std(errors)
        title = f'Sigma = {stddev:.4f} {range}'
        print(title)
        # Plot ramp
        plt.plot(time, data)
        plt.ylabel('Data (' + range + ')')
        plt.xlabel('Time (s)')
        plt.suptitle(filename)
        plt.title('Ramp data')
        plotfile = filename.replace('.csv', '.png')
        plt.savefig(plotfile)
        plt.show()
        # Plot errors
        plt.plot(time, errors)
        plt.ylim(-.04, .04)
        plt.ylabel('Error (' + range + ')')
        plt.xlabel('Time (s)')
        plt.suptitle(filename)
        plt.title(title)
        plotfile = filename.replace('.csv', 'errors.png')
        plt.savefig(plotfile)
        plt.show()

      def analyze_sin(self, filename):
        data = self.read_file(filename)
        time = np.arange(data.size)*0.1
        range = 'uA' if 'uA' in filename else 'nA'
        #initial_guess = [guess_amplitude, guess_frequency, guess_phase, guess_offset]
        initial_guess = [50., 0.01, 0, 50.]
        popt, pcov = curve_fit(sine_model, time, data, p0=initial_guess)
        fit_amp, fit_freq, fit_phase, fit_offset = popt
        predicted = sine_model(time, fit_amp, fit_freq, fit_phase, fit_offset)
        errors = data - predicted
        mean = np.mean(errors)
        stddev = np.std(errors)
        title = f"Amplitude: {fit_amp:.4f} Frequency: {fit_freq:.4f}, Phase: {fit_phase:.4f}, Offset: {fit_offset:.4f}"
        print(title)
        # Plot data
        plt.plot(time, data)
        #plt.plot(time, predicted, color='red')
        plt.ylabel('Current (' + range + ')')
        plt.xlabel('Time (s)')
        plt.suptitle(filename)
        plt.title(title, fontsize=10)
        plotfile = filename.replace('.csv', '.png')
        plt.savefig(plotfile)
        plt.show()
        # Plot errors
        plt.plot(time, errors)
        plt.ylim(-.04, .04)
        plt.ylabel('Error (' + range + ')')
        plt.xlabel('Time (s)')
        plt.suptitle(filename)
        title = f"Sigma: {stddev:.4f} {range}"
        plt.title(title)
        plotfile = filename.replace('.csv', 'errors.png')
        plt.savefig(plotfile)
        plt.show()

