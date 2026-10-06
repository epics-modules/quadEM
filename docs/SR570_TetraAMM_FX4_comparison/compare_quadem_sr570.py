import epics
import time
import math
import csv
import numpy as np
from numpy.polynomial import Polynomial
import matplotlib.pyplot as plt
from scipy.optimize import curve_fit
from scipy.interpolate import make_smoothing_spline

class compare_quadem_sr570():
    def __init__(self, num_points=1000, time_per_point=0.1):
        self.usb1808_prefix  = '13MCBOX:USB1808:'
        self.dac_pv          = self.usb1808_prefix + 'Ao1'
        self.wavegen_prefix  = self.usb1808_prefix + 'WaveGen'
        self.wavegen1_prefix = self.usb1808_prefix + 'WaveGen1'
        self.wavedig_prefix  = self.usb1808_prefix + 'WaveDig'
        self.tetramm_prefix  = 'QE1:TetrAMM:'
        self.fx4_prefix      = 'QE1:FX4:'
        self.sr570_prefix    = '13TEST:A1'
        self.mcs_prefix      = '13MCBOX:MCS1:'
        self.num_points = num_points
        self.time_per_point = time_per_point
        self.wavegen_freq = 1/(self.num_points * self.time_per_point)
        self.constant_fraction = 0.01
        self.ramp_fraction = 1.0
        self.sin_fraction = 0.98
 
    def configure_current(self, mode, range):
        # Configures USB1808 to generate current for tests
        # mode = 'Constant' for constant current at 1% of vmax
        # mode = 'Ramp' for linear ramp from 0 to vmax
        # mode = 'Sin; for 1 cycle sin wave from 2.5% to 97.5% of vmax
        # range = '100nA' '1uA', '10uA', '100uA', '1mA', or 10 mA
        self.range = range
        match range:
          case '10mA':
            vmax = 3  # The normal FX4 is limited to 3 mA
          case _:
            vmax = 10
        if (mode == 'CorrectedRamp'):
          prefix = self.wavegen_prefix + 'User'
          volts = np.loadtxt('corrected_volts.csv', skiprows=1)
          epics.caput(self.wavegen1_prefix + 'UserWF', volts)
        else:
          prefix = self.wavegen_prefix + 'Int'
        epics.caput(prefix + 'Frequency', self.wavegen_freq)
        epics.caput(prefix + 'Dwell', self.time_per_point)
        epics.caput(prefix + 'NumPoints', self.num_points)          
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
          case 'CorrectedRamp':
            epics.caput(self.wavegen1_prefix + 'Type', 'User-defined')
            epics.caput(self.wavegen1_prefix + 'Amplitude', 1)
            epics.caput(self.wavegen1_prefix + 'Offset', 0)
            initial_value = 0
          case 'Sin':
            epics.caput(self.wavegen1_prefix + 'Type', 'Sin wave')
            epics.caput(self.wavegen1_prefix + 'Amplitude', self.sin_fraction*vmax)
            epics.caput(self.wavegen1_prefix + 'Offset', vmax/2.)
            initial_value = vmax/2.
        epics.caput(self.usb1808_prefix + 'Ao1', initial_value)
        time.sleep(1.0)
        epics.caput(self.wavedig_prefix + 'Dwell', self.time_per_point)
        epics.caput(self.wavedig_prefix + 'NumPoints', self.num_points)

    def start_current(self):
      epics.caput(self.wavegen_prefix + 'Run', 1)
      epics.caput(self.wavedig_prefix + 'Run', 1)
  
    def read_wavedig(self):
      return epics.caget(self.wavedig_prefix + '1VoltWF')

    def wait_wavedig(self):
      while (epics.caget(self.wavedig_prefix + 'Run') != 0):
        time.sleep(0.1)

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

    def configure_sr570(self):
      prefix = self.sr570_prefix
      epics.caput(prefix + 'filter_type', '12 dB lowpass')
      epics.caput(prefix + 'low_freq', 9) # 1 kHz
      # The offsets seem to drift so we set them manually before each measurement
      if (self.range == '100nA'):
        epics.caput(prefix + 'sens_unit', 'nA/V')
        epics.caput(prefix + 'sens_num', '20')
      elif (self.range == '1uA'):
        epics.caput(prefix + 'sens_unit', 'nA/V')
        epics.caput(prefix + 'sens_num', '200')
      elif (self.range == '10uA'):
        epics.caput(prefix + 'sens_unit', 'uA/V')
        epics.caput(prefix + 'sens_num', '2')
      elif (self.range == '100uA'):
        epics.caput(prefix + 'sens_unit', 'uA/V')
        epics.caput(prefix + 'sens_num', '20')
      elif (self.range == '1mA'):
        epics.caput(prefix + 'sens_unit', 'uA/V')
        epics.caput(prefix + 'sens_num', '200')
      elif (self.range == '10mA'):
        epics.caput(prefix + 'sens_unit', 'mA/V')
        epics.caput(prefix + 'sens_num', '2')

    def configure_quadem(self, model):
      if (model == 'TetrAMM'):
        prefix = self.tetramm_prefix
        match self.range:
          case '100nA':
            range = '+- 120 nA'
            epics.caput(prefix + 'CurrentScale1', '1e9')
          case _:
            range = '+- 120 uA'
            epics.caput(prefix + 'CurrentScale1', '1e6')
      else:
        prefix = self.fx4_prefix
        match self.range:
          case '100nA':
            range = '100 nA slow'
            epics.caput(prefix + 'CurrentUnits', 'nA')
          case '1uA':
            range = '1 uA slow'
            epics.caput(prefix + 'CurrentUnits', 'uA')
          case '10uA':
            range = '10 uA'
            epics.caput(prefix + 'CurrentUnits', 'uA')
          case '100uA':
            range = '100 uA'
            epics.caput(prefix + 'CurrentUnits', 'uA')
          case '1mA':
            range = '1 mA'
            epics.caput(prefix + 'CurrentUnits', 'mA')
          case '10mA':
            range = '10 mA'
            epics.caput(prefix + 'CurrentUnits', 'mA')
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

    def test_v2f100(self, mode, delay=0):
      range = '100nA' # Range not used but required
      self.configure_current(mode, range) 
      self.configure_mcs()
      self.start_current()
      if (delay != 0):
        time.sleep(delay)
      self.start_mcs()
      time.sleep(0.1)
      self.wait_mcs()
      time.sleep(0.1)
      current_data = self.read_mcs()
      print(current_data)
      self.save_data(current_data, mode, range, 'V2F100')

    def test_sr570(self, mode, range, delay=0):
      self.configure_current(mode, range)
      self.configure_sr570()
      time.sleep(1.)
      self.configure_mcs()
      self.start_current()
      if (delay != 0):
        time.sleep(delay)
      self.start_mcs()
      time.sleep(0.1)
      self.wait_mcs()
      time.sleep(0.1)
      current_data = self.read_mcs()
      print(current_data)
      self.save_data(current_data, mode, range, 'SR570')
      
    def test_quadem(self, mode, range, model, delay=0):
      self.configure_current(mode, range)
      self.configure_quadem(model)
      self.start_quadem()
      if (delay != 0):
        time.sleep(delay)
      self.start_current()
      time.sleep(0.1)
      self.wait_quadem()
      self.wait_wavedig()
      time.sleep(0.1)
      current_data = self.read_quadem()
      print(current_data)
      self.save_data(current_data, mode, range, model)

