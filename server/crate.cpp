#include "crate.h"
#include "circbuffer.h"
#include "influxdb.h"

const int commandSize = 4096;
char command[commandSize];
int autosaveCounter = 0; //counter for number of autosaves completed since run start

/* declare circular buffer objects used to log run time and readings from the two sensors */
CircularBuffer rtbuffer;         //buffer for storing local time strings
CircularBuffer tbuffer;          //buffer for storing time since run start
CircularBuffer weightbuffer;     //buffer for storing voltage readings from scale (to track LN2 tank weight)
CircularBuffer sensorBuffer[20]; //buffers for storing voltage readings from LN2 sensors
CircularBuffer tempBuffer[20];   //buffers for storing temperature readings from detector readouts

/* Declare elements corresponding to each buffer */
ElemType rtelement;
ElemType telement;
ElemType weightelement;
ElemType sensorValue[20];
ElemType tempValue[20];

influx_client_t c;

Crate::Crate() {
  signaled.BEGIN = false;
  signaled.END = false;
  signaled.EXIT = false;
  signaled.RUNNING = false;
  signaled.SAVE = false;
  signaled.NETSAVE = false;
  signaled.PLOT = false;
  signaled.FILL = false;
  signaled.FILLING = false;
  signaled.MEASURE = false;
  signaled.LIST = false;

  c.host = strdup("127.0.0.1");
  c.port = 8086;
  c.db = strdup("LN2");
  c.usr = strdup("");
  c.pwd = strdup("");
}
/*--------------------------------------------------------------*/
Crate::~Crate() {
  ;
}

/*--------------------------------------------------------------*/
int Crate::Boot(FillSched *s) {
  printf("Setting up the acquisition...\n");
  //first thing we do: try to make a lock file, abort the program
  //if one exists already
  l = new lock("LN2");

  readParameters();  //get parameters for run from file
  readConnections(); //get system component data from file
  readCalibration(); //get sensor calibration data from file
	readSchedule(s); //read in the filling schedule

  /* Initialize (or re-initialize) all data saving buffers prior to run */
  cbInit(&rtbuffer, circBufferSize);
  cbInit(&tbuffer, circBufferSize);
  cbInit(&weightbuffer, circBufferSize);
  for (int i = 0; i < numValves; i++) {
    cbInit(&sensorBuffer[i], circBufferSize);
  }
  for (int i = 0; i < numTempSensors; i++) {
    cbInit(&tempBuffer[i], circBufferSize);
  }

  emailAllow = true;
  messageAllow = true;

  //initialize timing vars and counters
  tfillelapsed = 0;
  prev_run_time = 0;
  for (int i = 0; i < 20; i++)
    cycleCounter[i] = detectorFillCounter[i] - 1; //reset counter of fill cycles

  //last thing we do: we enable the msg queue
  msg = new MsgQ();
  printf("Acquisition ready!\nType './master list' for a list of availiable commands.\n");

  return 1;
}
/***********************************************************************************/
/*The main loop, in which data is acquired and saved.*/
int Crate::MainLoop(FillSched *s) {
  int j = waiting_mult + 1;
  int day, hour, minute;
  struct tm *goodtime;
  time_t now;

  goodtime = (tm *)malloc(sizeof(tm));

  while (true) {

    //check the msg queue
    if (msg->read(command) == 1) {
      ReadCommand(&signaled, command);
      ProcessSignal();
    }
    //if the acquisition is on
    if (signaled.RUNNING) {
      current_run_time = GetTime();

      if (j >= waiting_mult) {
        j = 0;
        recordMeasurement();
        //	printf("j >= waiting_mult \n");

        if (atof(weightelement.value) < scale_threshold) {
          if (messageAllow) {
            printf("\nWARNING: scale reading is low.  LN2 dewar may need refilling.\n\n");
            messageAllow = false;
          }
          if ((email == true) && (emailAllow == true)) {
            emailAllow = false; //prevent spamming the poor user's inbox

            /*Convert the email message into a C string that can be read as a terminal command*/
            stringstream tmpcommand;
            tmpcommand << "sh emailalert.sh "
                       << "\"" << mailaddress << "\" "
                       << "The LN2 tank scale reading is low.  The tank should be refilled soon.";
            const std::string tmp = tmpcommand.str();
            const char *command = tmp.c_str();
            /*send email using external bash script*/
            system(command);
          }
        } else {
          emailAllow = true; //allow alert e-mails to be sent to the user again if scale reading is back to normal
          messageAllow = true;
        }
      }
      j += 1;

      //	      printf("Value of j = %i \n", j);

      if (((current_run_time + tfillelapsed) > (interval * 0.15)) && autosaveSwitch == true && autosave == true) {
        printf("autosaveData switch to false \n");
        autosaveData();
        autosaveSwitch = false; //doesn't allow program to autosave again until after next fill
      }

      if (signaled.FILL == true) {
        printf("Fill initiated outside of the scheduled cycle\n");
        fillCycle();
      }

      time(&now);
      goodtime = localtime(&now);
      // printf("goodtime received \n");
      day = goodtime->tm_wday;
      //printf("day received %d\n", day);
      hour = goodtime->tm_hour;
      //printf("hour received %d\n", hour);
      minute = goodtime->tm_min;
      //printf("minute received %d\n", minute);

      if ((day == fill_day1) || (day == fill_day2)) {
        // printf("day condition fulfilled \n");
        if (hour == fill_hour) {
          //  printf("hour condition fulfilled \n");
          if (minute == fill_min) {
            //  printf("minute fulfilled \n");
            fillCycle(); //start a fill cycle
          }
        }
      }

      //wait for some interval
      usleep(polling_time);
    }
  }
  return 0;
}

