/* drvQuadEM.h

    Author:  Mark Rivers
    Created: April 10, 2003, based on Ip330.h

*/

#ifndef drvQuadEMH
#define drvQuadEMH

/* This is the scale factor to go from (difference/sum) to position.  The total
 * range will be -32767 to 32767, which preserves the full 16 bit range of the
 * device */
#define QUAD_EM_POS_SCALE 32767

typedef enum {quadEMCurrent, 
              quadEMOffset,
              quadEMGain, 
              quadEMPeriod,
              quadEMPingPong,
              quadEMPulse,
              quadEMGo,
              quadEMScanPeriod
} quadEMCommand;

#define MAX_QUADEM_COMMANDS 8

/* Implements the following asyn interfaces:
    Interface:          asynInt32   
    Method:             read   
    asynUser->drvUser:  0 or &quadEMData
    asynDrvUser->create "DATA"
    Description:        read the current value of a channel

    Interface:          asynInt32   
    Method:             write 
    asynUser->drvUser:  0 or &quadEMData 
    asynDrvUser->create "DATA"
    Description:        Error, undefined

    Interface:          asynInt32 
    Method:             read   
    asynUser->drvUser:  &quadEMOffset
    asynDrvUser->create "OFFSET"
    Description:        read the offset for a channel

    Interface:          asynInt32
    Method:             write 
    asynUser->drvUser:  &quadEMOffset
    asynDrvUser->create "OFFSET"
    Description:        write the offset for a channel

    Interface:          asynInt32 
    Method:             read   
    asynUser->drvUser:  &quadEMPulse
    asynDrvUser->create "PULSE"
    Description:        read the pulse for a channel

    Interface:          asynInt32
    Method:             write 
    asynUser->drvUser:  &quadEMPulse
    asynDrvUser->create "PULSE"
    Description:        write the pulse for a channel

    Interface:          asynInt32 
    Method:             read   
    asynUser->drvUser:  &quadEMGo
    asynDrvUser->create "GO"
    Description:        not supported, error

    Interface:          asynInt32
    Method:             write 
    asynUser->drvUser:  &quadEMGo
    asynDrvUser->create "GO"
    Description:        give the go command

    Interface:          asynInt32Callback 
    Method:             registerCallback
    asynUser->drvUser:  0 or &quadEMData
    asynDrvUser->create "DATA"
    Description:        register callback with the current value of a channel

    Interface:          asynUInt32Digital 
    Method:             read   
    asynUser->drvUser:  &quadEMGain
    asynDrvUser->create "GAIN"
    Description:        read the gain for a channel

    Interface:          asynUInt32Digitial
    Method:             write 
    asynUser->drvUser:  &quadEMGain 
    asynDrvUser->create "GAIN"
    Description:        write the gain for a channel

    Interface:          asyniUInt32Digital 
    Method:             read   
    asynUser->drvUser:  &quadEMPingPong
    asynDrvUser->create "PING_PONG"
    Description:        read the ping/pong value for a channel

    Interface:          asynIntU32Digital
    Method:             write 
    asynUser->drvUser:  &quadEMPingPong
    asynDrvUser->create "PING_PONG"
    Description:        write the ping/pong for a channel

    Interface:          asynFloat64 
    Method:             read
    asynUser->drvUser:  0 or &quadEMData 
    asynDrvUser->create "DATA"
    Description:        read the current value of a channel

    Interface:          asynFloat64
    Method:             write 
    asynUser->drvUser:  0 or &quadEMData
    asynDrvUser->create "DATA"
    Description:        Error, undefined

    Interface:          asynFloat64
    Method:             read
    asynUser->drvUser:  &quadEMScanPeriod 
    asynDrvUser->create "SCAN_PERIOD"
    Description:        Read the scan period

    Interface:          asynFloat64
    Method:             write
    asynUser->drvUser:  &quadEMScanPeriod
    asynDrvUser->create "SCAN_PERIOD"
    Description:        Write the scan period

    Interface:          asynFloat64Callback
    Method:             registerCallback
    asynUser->drvUser:  0 or &quadEMData
    asynDrvUser->create "DATA"
    Description:        Register callback with the current value of a channel

    Interface:          asynFloat64Callback
    Method:             registerCallback
    asynUser->drvUser:  &quadEMScanPeriod
    asynDrvUser->create "SCAN_PERIOD"
    Description:        Register callback with the new scan period
*/

#endif /* drvQuadEMH */
