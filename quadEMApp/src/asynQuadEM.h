/* asynQuadEM.h

    Author:  Mark Rivers
    Created: April 10, 2003, based on Ip330.h

*/

#ifndef asynQuadEMH
#define asynQuadEMH

/* This is the scale factor to go from (difference/sum) to position.  The total
 * range will be -32767 to 32767, which preserves the full 16 bit range of the
 * device */
#define QUAD_EM_POS_SCALE 32767

/* drvQuadEM.c supports the following standard interfaces:
   asynInt32          - reads a single value from a channel
   asynFloat64        - reads a single value from a channel
   asynInt32Callback  - calls a function with a new value for a channel
                        whenever a new value is read
   asynFloat64Callback - calls a function with a new value for a channel
                        whenever a new value is read

   It also supports the device-specific inteface, asynQuadEM, defined below */

#define asynQuadEMType "asynQuadEM"

typedef struct {
    void (*setOffset)               (void *drvPvt, asynUser *pasynUser,
                                     int channel, int offset);
    void (*setPingPong)             (void *drvPvt, asynUser *pasynUser,
                                     int channel, int pingpong);
    void (*setPeriod)               (void *drvPvt, asynUser *pasynUser,
                                     int period);
    void (*setGain)                 (void *drvPvt, asynUser *pasynUser,
                                     int gain);
    void (*setPulse)                (void *drvPvt, asynUser *pasynUser,
                                     int pulse);
    void (*go)                      (void *drvPvt, asynUser *pasynUser);
} asynQuadEM;

#endif /* asynQuadEMH */