/*--------------------------------------------------------------*/

void Crate::ProcessSignal() {
  if (signaled.BEGIN) {
    signaled.BEGIN = false;
    if (signaled.RUNNING == false) {
      printf("Dewar(s) will be filled after %10.0f seconds\n\n", interval);
      BeginRun();
    } else
      printf("Run started already, command ignored\n");
  }
  if (signaled.TIME) {
    signaled.TIME = false;
    run_time = GetTime();
    printf(" Time since the last filling is %f s.\n", run_time);
  }
  if (signaled.END) {
    signaled.END = false;
    signaled.FILLING = false;
    if (signaled.RUNNING == true)
      EndRun();
    else
      printf("Run ended already, command ignored\n");
  }
  if (signaled.ON) {
    signaled.ON = false;
    printf(".");
    ChanOn(atoi(masterParam)); //turn specified DAQ channel on
    printf(".");
  }
  if (signaled.OFF) {
    signaled.OFF = false;
    ChanOff(); //turn DAQ channels off
  }
  if (signaled.MEASURE) {
    signaled.MEASURE = false;
    meas = Measure(atoi(masterParam)); //measure voltage
    printf("Average voltage value is %10.5f\n", meas);
  }
  if (signaled.PLOT) {
    signaled.PLOT = false;
    getPlot();
  }
  if (signaled.SAVE) {
    signaled.SAVE = false;
    Save(filename);
  }
  if (signaled.NETSAVE) {
    signaled.NETSAVE = false;
    NetSave(filename);
  }
  if (signaled.LIST) {
    signaled.LIST = false;
    printf("begin -- Begins the run.  The dewar filling process will occur at set intervals, defined in parameters.dat.\n");
    printf("start -- Same as above.\n");
    printf("end -- Ends the run.  If currently filling, ends the filling process.\n");
    printf("stop -- Same as above.\n");
    printf("fill -- Starts the dewar filling process immediately, then afterwards in set intervals as defined in parameters.dat.  Fills only if a run is currently in progress.\n");
    printf("time -- Shows the time elapsed since the last filling operation.\n");
    printf("on X -- Turns on the DAQ switch P0.X, where X is an integer from 0 to 7.\n");
    printf("off -- Turns off all DAQ switches, closing all valves.\n");
    printf("measure X-- Shows the voltage reading on DAQ channel aiX, where X is an integer from 0 to 7.\n");
    printf("table -- Shows recent sensor data in a table format.\n");
    printf("save filename -- Saves recent sensor data to a text file with name specifed by filename.\n");
    printf("netsave filename -- Saves recent sensor data to a text file with name specifed by filename, and uploads a graphical figure via scp to the location defined in the shell script plot.sh.\n");
    printf("exit -- Ends the run and exits the control program.\n");
    printf("quit -- Same as above.\n\n");
    printf("Run parameters can be modified by editing the text file parameters.dat in the same folder as the main program.  Parameters may be edited while the program is running, in which case they will be applied on the subsequent run.\n");
  }
  if (signaled.EXIT) {
    if (signaled.FILLING == true) {
      printf("\nFilling stopped partway.  Turning off DAQ switch ... \n\n");
      ChanOff(); //make sure DAQ switch is off
    }
    if (signaled.RUNNING)
      EndRun();
    l->unlock();
    delete l;
    exit(EXIT_SUCCESS);
  }
}
/*--------------------------------------------------------------*/
void Crate::ReadCommand(struct Signals *signal, char *command) {
  if (signal == NULL || command == NULL) {
    return;
  }
  if (((strstr(command, "end")) != NULL) || ((strstr(command, "stop")) != NULL)) {
    printf("\n Received end command ... \n\n");
    signal->END = true;
  } else if (((strstr(command, "begin")) != NULL) || ((strstr(command, "start")) != NULL)) {
    printf("\n Starting run ...\n\n");
    signal->BEGIN = true;
  } else if ((strstr(command, "time")) != NULL) {
    signal->TIME = true;
  } else if ((strstr(command, "netsave")) != NULL) {
    filename = strtok(command, " ");
    filename = strtok(NULL, " ");
    printf("\n Saving data with filename %s to network ...\n\n", filename);
    signal->NETSAVE = true;
  } else if ((strstr(command, "save")) != NULL) {
    filename = strtok(command, " ");
    filename = strtok(NULL, " ");
    printf("\n Saving data with filename %s ...\n\n", filename);
    signal->SAVE = true;
  } else if (((strstr(command, "exit")) != NULL) || ((strstr(command, "quit")) != NULL)) {
    printf("\n Received exit command ... \n\n");
    signal->EXIT = true;
  } else if ((strstr(command, "on")) != NULL) {
    masterParam = strtok(command, " ");
    masterParam = strtok(NULL, " ");
    if (masterParam != NULL && atoi(masterParam) >= 0 && atoi(masterParam) < 8) {
      printf("\n Turning on DAQ switch P0.%i... \n\n", atoi(masterParam));
      signal->ON = true;
    } else {
      printf("\n Invalid valve specified.  Type 'on X', where 'X' is an integer from 0 to 7. \n\n");
    }
  } else if ((strstr(command, "off")) != NULL) {
    printf("\n Turning off DAQ switch ... \n\n");
    signal->OFF = true;
  } else if ((strstr(command, "measure")) != NULL) {
    masterParam = strtok(command, " ");
    masterParam = strtok(NULL, " ");
    if (masterParam != NULL && atoi(masterParam) >= 0 && atoi(masterParam) < 8) {
      printf("\n Measuring voltage on DAQ channel ai%i... \n\n", atoi(masterParam));
      signal->MEASURE = true;
    } else {
      printf("\n Invalid DAQ channel specified.  Type 'measure X', where 'X' is an integer from 0 to 7. \n\n");
    }
  } else if ((strstr(command, "fill")) != NULL) {
    printf("\n Received fill command ... \n\n");
    signal->FILL = true;
    signal->RUNNING = true;
  } else if ((strstr(command, "table")) != NULL) {
    printf("\n Showing table of recent data ... \n\n");
    signal->PLOT = true;
  } else if (((strstr(command, "list")) != NULL) || ((strstr(command, "help")) != NULL)) {
    printf("\n Showing list of available commands ... \n\n");
    signal->LIST = true;
  } else {
    printf("\n Command not understood\n\n");
    ;
  }
}
/*--------------------------------------------------------------*/
int Crate::BeginRun(void) {
  signaled.RUNNING = true;

  ftime(&tstart);
  printf("Run start at %s\n", ctime(&tstart.time));
  return 1;
}
/*--------------------------------------------------------------*/
int Crate::EndRun(void) {
  ftime(&tstop);
  printf("Run end at %s\n", ctime(&tstop.time));
  current_run_time = GetTime();
  printf("Ending acquisition\n");
  printf("Run time %15.3f [s]\n", current_run_time + prev_run_time);
  signaled.RUNNING = false;
  if (autosave == true) {
    autosaveData(); //save data from end of run
  }

  //reset timing vars and counters for future runs
  prev_run_time = 0;
  tfillelapsed = 0;
  for (int i = 0; i < 20; i++)
    cycleCounter[i] = detectorFillCounter[i] - 1; //reset counter of fill cycles

  return 1;
}
/*--------------------------------------------------------------*/
double
Crate::GetTime(void) {
  double telapsed;
  ftime(&tcurrent);
  telapsed = tcurrent.millitm - tstart.millitm;
  telapsed = telapsed / 1000 + tcurrent.time - tstart.time;
  return telapsed;
}
/*Function which records current sensor values in circular buffers*/
int Crate::recordMeasurement(void) {
  double weightV, weight, sensor;
  time_t current_time;
  long long ts;

  time(&current_time);
  ts = current_time;
  ts *= 1000000000;
  //	printf("current time %ld time stame %lld\n",current_time,ts);
  weightV = Measure(scaleInput);
  sensor = Measure(detectorInputs[0]);
  weight = findWeight(weightV);

  //save real time to buffer
  strftime(rtelement.value, 80, "%d-%m-%Y,%H:%M:%S", localtime(&tcurrent.time));
  cbWrite(&rtbuffer, &rtelement);
  //save run time to buffer
  sprintf(telement.value, "%f", current_run_time + prev_run_time);
  cbWrite(&tbuffer, &telement);
  //save scale sensor measurement to buffer
  sprintf(weightelement.value, "%f", weight);
  cbWrite(&weightbuffer, &weightelement);
  post_http(&c,
            INFLUX_MEAS("scale"),
            INFLUX_F_FLT("scale", weightV, 6),
            INFLUX_TS(ts),
            INFLUX_END);

  post_http(&c,
            INFLUX_MEAS("weight"),
            INFLUX_F_FLT("weight", weight, 6),
            INFLUX_TS(ts),
            INFLUX_END);

  post_http(&c,
            INFLUX_MEAS("sensor"),
            INFLUX_F_FLT("sensor", sensor, 6),
            INFLUX_TS(ts),
            INFLUX_END);

  for (int i = 0; i < numValves; i++) {
    //save overflow sensor measurement to buffer
    sprintf(sensorValue[i].value, "%f", Measure(detectorInputs[i]));
    cbWrite(&sensorBuffer[i], &sensorValue[i]);
  }

  for (int i = 0; i < numTempSensors; i++) {
    //save temperature measurement to buffer
    sprintf(tempValue[i].value, "%f", findTemp(Measure(tempSensorInputs[i]), tempSensorBoxPorts[i]));
    cbWrite(&tempBuffer[i], &tempValue[i]);
  }

  return 1;
}
/*Function which prints a table of sensor values to the command line*/
int Crate::getPlot(void) {

  printf("Real Time		Run Time (s)	Scale Sensor (V)	");
  for (int i = 0; i < numValves; i++) {
    printf("Sensor %i (V)	", i + 1);
  }
  for (int i = 0; i < numTempSensors; i++) {
    printf("Temp. Sensor %i (K)	", i + 1);
  }
  printf("\n"); //Line break at end

  /* Print all elements in the buffers */
  for (int i = 0; i < cbCount(&tbuffer); i++) {

    cbRead(&rtbuffer, &rtelement); //read values from buffers
    cbRead(&tbuffer, &telement);
    cbRead(&weightbuffer, &weightelement);
    printf("%s	%s	%s	", rtelement.value, telement.value, weightelement.value); //print readings
    cbWrite(&rtbuffer, &rtelement);                                                           //put values that were just read back in buffers so that they can be read again
    cbWrite(&tbuffer, &telement);
    cbWrite(&weightbuffer, &weightelement);
    for (int i = 0; i < numValves; i++) {
      cbRead(&sensorBuffer[i], &sensorValue[i]);
      printf("%s	", sensorValue[i].value); //print all voltage sensor values
      cbWrite(&sensorBuffer[i], &sensorValue[i]);
    }
    for (int i = 0; i < numTempSensors; i++) {
      cbRead(&tempBuffer[i], &tempValue[i]);
      printf("%s	", tempValue[i].value); //print all temp sensor values
      cbWrite(&tempBuffer[i], &tempValue[i]);
    }
    printf("\n"); //Line break at end
  }
  return 1;
}
/*Function which prints a table of sensor values to a text file*/
int Crate::Save(char *filename) {
  FILE *fp;
  fp = fopen(filename, "w");
  printf("Saving data...\n");

  fprintf(fp, "Real Time		Run Time (s)	Scale Sensor (V)	");
  for (int i = 0; i < numValves; i++) {
    fprintf(fp, "Sensor %i (V)	", i + 1);
  }
  for (int i = 0; i < numTempSensors; i++) {
    fprintf(fp, "Temp. Sensor %i (K)	", i + 1);
  }
  fprintf(fp, "\n"); //Line break at end

  /* Print all elements in the buffers */
  for (int i = 0; i < cbCount(&tbuffer); i++) {

    cbRead(&rtbuffer, &rtelement); //read values from buffers
    cbRead(&tbuffer, &telement);
    cbRead(&weightbuffer, &weightelement);
    fprintf(fp, "%s	%s	%s	", rtelement.value, telement.value, weightelement.value); //print readings
    cbWrite(&rtbuffer, &rtelement);                                                           //put values that were just read back in buffers so that they can be read again
    cbWrite(&tbuffer, &telement);
    cbWrite(&weightbuffer, &weightelement);
    for (int i = 0; i < numValves; i++) {
      cbRead(&sensorBuffer[i], &sensorValue[i]);
      fprintf(fp, "%s	", sensorValue[i].value); //print all voltage sensor values
      cbWrite(&sensorBuffer[i], &sensorValue[i]);
    }
    for (int i = 0; i < numTempSensors; i++) {
      cbRead(&tempBuffer[i], &tempValue[i]);
      fprintf(fp, "%s	", tempValue[i].value); //print all temp sensor values
      cbWrite(&tempBuffer[i], &tempValue[i]);
    }
    fprintf(fp, "\n"); //Line break at end
  }
  fclose(fp);
  printf("Data saved locally file %s.\n", filename);
  return 1;
}
/*--------------------------------------------------------------*/
int Crate::NetSave(char *filename) {
  /*First save data to local file*/
  Save(filename);

  /*Convert the given filename into a C string that can be read as a terminal command*/
  stringstream tmpcommand;
  tmpcommand << "sh plot.sh " << filename << " " << networkloc << " " << numValves << " " << numTempSensors;
  const std::string tmp = tmpcommand.str();
  const char *command = tmp.c_str();

  /*Plot (in gnuplot) and upload file using external bash script*/
  system(command);

  return 1;
}
/*------------------------------------------------------------*/
/*Functions controlling the DAQ------------------------------*/
/*----------------------------------------------------------*/
int Crate::ChanOn(int chan) {

  /*int32 error = 0;
  TaskHandle taskHandle = 0;
  uInt32 data[1] = {0}; //all channels off by default

  data[0] = 1 << chan; //turn on specfied channel, leave others off

  char errBuff[2048] = {'\0'};

  // DAQmx Configure Code
  DAQmxErrChk(DAQmxBaseCreateTask("", &taskHandle));
  DAQmxErrChk(DAQmxBaseCreateDOChan(taskHandle, "Dev1/port0", "", DAQmx_Val_ChanForAllLines));

  // DAQmx Start Code
  DAQmxErrChk(DAQmxBaseStartTask(taskHandle));

  // DAQmx Write Code
  DAQmxErrChk(DAQmxBaseWriteDigitalU32(taskHandle, 1, 1, 10.0, DAQmx_Val_GroupByChannel, data, NULL, NULL));

Error:
  if (DAQmxFailed(error))
    DAQmxBaseGetExtendedErrorInfo(errBuff, 2048);
  if (taskHandle != 0) {
    // DAQmx Stop Code
    DAQmxBaseStopTask(taskHandle);
    DAQmxBaseClearTask(taskHandle);
  }
  if (DAQmxFailed(error))
    printf("DAQmx Error: %s\n", errBuff);
  printf("Switch turned on.\n\n");
  //getchar();*/
  return 1;
}
/*------------------------------------------------------------*/
/*Functions controlling the DAQ------------------------------*/
/*----------------------------------------------------------*/
int Crate::GEARBOXChanOn(void) {

  /*int32 error = 0;
  TaskHandle taskHandle = 0;
  uInt32 data[1] = {0}; //all channels off by default
  int chan;

  chan = 0;
  data[0] = 1 << chan; //turn on channel 0, leave others off
  chan = 1;
  data[0] |= 1 << chan; //turn on channel 1, leave others off
  chan = 3;
  data[0] |= 1 << chan; //turn on channel 3, leave others off
  chan = 5;
  data[0] |= 1 << chan; //turn on channel 5, leave others off

  char errBuff[2048] = {'\0'};

  // DAQmx Configure Code
  DAQmxErrChk(DAQmxBaseCreateTask("", &taskHandle));
  DAQmxErrChk(DAQmxBaseCreateDOChan(taskHandle, "Dev1/port0", "", DAQmx_Val_ChanForAllLines));

  // DAQmx Start Code
  DAQmxErrChk(DAQmxBaseStartTask(taskHandle));

  // DAQmx Write Code
  DAQmxErrChk(DAQmxBaseWriteDigitalU32(taskHandle, 1, 1, 10.0, DAQmx_Val_GroupByChannel, data, NULL, NULL));

Error:
  if (DAQmxFailed(error))
    DAQmxBaseGetExtendedErrorInfo(errBuff, 2048);
  if (taskHandle != 0) {
    // DAQmx Stop Code
    DAQmxBaseStopTask(taskHandle);
    DAQmxBaseClearTask(taskHandle);
  }
  if (DAQmxFailed(error))
    printf("DAQmx Error: %s\n", errBuff);
  printf("Switch turned on.\n\n");
  //getchar();*/
  return 1;
}
/*--------------------------------------------------------------*/
int Crate::ChanOff(void) {
  /*int32 error = 0;
  TaskHandle taskHandle = 0;
  uInt8 data[8] = {0, 0, 0, 0, 0, 0, 0, 0};
  char errBuff[2048] = {'\0'};

  // DAQmx Configure Code
  DAQmxErrChk(DAQmxBaseCreateTask("", &taskHandle));
  DAQmxErrChk(DAQmxBaseCreateDOChan(taskHandle, "Dev1/port0/line0:7", "", DAQmx_Val_ChanForAllLines));

  // DAQmx Start Code
  DAQmxErrChk(DAQmxBaseStartTask(taskHandle));

  // DAQmx Write Code
  DAQmxErrChk(DAQmxBaseWriteDigitalU8(taskHandle, 1, 1, 10.0, DAQmx_Val_GroupByChannel, data, NULL, NULL));

Error:
  if (DAQmxFailed(error))
    DAQmxBaseGetExtendedErrorInfo(errBuff, 2048);
  if (taskHandle != 0) {
    // DAQmx Stop Code
    DAQmxBaseStopTask(taskHandle);
    DAQmxBaseClearTask(taskHandle);
  }
  if (DAQmxFailed(error))
    printf("DAQmx Error: %s\n", errBuff);
  printf("Switch turned off.\n\n");
  //getchar();*/
  return 0;
}
/*--------------------------------------------------------------*/
float Crate::Measure(int channel) {
  /*Generate the DAQ channel (eg. Dev1/ai1) that will be measured*/
  /*char *mchannel = "Dev1/ai";
  stringstream ss;
  ss << mchannel << channel;
  const std::string tmp = ss.str();
  mchannel = (char *)tmp.c_str();

  int32 error = 0;
  TaskHandle taskHandle = 0;
  int32 read;
  int32 numMeasurements = 10; //number of measurements to average over
  float64 data[numMeasurements];
  float64 avg = 0; //will contain average voltage

  char errBuff[2048] = {'\0'};
  int ind;
  // DAQmx Configure Code
  DAQmxErrChk(DAQmxBaseCreateTask("", &taskHandle));

  //compensate for different default configuration of the two different channel banks on the DAQ (specific to the NI USB DAQ being used)
  if (channel < 4)
    DAQmxErrChk(DAQmxBaseCreateAIVoltageChan(taskHandle, mchannel, "", DAQmx_Val_RSE, -10.0, 10.0, DAQmx_Val_Volts, NULL));
  else
    DAQmxErrChk(DAQmxBaseCreateAIVoltageChan(taskHandle, mchannel, "", DAQmx_Val_Cfg_Default, -10.0, 10.0, DAQmx_Val_Volts, NULL));

  DAQmxErrChk(DAQmxBaseCfgSampClkTiming(taskHandle, "", 10000.0, DAQmx_Val_Rising, DAQmx_Val_FiniteSamps, numMeasurements));

  // DAQmx Start Code
  DAQmxErrChk(DAQmxBaseStartTask(taskHandle));

  // DAQmx Read Code
  DAQmxErrChk(DAQmxBaseReadAnalogF64(taskHandle, numMeasurements, 10.0, DAQmx_Val_GroupByChannel, data, numMeasurements, &read, NULL));

  for (ind = 0; ind < numMeasurements; ind++) {
    //printf("%4d %10.3f\n",ind,data[ind]);
    avg = avg + data[ind];
  }
Error:
  if (DAQmxFailed(error))
    DAQmxBaseGetExtendedErrorInfo(errBuff, 2048);
  if (taskHandle != 0) {
    // DAQmx Stop Code
    DAQmxBaseStopTask(taskHandle);
    DAQmxBaseClearTask(taskHandle);
  }
  if (DAQmxFailed(error))
    printf("DAQmxBase Error: %s\n", errBuff);

  avg = avg / numMeasurements;
  return avg;*/
	return 0;
}

