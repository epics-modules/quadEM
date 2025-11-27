Streaming data to disk
----------------------

The example IOCs in quadEM load the file ADCore/iocBoot/commonPlugins.cmd.
This provides all the standard file plugins, including HDF5, netCDF, and TIFF.
These can be used to stream the quadEM data to disk, with no limit on
the duration except for disk file size.

As explained in the Plugins section, the NDArray callbacks to plugins occur
each time NumAverage_RBV samples have been received from the device.
Callbacks on address 11 contain all the data items, and the NDArray
dimensions are [11, NumAverage_RBV] for each NDArray callback.

This is the medm screen for the HDF5 plugin used with the TetrAMM.
The TetrAMM is configured with ValuesPerRead=5, so it is sending data at 20 kHz. 
AveragingTime is set to 0.1 seconds, so NumAverage_RBV is 2000.

.. figure:: HDF5_stream.png
    :align: center

The Array address in the plugin is set to 11, so it is receiving all 11 data items.  
FileWriteMode is set to Stream, so it will stream the NDArrays to the HDF5 file
as they arrive.  NumCapture is set to 200, so it will collect 200 arrays, each with 
0.1 second of data giving a total of 20 seconds saved to disk.
Pressing the Start button starts the streaming.

The resulting file, quadEM_HDF5_001.h5, is 34 MB.

This is the result of the Linux h5dump --contents on that file
::

  (base) [epics@corvette scratch]$ h5dump --contents quadEM_HDF5_001.h5
  HDF5 "quadEM_HDF5_001.h5" {
  FILE_CONTENTS {
   group      /
   group      /entry
   group      /entry/data
   dataset    /entry/data/data
   group      /entry/instrument
   group      /entry/instrument/NDAttributes
   dataset    /entry/instrument/NDAttributes/NDArrayEpicsTSSec
   dataset    /entry/instrument/NDAttributes/NDArrayEpicsTSnSec
   dataset    /entry/instrument/NDAttributes/NDArrayTimeStamp
   dataset    /entry/instrument/NDAttributes/NDArrayUniqueId
   group      /entry/instrument/detector
   group      /entry/instrument/detector/NDAttributes
   dataset    /entry/instrument/detector/data -> /entry/data/data
   group      /entry/instrument/performance
   dataset    /entry/instrument/performance/timestamp
   }
  }

The streamed data is in dataset /entry/data/data.  
This is the output of h5dump --header, looking at just that dataset.
::

         DATASET "data" {
            DATATYPE  H5T_IEEE_F64LE
            DATASPACE  SIMPLE { ( 200, 2000, 11 ) / ( 200, 2000, 11 ) }
            ATTRIBUTE "NDArrayDimBinning" {
               DATATYPE  H5T_STD_I32LE
               DATASPACE  SIMPLE { ( 2 ) / ( 2 ) }
            }
            ATTRIBUTE "NDArrayDimOffset" {
               DATATYPE  H5T_STD_I32LE
               DATASPACE  SIMPLE { ( 2 ) / ( 2 ) }
            }
            ATTRIBUTE "NDArrayDimReverse" {
               DATATYPE  H5T_STD_I32LE
               DATASPACE  SIMPLE { ( 2 ) / ( 2 ) }
            }
            ATTRIBUTE "NDArrayNumDims" {
               DATATYPE  H5T_STD_I32LE
               DATASPACE  SCALAR
            }
            ATTRIBUTE "signal" {
               DATATYPE  H5T_STD_I32LE
               DATASPACE  SCALAR
            }
         }
      }

It is IEEE 64-bit little-endian, with dimensions [200,2000,11].
In HDF5 convention the last array index is the fastest varying.

Saving all 11 data items is somewhat wastefull, since everything else can be calculated from just
the 4 currents, which are the first 4 items in the arrays.
We can save disk space by only saving the currents by using an ROI plugin between the 
quadEM driver and the HDF5 plugin.

This is the medm screen for an ROI plugin which gets its data on address 11 from the TetrAMM port.
It is configured with a start of 0 and size of 4 on the first array dimension, and so will 
select only the 4 currents.  The second dimension is set to auto size = Yes, so it will automatically
select all elements in the second dimension, even if NumAverage_RBV changes.

.. figure:: ROI_stream.png
    :align: center

The HDF5 plugin is changed to have its array port be ROI1, and the array address to be 0.
Note that the NDArrays being received are now [4, 2000].

.. figure:: HDF5_stream_ROI.png
    :align: center

The HDF5 plugin is changed to have its array port be ROI1, and the array address to be 0.
The filename is changed to quadEM_HDF5_ROI_001.h5.  The resulting file size is 13 MB.





