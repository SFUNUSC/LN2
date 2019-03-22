//code for controlling the NIDAQmx based system used for GEARBOX

#include <string>
#include <cstdio>
#include <cstdlib>
#include <sstream>
#include <NIDAQmxBase.h>
using namespace std;

#define read_ports 2
#define first_port_read 1
#define write_ports 1

#define DAQmxErrChk(functionCall) if( DAQmxFailed(error=(functionCall)) ) goto Error; else