/*------------------------------------------------------------*/
/*Function containing fill cycle instructions----------------*/
/*----------------------------------------------------------*/
int Crate::fillCycle(void) {

  autosaveSwitch = true; //allows program to autosave again after fill

  //print a different message depending on whether the user started fill process manually
  if (signaled.FILL == true)
    printf("\nManual fill requested.  Turning on DAQ switch ... \n\n");
  else
    printf("\nScheduled fill time reached.  Turning on DAQ switch for first valve... \n\n");
  signaled.FILL = false;

  /*Start by filling the first dewar*/
  ftime(&tfillstart); //reset fill timer
  tfillelapsed = 0;
  fillGEARBOX();

  //take action depending on whether filling was finished normally or stopped by user
  if (signaled.FILLING == true) {
    signaled.FILLING = false;
    signaled.BEGIN = true; //restart the filling cycle
    printf("\nSensor threshold reached.  Turning off DAQ switch and restarting cycle ... \n\n");

    if (email == true) {
      /*Convert the email message into a C string that can be read as a terminal command*/
      stringstream tmpcommand;
      tmpcommand << "sh emailalert.sh "
                 << "\"" << mailaddress << "\" "
                 << "LN2 system filling operation was successfully completed.  Fill time was " << tfillelapsed << " seconds.";
      const std::string tmp = tmpcommand.str();
      const char *command = tmp.c_str();
      /*send email using external bash script*/
      system(command);
    }

  } else {
    printf("\nFilling stopped partway.  Turning off DAQ switch ... \n\n");
  }

  ChanOff();
  signaled.RUNNING = false;
  prev_run_time += current_run_time;
  ProcessSignal();

  return 1;
}

