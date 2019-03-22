#include <sstream>
#include <sys/timeb.h>
#include <stdio.h>
#include "msgtool.h"
#include "lock.h"
#include <cstdlib>
#include <unistd.h>

#define MAXNUMVALVES 8
#define MAXSCHEDENTRIES 256

//valve and sensor measurement functions which should be implemented for any DAQ used with this code
int chanOn(int);
int chanOff();
float measure(int);

//data structures for fill schedule
typedef struct {
    char entryName[256]; //the name of the entry, shown when the entry is run
		int valves[MAXNUMVALVES]; //list of valves to be opened, in the order they re opened in
		int overflowSensor; //overflow (temperature) sensor input
		int numValves; //number of valves in the list
		int schedMode; //0 to 6=specific day and time (0=sunday,1=monday,...), 7=interval in minutes
		int schedHour,schedMin; //parameters for scheduling frequency(number of minutes, time of day, etc.)
		int schedFlag; //1=fill has been triggered
		int hasBeenTriggered; //0 if never triggered before
		double lastTriggerTime; //time of last fill in minutes, used to determine whether to trigger a fill
} SchedEntry;

typedef struct {
    SchedEntry sched[MAXSCHEDENTRIES]; //the individual entries in the fill schedule
		int numEntries; //number of total fill schedule entries
} FillSched;


//the Signals 
struct Signals
{
  bool BEGIN;
  bool END;
  bool TIME;
  bool EXIT;
  bool RUNNING;
  bool PLOT;
  bool SAVE;
  bool NETSAVE;
  bool ON;
  bool OFF;
  bool MEASURE;
  bool FILLING;
  bool FILL;
  bool LIST;
};

class Crate
{
public:
  Crate();
  ~Crate();
  int Boot(FillSched*);
  int MainLoop(FillSched*);
  int recordMeasurement(FillSched*);
  void ReadCommand (struct Signals*, char*);
  void ProcessSignal (FillSched*);
  int BeginRun(void);
  int EndRun(FillSched*);
  int PauseRun(void);
  int ResumeRun(void);
  int ClearSpectrum(void);
  int Save(FillSched*, char*);
  int NetSave(FillSched*, char*);
  int ChanOn(int chan);
  int ChanOff(void);
  int getPlot(FillSched*);
  float Measure(int channel);
  double GetTime(void);
  int fill(FillSched*,int);
  int autosaveData(FillSched*);
  int readParameters(void);
  int readConnections(void);
  int readCalibration(void);
	void readSchedule(FillSched*);
  double findTemp(double vSensor, int sensorPort);
  double findWeight(double vScale);

private:
	struct Signals signaled;
	MsgQ *msg;
	lock *l;
	// Channel parameters

	char port0[128];
	char port1[128];
	char port2[128];

	/*TaskHandle  taskHandleRead, taskHandleWrite;
	int32       error;
	char        errBuff[2048];

	// Read parameters
	uInt32      r_data [read_ports];
	int32       read;
	// Write parameters
	uInt32      w_data[write_ports];
	int32       written;*/

	struct timeb tstart,tstop,pstart,pstop,tcurrent;
	double paused;
	float meas;
	double run_time;
	double current_run_time;
	float reading;
	char tmp [200]; //for temporary storage of content in parameter file
	bool emailAllow; //trigger to allow or disallow e-mail, set by program
	bool messageAllow; //trigger to allow or disallow messages, set by program
	
	//Run parameter declarations
	double threshold; //the sensor threshold (in volts) that indicates an overflow
	double scale_threshold; //scale sensor threshold which triggers a warning that the LN2 tank is close to empty
	int polling_time; //the amount of time (in microseconds) between sensor readings when not filling
	int iterations; //number of measurements allowed above the sensor threshold before stopping LN2 flow
	double maxfilltime; //maximum length of time (in seconds) during which filling can take place before automatic shut-off of valves
	int circBufferSize; //size of the circular buffers (# of data points)
	char* filename; //name of file to save data to
	char* masterParam; //additional parameter that can be given to master
	bool autosave; //if true, data will be automatically saved and uploaded once the data buffers are full
	bool autosaveSwitch; //used to enforce only one autosave after each fill
	bool email; //if true, alerts (tank nearly empty, automatic shutdown) will be sent by e-mail
	char networkloc [200]; //network location to upload data to
	char mailaddress [200]; //e-mail address to send alerts to
	
	//cooling system component value declarations
	int numValves; //number of valves/overflow sensors used in the setup
	int valveOutputs [20]; //array of output DAQ channels for each valve
	int scaleInput; //input DAQ channel for the scale reading
	
	//Sensor calibration parameter declarations
	double sensorBoxSupplyV; //sensor box supply voltage
	double boxResistors [20]; //array of resistance values for resistors in sensor box
	double tempFit [3]; //array of fit parameters for PT100 temperature
	double scaleFit [2]; //array of fit parameters for scale reading
   
};
