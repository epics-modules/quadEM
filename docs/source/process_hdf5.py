import numpy as np
import h5py

# Windows/MacOS: pip install wxmplot
# Linux: canda install -c conda-forge wxpython && pip install wxmplot
from wxmplot.interactive import plot

h5root = h5py.File('quadem_HDF5_001.h5')
data = h5root['/entry/data/data'][()]
ny, nx, nchans = data.shape  # = (200, 2000, 11)
npts = ny*nx
data.shape = (npts, 11)
sum_all = data[:, 6]

sample_time = 50e-6
time = np.arange(npts)*sample_time

# Plot the first 0.5 second of data
p1 = plot(time[:10000], sum_all[:10000],
          xlabel='Time (s)', ylabel='Sum of all diodes (nA)', title='Time Series')
p1.save_figure('Py_HDF5_time_plot.png')

# Compute the FFT: note that for numpy.fft, the normalization is set with
#  'norm', which can be one of 'backward', 'forward, or 'ortho'.
#  'backward' is the NumPy default, 'forward' matches the IDL default.
f = np.fft.fft(sum_all, norm='forward')
# or one of:
# f = np.fft.fft(sum_all) / npts
# f = np.fft.fft(sum_all, norm='ortho') / np.sqrt(npts)

# Take the absolute value, first half of array
nfft = npts//2
f_abs = abs(f)[:nfft]

# Set the DC offset to 0
f_abs[0] = 0

# Compute the frequency axis.
# Note: The sampling rate is 20 kHz so the Nyquist frequency is 10 kHz
freq = np.arange(nfft)/(nfft*2*sample_time)
# or
# freq = np.fft.fftfreq(npts, sample_time)

# Plot the power spectrum out to 1 kHz
p2 = plot(freq[:20000], f_abs[:20000], ymin=0.01, ymax=100, ylog_scale=True,
          xlabel='Frequency (Hz)', ylabel=r'SumAll Intensity', color='red',
          title='Power Spectrum',  win=2)

p2.save_figure('Py_HDF5_frequency_plot.png')