/*-----------------------------------------------*/
/*Fill GEARBOX during a fill cycle*/
/*---------------------------------------------*/
int Crate::fillGEARBOX(void) {
  // KS modified Fri. Dec. 21, 2018, for the GEARBOX fill only
  // valve numbering and wiring should be:
  // valve 0: valve on the big dewar
  // valve 1: valve to the GEARBOX+stand in the T on the wall (the other valve goes to the 8pi)
  // valve 2: valve to the 8pi in the T on the wall (the other valves goes to GEARBOX+stand)
  // valve 3: valve to the GEARBOX in the T behind the GEARBOX led shield (the other goes to the stand)
  // valve 4: valve to the stand in the T behind the GEARBOX led shield (the other goes to the GEARBOX)
  // valve 5: valve blocking the outlet from the GEARBOX
  // GEARBOX sensor is on channel 0, input from the parameter file is ignored
  // scale sensor is on channel 7
  int valve;
  valve = 3;
  signaled.FILLING = true;

  cycleCounter[valve] += 1;

  if (cycleCounter[valve] == detectorFillCounter[valve]) {
    cycleCounter[valve] = 0;

    usleep(5000000); //wait a few seconds before turning on the valve so that switching between valves isn't instantaneous

    GEARBOXChanOn(); //turn on the fill on GEARBOX valves
    int inum = 0;

    //check voltage while filling, and allow viewer to stop filling with the end command
    //filling automatically stops if sfilling time is greater than maxfilltime
    while (((inum < iterations) && signaled.FILLING == true) && (tfillelapsed < maxfilltime)) {
      usleep(polling_time); //wait a bit
      current_run_time = GetTime();
      printf("current run time %f \n", current_run_time);
      reading = Measure(detectorInputs[0]); //measure voltage on overflow sensor
      //printf(" %i %i %i \n", valve, valveOutputs[valve], detectorInputs[valve]);
      printf("Sensor reading is %10.3f V\n", reading);
      if (reading > threshold)
        inum++;

      if (msg->read(command) == 1) //keep this so that user can still issue commands
      {
        ReadCommand(&signaled, command);
        ProcessSignal();
      }

      //figure out how much time has elapsed since filling started
      ftime(&tcurrent);
      tfillelapsed = tcurrent.millitm - tfillstart.millitm;
      tfillelapsed = tfillelapsed / 1000 + tcurrent.time - tfillstart.time;
      if (tfillelapsed > maxfilltime) {
        printf("\nSensor voltage threshold is not being reached.  Threshold may be set poorly, or perhaps LN2 tank is empty.\nAborting run ...\n");

        if (email == true) {
          /*Converting the email message into a C string that can be read as a terminal command*/
          stringstream tmpcommand;
          tmpcommand << "sh emailalert.sh "
                     << "\"" << mailaddress << "\" "
                     << "The LN2 system was shut off automatically since the sensor did not indicate filling was done after " << maxfilltime << " seconds.";
          const std::string tmp = tmpcommand.str();
          const char *command = tmp.c_str();
          /*send email using external bash script*/
          system(command);
        }
        signaled.FILLING = false;
      }

      recordMeasurement();
    }

    ChanOff(); //turn off the valve

  } else {
    printf("Fill cycle was skipped on valve %i.\n", valve);
  }

  return 1;
}
/*--------------------------------------------------------------*/
int Crate::autosaveData(void) {
  printf("\n Autosaving data ...\n\n");

  char s[80];
  time_t t = time(0);
  strftime(s, 80, "%d%m%Y", localtime(&t));
  autosaveCounter += 1;
  /*Generate a filename to autosave with*/
  stringstream tmpascommand;
  tmpascommand << s << "autosave" << autosaveCounter;
  std::string tmp = tmpascommand.str();
  const char *autosavename = tmp.c_str();
  NetSave(const_cast<char *>(autosavename));

  return 1;
}
/*--------------------------------------------------------------*/
int Crate::readParameters(void) {
  /* Read parameters from text file parameters.dat*/
  FILE *parfile = fopen("parameters.dat", "r");
  //char tmp [200];
  fgets(tmp, 200, parfile); //advance a line
  fgets(tmp, 200, parfile); //advance a line
  fgets(tmp, 200, parfile); //advance a line
  fgets(tmp, 200, parfile); //advance a line
  fgets(tmp, 200, parfile); //advance a line
  fgets(tmp, 200, parfile); //advance a line
  fscanf(parfile, "%s", tmp);
  fill_day1 = atof(tmp);
  printf("First fill day of week = %i \n", fill_day1);
  fgets(tmp, 200, parfile); //end the line
  fgets(tmp, 200, parfile); //advance a line
  fscanf(parfile, "%s", tmp);
  fill_day2 = atof(tmp);
  printf("Second fill day of week (0-6) = %i \n", fill_day2);
  fgets(tmp, 200, parfile); //end the line
  fgets(tmp, 200, parfile); //advance a line
  fscanf(parfile, "%s", tmp);
  fill_hour = atof(tmp);
  printf("Hour of the day to conduct fill sequence = %i \n", fill_hour);
  fgets(tmp, 200, parfile); //end the line
  fgets(tmp, 200, parfile); //advance a line
  fscanf(parfile, "%s", tmp);
  fill_min = atof(tmp);
  printf("Minute of the hour of the day to conduct fill sequence = %i \n", fill_min);
  fgets(tmp, 200, parfile); //end the line
  fgets(tmp, 200, parfile); //advance a line
  fscanf(parfile, "%s", tmp);
  threshold = atof(tmp);
  printf("Sensor threshold to indicate LN2 overflow (V) = %f \n", threshold);
  fgets(tmp, 200, parfile); //end the line
  fgets(tmp, 200, parfile); //advance a line
  fscanf(parfile, "%s", tmp);
  scale_threshold = atof(tmp);
  printf("Weight at which tank needs refilled (kg)= %f \n", scale_threshold);
  fgets(tmp, 200, parfile); //end the line
  fgets(tmp, 200, parfile); //advance a line
  fscanf(parfile, "%s", tmp);
  polling_time = atoi(tmp) * 1000; //convert from milliseconds into microseconds
  printf("Time between readings while filling (micros) = %i \n", polling_time);
  fgets(tmp, 200, parfile); //end the line
  fgets(tmp, 200, parfile); //advance a line
  fscanf(parfile, "%s", tmp);
  waiting_mult = atoi(tmp);
  printf("Integer number of measurements taken at the above time interval specified = %i \n", waiting_mult);
  fgets(tmp, 200, parfile); //end the line
  fgets(tmp, 200, parfile); //advance a line
  fscanf(parfile, "%s", tmp);
  iterations = atoi(tmp);
  printf("Number of measurements allowed above sensor threshold = %i \n", iterations);
  fgets(tmp, 200, parfile); //end the line
  fgets(tmp, 200, parfile); //advance a line
  fscanf(parfile, "%s", tmp);
  maxfilltime = atof(tmp);
  printf("Maximum length of time filling can take place (s) = %f \n", maxfilltime);
  fgets(tmp, 200, parfile); //end the line
  fgets(tmp, 200, parfile); //advance a line
  fscanf(parfile, "%s", tmp);
  circBufferSize = atoi(tmp);
  printf("Number of saved data points = %i \n", circBufferSize);
  autosaveSwitch = true;    //make sure to set this value for proper autosaving
  fgets(tmp, 200, parfile); //end the line
  fgets(tmp, 200, parfile); //advance a line
  fscanf(parfile, "%s", tmp);
  autosave = atoi(tmp);
  fgets(tmp, 200, parfile); //end the line
  fgets(tmp, 200, parfile); //advance a line
  fscanf(parfile, "%s", networkloc);
  fgets(tmp, 200, parfile); //end the line
  fgets(tmp, 200, parfile); //advance a line
  fscanf(parfile, "%s", tmp);
  email = atoi(tmp);
  printf("Email to send alerts to = %i \n", email);
  fgets(tmp, 200, parfile); //end the line
  fgets(tmp, 200, parfile); //advance a line
  fscanf(parfile, "%s", mailaddress);
  printf("Alert email = %s", mailaddress);

  printf("File 'parameters.dat' read sucessfully!\n");

  //fclose(parfile);

  return 1;
}

