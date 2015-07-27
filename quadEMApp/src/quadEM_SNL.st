program quadEM_SNL("P=quadEMTest:, R=QE1:")

/*  This sequencer program works with quadEM_TimeSeries.template
 *  It does the following:
 *  Forces all the mca or waveform records to read when the ReadAll PV processes or when acquisition completes
 *  Constructs the time-base array when the dwell time changes
 *  Computes the FFT when acquisition completes if FFT computation is enabled
 *
 *  Mark Rivers
 *  June 18, 2012
 *
 * This program must be compiled with the recursive option so that multiple copies
 * can run at once */
option +r;

/* Allow the program to start, even if not all assigned channels are connected.
 * This could be useful if the SNL is running on a different ioc. */
option -c;

%%#include <stdlib.h>
%%#include <errlog.h>
%%#include <string.h>
%%#include <math.h>

%%extern void four1(float data[], unsigned long nn, int isign);


/* Maximum number of detectors supported */
#define MAX_SIGNALS 11
#define MAX_CHANNELS 2048

int     Acquiring; assign Acquiring to "{P}{R}Acquiring";

int     EraseStart; assign EraseStart to "{P}{R}EraseStart";

int     NumChannels; assign NumChannels to "{P}{R}NuseAll"; 
monitor NumChannels; evflag NumChannelsMon; sync NumChannels NumChannelsMon;

int     TimeSeriesProc[MAX_SIGNALS]; 
assign  TimeSeriesProc to
          {"{P}{R}Current1TS.PROC","{P}{R}Current2TS.PROC","{P}{R}Current3TS.PROC","{P}{R}Current4TS.PROC",
           "{P}{R}SumXTS.PROC","{P}{R}SumYTS.PROC","{P}{R}SumAllTS.PROC",
           "{P}{R}DiffXTS.PROC","{P}{R}DiffYTS.PROC","{P}{R}PositionXTS.PROC","{P}{R}PositionYTS.PROC"};

int     TimeSeriesData[MAX_SIGNALS][MAX_CHANNELS]; 
assign  TimeSeriesData to
          {"{P}{R}Current1TS.VAL","{P}{R}Current2TS.VAL","{P}{R}Current3TS.VAL","{P}{R}Current4TS.VAL",
            "{P}{R}SumXTS.VAL","{P}{R}SumYTS.VAL", "{P}{R}SumAllTS.VAL",
            "{P}{R}DiffXTS.VAL","{P}{R}DiffYTS.VAL","{P}{R}PositionXTS.VAL","{P}{R}PositionYTS.VAL"};

int     ReadTimeSeries; assign ReadTimeSeries to "{P}{R}DoReadAll";
monitor ReadTimeSeries; evflag ReadTimeSeriesMon; sync ReadTimeSeries ReadTimeSeriesMon;

float   FFTData[MAX_SIGNALS][MAX_CHANNELS]; 
assign  FFTData to
          {"{P}{R}Current1FFT.VAL","{P}{R}Current2FFT.VAL","{P}{R}Current3FFT.VAL","{P}{R}Current4FFT.VAL",
            "{P}{R}SumXFFT.VAL","{P}{R}SumYFFT.VAL", "{P}{R}SumAllFFT.VAL",
            "{P}{R}DiffXFFT.VAL","{P}{R}DiffYFFT.VAL","{P}{R}PositionXFFT.VAL","{P}{R}PositionYFFT.VAL"};

float   TimeArray[MAX_CHANNELS];
assign  TimeArray to "{P}{R}TimeAxis";

float   FreqArray[MAX_CHANNELS];
assign  FreqArray to "{P}{R}FreqAxis";

double  DwellTime; assign DwellTime to "{P}{R}Dwell_RBV";
monitor DwellTime; evflag DwellTimeMon; sync DwellTime DwellTimeMon;

