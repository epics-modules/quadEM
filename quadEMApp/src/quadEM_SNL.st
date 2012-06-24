program quadEM_SNL("P=quadEMTest:, R=QE1:, ARRAY=mca, NUM_SIGNALS=11, NUM_CHANNELS=2048, FIELD=READ")

/*  This sequencer program works with quadEM_TimeSeries.template and quadEM_FFT.template
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

/* Maximum number of detectors supported */
#define MAX_SIGNALS 11
#define MAX_CHANNELS 2048

int i;
int n;
int numSignals;
int numChannels;
char temp[100];

char *prefix;
char *record;
char *array;
char *field;

int     ReadArray[MAX_SIGNALS]; 
assign  ReadArray to
          {"","","","","","",
           "","","","",""};
int     ReadArrays; assign ReadArrays to "{P}{R}DoReadAll";
monitor ReadArrays; evflag ReadArraysMon; sync ReadArrays ReadArraysMon;

double  TimeArray[MAX_CHANNELS];
assign  TimeArray to "{P}{R}Xaxis";

double  DwellTime; assign DwellTime to "{P}{R}Dwell";
monitor DwellTime; evflag DwellTimeMon; sync DwellTime DwellTimeMon;

int     HardwareAcquiring; assign HardwareAcquiring to "{P}{R}HardwareAcquiring";
monitor HardwareAcquiring; evflag HardwareAcquiringMon; sync HardwareAcquiring HardwareAcquiringMon;

int     SNL_Connected;  assign SNL_Connected      to "{P}{R}SNL_Connected";
int     AsynDebug;      assign AsynDebug          to "{P}{R}Asyn.TB1";
monitor AsynDebug;

int Acquiring; assign Acquiring to "{P}{R}Acquiring";

ss mca_control {
  state init {
    when() {
      prefix = macValueGet("P");
      record = macValueGet("R");
      array = macValueGet("ARRAY");
      field = macValueGet("FIELD");
      numSignals = atoi(macValueGet("NUM_SIGNALS"));
      numChannels = atoi(macValueGet("NUM_CHANNELS"));
      for (i=0; i<numSignals; i++) {
        n = i+1;
        sprintf(temp, "%s%s%s%d.%s", prefix, record, array, n, field);
        pvAssign(ReadArray[i], temp);
       }
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

    when(efTestAndClear(DwellTimeMon)) {
      if (AsynDebug) printf("quadEM_SNL.st: Computing time array\n");
      for (i=0; i<numChannels; i++) {
        TimeArray[i] = i*DwellTime;
      }
      pvPut(TimeArray);
    } state monitor_changes

    when(efTestAndClear(ReadArraysMon) && (ReadArrays == 1)) {
      if (AsynDebug) printf("quadEM_SNL.st: Read array data\n");
      for (i=0; i<numSignals; i++) {
        ReadArray[i] = 1;
        pvPut(ReadArray[i]);
      }
      ReadArrays = 0;
      pvPut(ReadArrays);
    } state monitor_changes

    when(efTestAndClear(HardwareAcquiringMon)) {
      if (AsynDebug) printf("quadEM_SNL.st: HardwareAcquiringMon, HardwareAcquiring=%d\n", HardwareAcquiring);
      /* If the detector is acquiring then force each record to read status */
      if (!HardwareAcquiring) {
        /* If the detector is done then force each record to read data */
        for (i=0; i<numSignals; i++) {
          ReadArray[i] = 1;
          pvPut(ReadArray[i], SYNC);
        }
        /* Clear the Acquiring busy record */
        Acquiring = 0;
        pvPut(Acquiring);
      }
    } state monitor_changes
  }
}