int Crate::readConnections(void) {
  /* Read sensor connections from text file connections.dat*/
  FILE *parfile = fopen("connections.dat", "r");
  //char tmp [200];
  fgets(tmp, 200, parfile); //advance a line
  fgets(tmp, 200, parfile); //advance a line
  fgets(tmp, 200, parfile); //advance a line
  fgets(tmp, 200, parfile); //advance a line
  fgets(tmp, 200, parfile); //advance a line
  fgets(tmp, 200, parfile); //advance a line
  fscanf(parfile, "%s", tmp);
  numValves = atoi(tmp);
  fgets(tmp, 200, parfile); //end the line
  fgets(tmp, 200, parfile); //advance a line
  fscanf(parfile, "%s", tmp);
  numTempSensors = atoi(tmp);
  fgets(tmp, 200, parfile); //end the line
  fgets(tmp, 200, parfile); //advance a line
  int j = 0;
  for (int i = 0; i < 20; i++) {
    fscanf(parfile, "%s", tmp);
    if (j < numValves) {
      detectorInputs[i] = atoi(tmp);
      j++;
    }
    fgets(tmp, 200, parfile); //end the line
  }
  fgets(tmp, 200, parfile); //advance a line
  j = 0;
  for (int i = 0; i < 20; i++) {
    fscanf(parfile, "%s", tmp);
    if (j < numValves) {
      valveOutputs[i] = atoi(tmp);
      j++;
    }
    fgets(tmp, 200, parfile); //end the line
  }
  fgets(tmp, 200, parfile); //advance a line
  j = 0;
  for (int i = 0; i < 20; i++) {
    fscanf(parfile, "%s", tmp);
    if (j < numTempSensors) {
      tempSensorInputs[i] = atoi(tmp);
      j++;
    }
    fgets(tmp, 200, parfile); //end the line
  }
  fgets(tmp, 200, parfile); //advance a line
  j = 0;
  for (int i = 0; i < 20; i++) {
    fscanf(parfile, "%s", tmp);
    if (j < numTempSensors) {
      tempSensorBoxPorts[i] = atoi(tmp);
      j++;
    }
    fgets(tmp, 200, parfile); //end the line
  }
  fgets(tmp, 200, parfile); //advance a line
  j = 0;
  for (int i = 0; i < 20; i++) {
    fscanf(parfile, "%s", tmp);
    if (j < numValves) {
      detectorFillCounter[i] = atoi(tmp);
      j++;
    }
    fgets(tmp, 200, parfile); //end the line
  }
  fgets(tmp, 200, parfile); //advance a line
  fscanf(parfile, "%s", tmp);
  scaleInput = atoi(tmp);

  printf("File 'connections.dat' read sucessfully!\n");

  return 1;
}