int     HardwareAcquiring; assign HardwareAcquiring to "{P}{R}HardwareAcquiring";
monitor HardwareAcquiring; evflag HardwareAcquiringMon; sync HardwareAcquiring HardwareAcquiringMon;

int     ComputeFFT; assign ComputeFFT to "{P}{R}ComputeFFT";
monitor ComputeFFT;

int     AutoRestart; assign AutoRestart to "{P}{R}AutoRestartTS";
monitor AutoRestart;

int     SNL_Connected;  assign SNL_Connected      to "{P}{R}SNL_Connected";

int     AsynDebug;      assign AsynDebug          to "{P}{R}Asyn.TB1";
monitor AsynDebug;

int i;
int j;
int n;
float   freqStep;
float   tempData[4096];

ss mca_control {
  state init {
    when() {
    } state waitConnected
  }

  state waitConnected {
    when (pvAssignCount () == pvConnectCount ()) {
      printf ("quadEM_TimeSeries: All channels connected.\n");
      SNL_Connected = 1;
      pvPut(SNL_Connected);
    } state monitor_changes
  }

  state monitor_changes {
    when (pvAssignCount () != pvConnectCount ()) {
      printf ("SIS: Not all channels connected.\n");
    } state waitConnected

    when(efTestAndClear(DwellTimeMon) || efTestAndClear(NumChannelsMon)) {
      if (AsynDebug) printf("quadEM_SNL.st: Computing time array\n");
      for (i=0; i<MAX_CHANNELS; i++) {
        TimeArray[i] = (float)(i*DwellTime);
      }
      pvPut(TimeArray);
      freqStep = 1./DwellTime/(NumChannels-2);
      for (i=0; i<MAX_CHANNELS; i++) {
        FreqArray[i] = (float)(i*freqStep);
      }
      pvPut(FreqArray);
    } state monitor_changes

    when(efTestAndClear(ReadTimeSeriesMon) && (ReadTimeSeries == 1)) {
      if (AsynDebug) printf("quadEM_SNL.st: Read array data\n");
      for (i=0; i<MAX_SIGNALS; i++) {
        TimeSeriesProc[i] = 1;
        pvPut(TimeSeriesProc[i]);
      }
      ReadTimeSeries = 0;
      pvPut(ReadTimeSeries);
    } state monitor_changes

    when(efTestAndClear(HardwareAcquiringMon)) {
      if (AsynDebug) printf("quadEM_SNL.st: HardwareAcquiringMon, HardwareAcquiring=%d\n", HardwareAcquiring);
      /* If the detector is acquiring then force each record to read status */
      if (!HardwareAcquiring) {
        /* If the detector is done then force each record to read data */
        for (i=0; i<MAX_SIGNALS; i++) {
          TimeSeriesProc[i] = 1;
          pvPut(TimeSeriesProc[i], SYNC);
        }
        if (ComputeFFT) {
          /* Compute the FFTs of each array */
          for (i=0; i<MAX_SIGNALS; i++) {
            pvGet(TimeSeriesData[i]);
            memset(tempData, 0, MAX_CHANNELS*4);
            for (j=0; j<NumChannels; j++) {
              tempData[2*j] = (float)TimeSeriesData[i][j];
              tempData[2*j+1] = 0.;
            }
            four1(tempData-1, NumChannels, 1);
            memset(FFTData[i], 0, MAX_CHANNELS*4);
            /* Start at 1 so we don't copy DC offset which messes up scaling of plots */
            for (j=1; j<NumChannels/2; j++) {
              FFTData[i][j] = (float)sqrt((tempData[2*j]*tempData[2*j] + 
                                    tempData[2*j+1]*tempData[2*j+1])
                                    / (NumChannels * NumChannels));
            }
            pvPut(FFTData[i]);
          }
        }          
        /* Clear the Acquiring busy record */
        Acquiring = 0;
        pvPut(Acquiring);
        if (AutoRestart) {
          EraseStart = 1;
          pvPut(EraseStart);
        }
      }
    } state monitor_changes
  }
}

