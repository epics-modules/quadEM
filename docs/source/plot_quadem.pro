; Program to read HDF5 file for streamed quadEM data, plot it
data = h5_getdata('J:\epics\scratch\quadEM_HDF5_001.h5', '/entry/data/data')
help, data
; Convert from a 3-D array [11, 2000, 200] to a 2-D array [11, 400000]
data = reform(data, 11, 400000)
; Extract the SumAll data
sum_all = data[6, *]
; Compute the time axis, 50 microseconds/sample
time = findgen(400000) * 50e-6
; Plot the first 0.5 second of data
p1 = plot(time[0:9999], sum_all[0:9999], xtitle='Time (s)', ytitle='Sum of all diodes (nA)', $
          title='Time Series', color='blue')
p1.save, 'IDL_HDF5_time_plot.png'
; Compute the FFT
f = fft(sum_all)
; Take the absolute value, first half of array
f_abs = (abs(f))[0:199999]
; Set the DC offset to 0
f_abs[0] = 0
; Compute the frequency axis. The sampling rate is 20 kHz so the Nyquist frequency is 10 kHz
freq = findgen(200000)/199999. * 10000.
; Plot the data out to 1 kHz
p2 = plot(freq[0:19999], f_abs[0:199999], xtitle='Frequency (Hz)', ytitle='SumAll Intensity', $
          title='Power Spectrum', color='red', /ylog, yrange=[.01,100])
p2.save, 'IDL_HDF5_frequency_plot.png'
end