int Crate::readCalibration(void) {
  /* Read sensor connections from text file connections.dat*/
  FILE *parfile = fopen("calibration.dat", "r");
  //char tmp [200];
  fgets(tmp, 200, parfile); //advance a line
  fgets(tmp, 200, parfile); //advance a line
  fgets(tmp, 200, parfile); //advance a line
  fgets(tmp, 200, parfile); //advance a line
  fgets(tmp, 200, parfile); //advance a line
  fgets(tmp, 200, parfile); //advance a line
  fgets(tmp, 200, parfile); //advance a line
  fscanf(parfile, "%s", tmp);
  sensorBoxSupplyV = atof(tmp);
  fgets(tmp, 200, parfile); //end the line
  fgets(tmp, 200, parfile); //advance a line
  for (int i = 0; i < 20; i++) {
    fscanf(parfile, "%s", tmp);
    boxResistors[i] = atof(tmp);
    fgets(tmp, 200, parfile); //end the line
  }
  fgets(tmp, 200, parfile); //advance a line
  fgets(tmp, 200, parfile); //advance a line
  for (int i = 0; i < 3; i++) {
    fscanf(parfile, "%s", tmp);
    tempFit[i] = atof(tmp);
    fgets(tmp, 200, parfile); //end the line
    fgets(tmp, 200, parfile); //advance a line
  }
  fgets(tmp, 200, parfile); //advance a line
  fgets(tmp, 200, parfile); //advance a line
  for (int i = 0; i < 2; i++) {
    fscanf(parfile, "%s", tmp);
    scaleFit[i] = atof(tmp);
    fgets(tmp, 200, parfile); //end the line
    fgets(tmp, 200, parfile); //advance a line
  }
  fclose(parfile);

  printf("File 'calibration.dat' read sucessfully!\n");

  return 1;
}