def sine_model(x, amplitude, frequency, phase, offset):
    return amplitude * np.sin(frequency * 2. * math.pi * x + phase) + offset

class analyze_quadem_sr570():
      def __init__(self):
        self.usb1808_prefix  = '13MCBOX:USB1808:'
        self.wavedig_prefix  = self.usb1808_prefix + 'WaveDig'
        self.wavegen_prefix  = self.usb1808_prefix + 'WaveGen'

      def read_file(self, filename, skip):
        data = np.genfromtxt(filename, delimiter=' ', dtype=None, skip_header=1)
        # Throw away first column
        data = data[:,1]
        # Throw away first 2 and last 2 values because of timing jitter
        data = data[skip[0]:-skip[1]]
        if 'SR570' in filename:  # For SR570 convert counts to current
          offset = min(data)
          if '_100' in filename:
            scale = 100.
          elif '_10mA' in filename:
            # Should be 2 mA/V but SR570 only allows 1 mA/V
            scale = 5.
          elif '_10' in filename:
            scale = 10.
          else:
            scale = 1.
          data = (data - offset)/1250000. * scale
        return data

      def analyze_constant(self, filename, skip=[2,2]):
        data = self.read_file(filename, skip=skip)
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

      def analyze_ramp(self, filename, skip=[2,2]):
        data = self.read_file(filename, skip=skip)
        time = np.arange(data.size)*0.1
        if 'nA' in filename:
          data_range = 'nA'
          sigma_range = 'pA'
        elif 'uA' in filename:
          data_range = 'uA'
          sigma_range = 'nA'
        elif 'mA' in filename:
          data_range = 'mA'
          sigma_range = 'uA'
        linear_model = Polynomial.fit(time, data, deg=1)
        b, m = linear_model.convert().coef
        predicted = b + m*time
        errors = (data - predicted)*1000.
        mean = np.mean(errors)
        stddev = np.std(errors)
        stddev_ppm = stddev/max(data)*1e3
        title = f'Sigma = {stddev:.4f} {sigma_range} ({stddev_ppm:.1f} ppm of maximum current)'
        print(title)
        # Plot ramp
        plt.plot(time, data)
        plt.ylabel('Current (' + data_range + ')')
        plt.xlabel('Time (s)')
        plt.suptitle(filename)
        plt.title('Ramp data')
        plotfile = filename.replace('.csv', '.png')
        plt.savefig(plotfile)
        plt.show()
        # Plot errors
        plt.plot(time, errors)
        plot_limit = max(data)*0.4
        plt.ylim(-plot_limit, plot_limit)
        plt.ylabel('Error (' + sigma_range + ')')
        plt.xlabel('Time (s)')
        plt.suptitle(filename)
        plt.title(title)
        plotfile = filename.replace('.csv', 'errors.png')
        plt.savefig(plotfile)
        plt.show()

      def analyze_wavedig(self, filename, output_filename=None):
        data = np.genfromtxt(filename, delimiter=' ', dtype=None, skip_header=1)
        # Throw away second column
        data = data[:, 0]
        requested_volts = np.arange(0, 10, 0.01001001)
        errors = (data - requested_volts)*1e3
        spl = make_smoothing_spline(requested_volts, errors, lam=None)
        smoothed_errors = spl(requested_volts)
        mean = np.mean(errors)
        stddev = np.std(errors)
        title = f'Sigma = {stddev:.3f} mV'
        print(title)
        # Plot ramp
        plt.plot(requested_volts, data)
        plt.ylabel('Actual Volts')
        plt.xlabel('Requested Volts (s)')
        plt.suptitle('Waveform digitizer data')
        plt.title('Ramp waveform digitizer data')
        plotfile = 'waveform_digitizer.png'
        plt.savefig(plotfile)
        plt.show()
        # Plot errors
        plt.plot(requested_volts, errors)
        plt.plot(requested_volts, smoothed_errors, color='red')
        plot_limit = 2
        #plt.ylim(-plot_limit, plot_limit)
        plt.ylabel('Error (mV)')
        plt.xlabel('Requested volts (s)')
        plt.suptitle('Waveform digitizer')
        plt.title(title)
        plotfile = 'waveform_digitizer_errors.png'
        plt.savefig(plotfile)
        plt.show()
        # Constructed a corrected voltage array
        new_volts = requested_volts - smoothed_errors/1000.
        if (output_filename != None):
          with open(output_filename, "w", newline="") as file:
            writer = csv.writer(file)
            writer.writerow(["Volts"]) 
            for v in new_volts:
              writer.writerow([v])
            
      def analyze_v2f_ramp(self, filename, skip=[2,2]):
        data = self.read_file(filename, skip=skip)
        time = np.arange(data.size)*0.1
        linear_model = Polynomial.fit(time, data, deg=1)
        b, m = linear_model.convert().coef
        predicted = b + m*time
        errors = (data - predicted)
        mean = np.mean(errors)
        stddev = np.std(errors)
        stddev_ppm = stddev/max(data)*1e6
        title = f'Sigma = {stddev:.1f} counts ({stddev_ppm:.1f} ppm of maximum counts)'
        print(title)
        # Plot ramp
        plt.plot(time, data)
        plt.ylabel('Counts')
        plt.xlabel('Time (s)')
        plt.suptitle(filename)
        plt.title('Ramp data')
        plotfile = filename.replace('.csv', '.png')
        plt.savefig(plotfile)
        plt.show()
        # Plot errors
        plt.plot(time, errors)
        plot_limit = 400
        plt.ylim(-plot_limit, plot_limit)
        plt.ylabel('Error (counts)')
        plt.xlabel('Time (s)')
        plt.suptitle(filename)
        plt.title(title)
        plotfile = filename.replace('.csv', 'errors.png')
        plt.savefig(plotfile)
        plt.show()

      def analyze_sin(self, filename, skip=[2,2]):
        data = self.read_file(filename,skip=skip)
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
