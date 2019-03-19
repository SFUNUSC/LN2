#include <sstream>
#include <sys/timeb.h>
#include <stdio.h>
#include "msgtool.h"
#include <cstdlib>
#include <unistd.h>


//data structures for fill schedule
typedef struct {
    char entryName[256]; //the name of the entry, shown when the entry is run
		int valves[8]; //list of valves to be opened, in the order they re opened in
		int numValves; //number of valves in the list
		int schedMode; //0 to 6=specific day and time (0=sunday,1=monday,...), 7=interval in minutes, 8=interval in hours
		int schedFreqPar; //parameter for scheduling frequency(number of minutes, time of day, etc.)
} SchedEntry;

typedef struct {
    SchedEntry sched[256]; //the individual entries in the fill schedule
		int numEntries; //number of total fill schedule entries
} FillSched;