void Crate::readSchedule(FillSched *s){

	char *tok,*tok2;
  char str[256];//string to be read from file (will be tokenized)
  char fullLine[256], line[256];
	int k;
	int currentEntry=0;
	int currentParameter=0;

	FILE *schedfile = fopen("schedule.txt", "r");

	while(!(feof(schedfile)))//go until the end of file is reached
    {
			if(fgets(str,256,schedfile)!=NULL) //get an entire line
				{
					strcpy(fullLine,str);
					currentParameter=1;
					while(true){
							//printf("%s\n",str); //print the line (debug)
						strcpy(str,fullLine);
						tok=strtok (str,",");
						if(currentParameter == 1){
							tok[strcspn(tok, "\r\n")] = 0;//strips newline characters from the string
							strcpy(s->sched[currentEntry].entryName,tok);
						}

						for(int i=0;i<currentParameter;i++){
							if(i>0){
								tok = strtok (NULL, "]");
								tok = strtok (NULL, ",[");
							}else{
								tok = strtok (NULL, "[");
							}
						}

						
						if(tok!=NULL)
							{
								tok[strcspn(tok, "\r\n")] = 0;//strips newline characters from the string
								strcpy(line,tok);
								if(strcmp(line,"valve")==0){
									tok = strtok (NULL, "]");
									tok[strcspn(tok, "\r\n")] = 0;//strips newline characters from the string
									strcpy(line,tok);
									k=0;
									tok2=strtok (line,",");
									tok2[strcspn(tok2, "\r\n")] = 0;//strips newline characters from the string
									while(tok2!=NULL){
										if(k<MAXNUMVALVES){
											s->sched[currentEntry].valves[k] = atoi(tok2);
											tok2=strtok (NULL,",");
											if(tok2!=NULL){
												tok2[strcspn(tok2, "\r\n")] = 0;//strips newline characters from the string
											}
											k++;
										}else{
											printf("ERROR: Maximum number of valves (%i) exceeded in schedule entry %i.  Revise the number of valves used, or increase MAXNUMVALVES in LN2_server.h and try again.\n",MAXNUMVALVES,currentEntry+1);
											exit(-1);
										}
									}
									s->sched[currentEntry].numValves=k;
								}else if(strcmp(line,"time")==0){
									//tok = strtok (NULL, "]");
									//strcpy(line,tok);
									printf("here!\n");
									
								}else{
									break;
								}
								
							}else{
								break;
							}
						currentParameter++;
					}
					
					
					
				}
			currentEntry++;
			if(currentEntry >= MAXSCHEDENTRIES){
				printf("ERROR: Maximum number of schedule entries (%i) exceeded.  Increase MAXSCHEDENTRIES in LN2_server.h and try again.\n",MAXSCHEDENTRIES);
				exit(-1);
			}
		}

	fclose(schedfile);

	//report on fill schedule info that was read in
	printf("Fill schedule read. %i entries read.\n",currentEntry-1);
	for(int i=0;i<currentEntry-1;i++){
		printf("Entry %i name: %s, Valve sequence: [",i+1,s->sched[i].entryName);
		for (int j=0;j<s->sched[i].numValves;j++){
			printf(" %i",s->sched[i].valves[j]);
		}
		printf(" ]\n");
	}
}



/*Funtion which converts dewar temperature sensor voltage values into temperatures*/
double
Crate::findTemp(double vSensor, int sensorPort) {
  double r1 = boxResistors[sensorPort - 1];                 //measured resistance inside sensor box
  double rSensor = r1 / ((sensorBoxSupplyV / vSensor) - 1); //resistance of the temperature sensor

  /*returns temperature in kelvin, based on 2nd order polynomial fit to resistance/temp curve*/
  double temp = (rSensor * rSensor * tempFit[0] + rSensor * tempFit[1] + tempFit[2]);

  if (temp < 1000) {
    return temp;
  } else {
    return 0; //don't record very high values (eg. from unplugged sensor) to prevent bad plot scaling
  }
}

/*Function which converts scale voltage values into temperatures*/
/*Currently using a very rough calibration*/
double
Crate::findWeight(double vScale) {

  /*returns weight in kg, based on linear fit to calibration curve*/
  return (vScale * scaleFit[0] + scaleFit[1]);
}
