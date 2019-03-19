//#define  MAXCHANNELS 8192
#include <sstream>
#include <sys/timeb.h>
#include <stdio.h>
//#include <NIDAQmxBase.h>
#include "msgtool.h"
#include <cstdlib>
#include <unistd.h>

#define read_ports 2
#define first_port_read 1
#define write_ports 1

#define DAQmxErrChk(functionCall) if( DAQmxFailed(error=(functionCall)) ) goto Error; else

