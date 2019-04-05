#include "LN2_server.h"
#include "circbuffer.h"
#include "influxdb.h"

const int commandSize = 4096;
char command[commandSize];
int autosaveCounter = 0; //counter for number of autosaves completed since run start

// declare circular buffer objects used to log run time and readings from the two sensors
CircularBuffer rtbuffer;         //buffer for storing local time strings
CircularBuffer tbuffer;          //buffer for storing time since run start
CircularBuffer weightbuffer;     //buffer for storing voltage readings from scale (to track LN2 tank weight)
CircularBuffer sensorBuffer[MAXSCHEDENTRIES]; //buffers for storing voltage readings from LN2 sensors
CircularBuffer tempBuffer[MAXSCHEDENTRIES];   //buffers for storing temperature readings from detector readouts

// Declare elements corresponding to each buffer
ElemType rtelement;
ElemType telement;
ElemType weightelement;
ElemType sensorValue[MAXSCHEDENTRIES];
ElemType tempValue[MAXSCHEDENTRIES];

influx_client_t c;


int Boot(FillSched *s) {
  printf("Setting up the acquisition...\n");
  //first thing we do: try to make a lock file, abort the program
  //if one exists already
  l = new lock("LN2");

  readParameters();  //get parameters for run from file
  readCalibration(); //get sensor calibration data from file
	readSchedule(s); //read in the filling schedule

  // Initialize (or re-initialize) all data saving buffers prior to run
  cbInit(&rtbuffer, circBufferSize);
  cbInit(&tbuffer, circBufferSize);
  cbInit(&weightbuffer, circBufferSize);
  for (int i = 0; i < s->numEntries; i++) {
    cbInit(&sensorBuffer[i], circBufferSize);
  }
  for (int i = 0; i < s->numEntries; i++) {
    cbInit(&tempBuffer[i], circBufferSize);
  }

  emailAllow = true;
  messageAllow = true;

  //last thing we do: we enable the msg queue
  msg = new MsgQ();
  printf("Acquisition ready!\nType './LN2_master list' for a list of available commands.\nOr type './LN2_master begin' to start running.\n");

  return 1;
}
/***********************************************************************************/
//The main loop, in which data is acquired and saved.
int MainLoop(FillSched *s) {
	double current_run_min;
  int day, hour, minute;
  struct tm *goodtime;
  time_t now;
  bool foundDetector;
  double autosaveFlagTime = 0.0;
  bool autosaveFlag = false;
  bool autosaveNow = false;

  goodtime = (tm *)malloc(sizeof(tm));

  while (true) {

    //check the msg queue
    if (msg->read(command) == 1) {
      ReadCommand(&signaled, command);
      ProcessSignal(s);
    }
    //if the acquisition is on
    if (signaled.RUNNING) {
			current_run_min = GetTime()/60.0;
			//printf("minute: %f\n",current_run_min);

      //autosave if it has been 20 minutes since a fill
      if(autosaveFlag){
        for (int i=0;i<s->numEntries;i++){
          if((current_run_min - autosaveFlagTime) > 20){
            autosaveNow = true;
            autosaveFlag = false; //unset the flag
          }
          //printf("autosavetime = %f %f %f \n",current_run_min - autosaveFlagTime, current_run_min, autosaveFlagTime);
        }
        if(autosaveNow){
          autosaveData(s);
          autosaveNow = false; //unset the flag
        }
      }

      //filling outside of the normal cycle
      if (signaled.FILL == true) {
        foundDetector=false;
        for(int i=0;i<s->numEntries;i++){
          //check for matching detector name
          if(strcmp(s->sched[i].entryName,fillName)==0){
            foundDetector=true;
            fill(s,i);
            break;
          }
        }
        if(foundDetector==false){
          printf("Could not find detector with name %s in the schedule, no action taken.\n",fillName);
        }
        
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

			//SCHEDULE FILLING
			//check for fill conditions
      for (int i=0;i<s->numEntries;i++){
				//only check entries which have not awaiting fill
				if(s->sched[i].schedFlag==0){
					if(s->sched[i].schedMode == 7){
						//fill in a set interval (given in minutes)
						if((current_run_min - s->sched[i].lastTriggerTime) > s->sched[i].schedMin){
							printf("Scheduling fill for %s ...\n",s->sched[i].entryName);
							s->sched[i].schedFlag=1; //set the fill flag
							s->sched[i].lastTriggerTime = current_run_min;
							s->sched[i].hasBeenTriggered = 1;
						}
					}else{
						//fill on a day of the week
						//check that the day of the week is correct
						if(day==s->sched[i].schedMode){
							//check that the time is correct
							if(hour>=s->sched[i].schedHour){
								if(minute>=s->sched[i].schedMin){
									//check that we haven't already triggered a fill today
									if( ((current_run_min - s->sched[i].lastTriggerTime) > 1500) || (s->sched[i].hasBeenTriggered == 0) ){
										printf("Scheduling fill for %s ...\n",s->sched[i].entryName);
										s->sched[i].schedFlag=1; //set the fill flag
										s->sched[i].lastTriggerTime = current_run_min;
										s->sched[i].hasBeenTriggered = 1;
									}
								}
							}
						}else{
							//wrong day of the week
							s->sched[i].schedFlag=0;
						}
					}
				}
				
			}

      //PERFORM FILLING
      for (int i=0;i<s->numEntries;i++){
        if(s->sched[i].schedFlag){
          fill(s,i); //start the fill cycle

          //schedule entries that are supposed to occur directly after fills
          for(int j=0;j<s->numEntries;j++){
            if(j!=i){ //entries cannot run directly after themselves
              if(s->sched[j].schedMode == 8){
                if(s->sched[j].schedAfterEntry == i){
                  current_run_min = GetTime()/60.0;
                  printf("Scheduling fill for %s ...\n",s->sched[j].entryName);
                  s->sched[j].schedFlag=1; //set the fill flag
                  s->sched[j].lastTriggerTime = current_run_min;
                  s->sched[j].hasBeenTriggered = 1;
                }
              }
            }
          }

          //schedule an autosave
          if(autosaveFlag == false){
            autosaveFlag = true;
            autosaveFlagTime = GetTime()/60.0; //time in minutes
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

void ProcessSignal(FillSched* s) {
  if (signaled.BEGIN) {
    signaled.BEGIN = false;
    if (signaled.RUNNING == false) {
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
      EndRun(s);
    else
      printf("Run ended already, command ignored\n");
  }
  if (signaled.ON) {
    signaled.ON = false;
    printf(".");
    chanOn(atoi(masterParam)); //turn specified DAQ channel on
    printf(".");
  }
  if (signaled.OFF) {
    signaled.OFF = false;
    chanOff(); //turn DAQ channels off
  }
  if (signaled.MEASURE) {
    signaled.MEASURE = false;
    meas = measure(atoi(masterParam)); //measure voltage
    printf("Average voltage value is %10.5f\n", meas);
  }
  if (signaled.PLOT) {
    signaled.PLOT = false;
    getPlot(s);
  }
  if (signaled.SAVE) {
    signaled.SAVE = false;
    Save(s,filename);
  }
  if (signaled.NETSAVE) {
    signaled.NETSAVE = false;
    NetSave(s,filename);
  }
  if (signaled.LIST) {
    signaled.LIST = false;
    printf("begin -- Begins the run.  The dewar filling process will occur at set intervals, defined in parameters.dat.\n");
    printf("start -- Same as above.\n");
    printf("end -- Ends the run.  If currently filling, ends the filling process.\n");
    printf("stop -- Same as above.\n");
    printf("fill detector_name -- Starts the dewar filling process immediately for the detector with name detector_name defined in schedule.dat.\n");
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
      chanOff(); //make sure DAQ switch is off
    }
    if (signaled.RUNNING)
      EndRun(s);
    l->unlock();
    delete l;
    exit(EXIT_SUCCESS);
  }
}
/*--------------------------------------------------------------*/
void ReadCommand(struct Signals *signal, char *command) {
  if (signal == NULL || command == NULL) {
    return;
  }
  if (((strstr(command, "end")) != NULL) || ((strstr(command, "stop")) != NULL)) {
    printf("\n Received end command ... \n\n");
    signal->END = true;
  } else if (((strcmp(command, "begin")) == 0) || ((strcmp(command, "start")) == 0)) {
    printf("\n Starting run ...\n\n");
    signal->BEGIN = true;
  } else if ((strcmp(command, "time")) == 0) {
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
    fillName = strtok(command, " ");
    fillName = strtok(NULL, " ");
    if(fillName != NULL){
      printf("\n Received command to fill %s ...\n\n", fillName);
      signal->FILL = true;
      //start the run if it hasn't already been started
      if(signal->RUNNING == false){
        signal->BEGIN = true;
      }
    }else{
      printf("\n Invalid fill command (syntax: ./LN2_master fill detector_name).\n\n");
    }
  } else if ((strstr(command, "table")) != NULL) {
    printf("\n Showing table of recent data ... \n\n");
    signal->PLOT = true;
  } else if (((strcmp(command, "list")) == 0) || ((strcmp(command, "help")) == 0)) {
    printf("\n Showing list of available commands ... \n\n");
    signal->LIST = true;
  } else {
    printf("\n Command not understood (%s).\n\n",command);
    ;
  }
}
/*--------------------------------------------------------------*/
int BeginRun(void) {
  signaled.RUNNING = true;

  ftime(&tstart);
  printf("Run start at %s\n", ctime(&tstart.time));
  return 1;
}
/*--------------------------------------------------------------*/
int EndRun(FillSched* s) {
  ftime(&tstop);
  printf("Run end at %s\n", ctime(&tstop.time));
  current_run_time = GetTime();
  printf("Ending acquisition\n");
  printf("Run time %15.3f [s]\n", current_run_time);
  signaled.RUNNING = false;
  if (autosave == true) {
    autosaveData(s); //save data from end of run
  }

  return 1;
}
/*--------------------------------------------------------------*/
double GetTime(void) {
  double telapsed;
  ftime(&tcurrent);
  telapsed = tcurrent.millitm - tstart.millitm;
  telapsed = telapsed / 1000 + tcurrent.time - tstart.time;
  return telapsed;
}
// Function which records current sensor values in circular buffers
int recordMeasurement(FillSched *s) {
  double weightV, weight;
  double sensor[MAXSCHEDENTRIES];
  char sensorName[256];
  time_t current_time;
  long long ts;

  time(&current_time);
  ts = current_time;
  ts *= 1000000000;
  //	printf("current time %ld time stame %lld\n",current_time,ts);
  weightV = measure(scaleInput);
  for(int i=0;i<s->numEntries;i++){
    if(i<MAXSCHEDENTRIES){
      sensor[i] = measure(s->sched[i].overflowSensor);
    }
  }
  weight = findWeight(weightV);

  //save real time to buffer
  strftime(rtelement.value, 80, "%d-%m-%Y,%H:%M:%S", localtime(&tcurrent.time));
  cbWrite(&rtbuffer, &rtelement);
  //save run time to buffer
  sprintf(telement.value, "%f", current_run_time);
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

  for(int i=0;i<s->numEntries;i++){
    if(i<MAXSCHEDENTRIES){
      sprintf(sensorName,"sensor%i",i);
      post_http(&c,
            INFLUX_MEAS(sensorName),
            INFLUX_F_FLT(sensorName, sensor[i], 6),
            INFLUX_TS(ts),
            INFLUX_END);
    }
  }
  

  for (int i = 0; i < s->numEntries; i++) {
    //save overflow sensor measurement to buffer
    sprintf(sensorValue[i].value, "%f", measure(s->sched[i].overflowSensor));
    cbWrite(&sensorBuffer[i], &sensorValue[i]);
  }

  return 1;
}
// Function which prints a table of sensor values to the command line
int getPlot(FillSched *s) {

  printf("Real Time		Run Time (s)	Scale Sensor (V)	");
  for (int i = 0; i < s->numEntries; i++) {
    printf("Sensor %i (V)	", i + 1);
  }
  for (int i = 0; i < s->numEntries; i++) {
    printf("Temp. Sensor %i (K)	", i + 1);
  }
  printf("\n"); //Line break at end

  // Print all elements in the buffers
  for (int i = 0; i < cbCount(&tbuffer); i++) {

    cbRead(&rtbuffer, &rtelement); //read values from buffers
    cbRead(&tbuffer, &telement);
    cbRead(&weightbuffer, &weightelement);
    printf("%s	%s	%s	", rtelement.value, telement.value, weightelement.value); //print readings
    cbWrite(&rtbuffer, &rtelement);                                                           //put values that were just read back in buffers so that they can be read again
    cbWrite(&tbuffer, &telement);
    cbWrite(&weightbuffer, &weightelement);
    for (int i = 0; i < s->numEntries; i++) {
      cbRead(&sensorBuffer[i], &sensorValue[i]);
      printf("%s	", sensorValue[i].value); //print all voltage sensor values
      cbWrite(&sensorBuffer[i], &sensorValue[i]);
    }
    for (int i = 0; i < s->numEntries; i++) {
      cbRead(&tempBuffer[i], &tempValue[i]);
      printf("%s	", tempValue[i].value); //print all temp sensor values
      cbWrite(&tempBuffer[i], &tempValue[i]);
    }
    printf("\n"); //Line break at end
  }
  return 1;
}
// Function which prints a table of sensor values to a text file
int Save(FillSched *s, char *filename) {
  FILE *fp;
  fp = fopen(filename, "w");
  printf("Saving data...\n");

  fprintf(fp, "Real Time		Run Time (s)	Scale Sensor (V)	");
  for (int i = 0; i < s->numEntries; i++) {
    fprintf(fp, "Sensor %i (V)	", i + 1);
  }
  for (int i = 0; i < s->numEntries; i++) {
    fprintf(fp, "Temp. Sensor %i (K)	", i + 1);
  }
  fprintf(fp, "\n"); //Line break at end

  // Print all elements in the buffers
  for (int i = 0; i < cbCount(&tbuffer); i++) {

    cbRead(&rtbuffer, &rtelement); //read values from buffers
    cbRead(&tbuffer, &telement);
    cbRead(&weightbuffer, &weightelement);
    fprintf(fp, "%s	%s	%s	", rtelement.value, telement.value, weightelement.value); //print readings
    cbWrite(&rtbuffer, &rtelement);                                                           //put values that were just read back in buffers so that they can be read again
    cbWrite(&tbuffer, &telement);
    cbWrite(&weightbuffer, &weightelement);
    for (int i = 0; i < s->numEntries; i++) {
      cbRead(&sensorBuffer[i], &sensorValue[i]);
      fprintf(fp, "%s	", sensorValue[i].value); //print all voltage sensor values
      cbWrite(&sensorBuffer[i], &sensorValue[i]);
    }
    for (int i = 0; i < s->numEntries; i++) {
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
int NetSave(FillSched* s, char *filename) {
  /*First save data to local file*/
  Save(s,filename);

  //Convert the given filename into a C string that can be read as a terminal command
  stringstream tmpcommand;
  tmpcommand << "sh plot.sh " << filename << " " << networkloc << " " << s->numEntries << " " << s->numEntries; //THIS PROBABLY DOES NOT WORK
  const std::string tmp = tmpcommand.str();
  const char *command = tmp.c_str();

  //Plot (in gnuplot) and upload file using external bash script
  if((system(command))!=0){
    printf("Netsave completed.\n");
  }

  return 1;
}

/*------------------------------------------------------------*/
/*Function containing fill cycle instructions----------------*/
/*----------------------------------------------------------*/
// valve numbering and wiring for GEARBOX should be:
  // valve 0: valve on the big dewar
  // valve 1: valve to the GEARBOX+stand in the T on the wall (the other valve goes to the 8pi)
  // valve 2: valve to the 8pi in the T on the wall (the other valves goes to GEARBOX+stand)
  // valve 3: valve to the GEARBOX in the T behind the GEARBOX led shield (the other goes to the stand)
  // valve 4: valve to the stand in the T behind the GEARBOX led shield (the other goes to the GEARBOX)
  // valve 5: valve blocking the outlet from the GEARBOX
  // GEARBOX sensor is on channel 0, input from the parameter file is ignored
  // scale sensor is on channel 7
int fill(FillSched *s, int schedEntry) {

  if((schedEntry >= s->numEntries)||(schedEntry < 0)){
    printf("ERROR: Invalid fill schedule entry (%i)!\n",schedEntry);
    exit(-1);
  }

  autosaveSwitch = true; //allows program to autosave again after fill

  //set up timer
  ftime(&tcurrent);
  double tfillstart = GetTime(); //reset fill timer
  double tfillelapsed = GetTime() - tfillstart;

  //print a different message depending on whether the user started fill process manually
  if (signaled.FILL == true)
    printf("\nManual fill requested for %s.  Starting fill at: %s \n",s->sched[schedEntry].entryName,ctime(&tcurrent.time));
  else
    printf("\nStarting fill for %s at: %s \n",s->sched[schedEntry].entryName,ctime(&tcurrent.time));
  signaled.FILL = false;

  //signal that filling is in progress
  signaled.FILLING = true;
  

  //turn on all valves
  chanOn(s->sched[schedEntry].valves,s->sched[schedEntry].numValves);

  //check voltage while filling, and allow viewer to stop filling with the end command
  //filling automatically stops if sfilling time is greater than maxfilltime
  int inum = 0;
  while (((inum < iterations) && signaled.FILLING == true) && (tfillelapsed < maxfilltime)) {
    usleep(1000000); //wait 1s
    //current_run_time = GetTime();
    //printf("current run time %f \n", current_run_time);
    reading = measure(s->sched[schedEntry].overflowSensor); //measure voltage on overflow sensor
    printf("Sensor reading is %10.3f V\n", reading);
    if (reading > threshold)
      inum++;

    if (msg->read(command) == 1) //keep this so that user can still issue commands
    {
      ReadCommand(&signaled, command);
      ProcessSignal(s);
    }

    //figure out how much time has elapsed since filling started
    tfillelapsed = GetTime() - tfillstart;
    if (tfillelapsed > maxfilltime) {
      printf("\nSensor voltage threshold is not being reached.  Threshold may be set poorly, or perhaps LN2 tank is empty.\nAborting run ...\n");

      if (email == true) {
        /*Converting the email message into a C string that can be read as a terminal command*/
        stringstream tmpcommand;
        tmpcommand << "sh emailalert.sh "
                    << "\"" << mailaddress << "\" "
                    << "The LN2 system was shut off automatically when filling " << s->sched[schedEntry].entryName << " since the sensor did not indicate filling was done after " << maxfilltime << " seconds.";
        const std::string tmp = tmpcommand.str();
        const char *command = tmp.c_str();
        /*send email using external bash script*/
        if((system(command))!=0){
          printf("Email sent.\n");
        }
      }
      signaled.FILLING = false;
    }

    recordMeasurement(s);
  }


  //take action depending on whether filling was finished normally or stopped by user
  if (signaled.FILLING == true) {
    signaled.FILLING = false;
    printf("\nSensor threshold reached.  Finishing fill for %s ... \n\n",s->sched[schedEntry].entryName);

    if (email == true) {
      /*Convert the email message into a C string that can be read as a terminal command*/
      stringstream tmpcommand;
      tmpcommand << "sh emailalert.sh "
                 << "\"" << mailaddress << "\" "
                 << "LN2 system filling operation for " << s->sched[schedEntry].entryName << " was successfully completed.  Fill time was " << tfillelapsed << " seconds.";
      const std::string tmp = tmpcommand.str();
      const char *command = tmp.c_str();
      /*send email using external bash script*/
      if((system(command))!=0){
        printf("Email sent.\n");
      }
    }

  } else {
    printf("\nFilling stopped partway, closing all valves ... \n\n");
  }

  chanOff(); //close all valves
  usleep(1000000); //wait a bit so that switching between valves isn't instantaneous

  s->sched[schedEntry].schedFlag=0; //reset the fill flag
  ProcessSignal(s);

  return 1;
}

int autosaveData(FillSched* s) {
  printf("\n Autosaving data ...\n\n");

  char st[80];
  time_t t = time(0);
  strftime(st, 80, "%d%m%Y", localtime(&t));
  autosaveCounter += 1;
  // Generate a filename to autosave with
  stringstream tmpascommand;
  tmpascommand << st << "autosave" << autosaveCounter;
  std::string tmp = tmpascommand.str();
  const char *autosavename = tmp.c_str();
  NetSave(s, const_cast<char *>(autosavename));

  return 1;
}
/*--------------------------------------------------------------*/
int readParameters(void) {
  // Read parameters from text file parameters.dat
  char *tok;
  char str[256],fullLine[256],parameter[256],value[256];

  FILE *parfile = fopen("parameters.dat", "r");

  while(!(feof(parfile)))//go until the end of file is reached
    {
			if(fgets(str,256,parfile)!=NULL) //get an entire line
				{
          strcpy(fullLine,str);
					tok=strtok(str,"[");
          if(tok!=NULL){
            tok[strcspn(tok, "\r\n")] = 0;//strips newline characters from the string
            strcpy(parameter,tok);
            tok = strtok (NULL, "]");
            if(tok!=NULL){
              tok[strcspn(tok, "\r\n")] = 0;//strips newline characters from the string
              strcpy(value,tok);
              if((parameter!=NULL)&&(value!=NULL)){
                if(strcmp(parameter,"sensor_threshold_V")==0){
                  threshold = atof(value);
                }else if(strcmp(parameter,"scale_threshold_kg")==0){
                  scale_threshold = atof(value);
                }else if(strcmp(parameter,"sensor_reading_interval_ms")==0){
                  polling_time = atoi(value) * 1000; //convert from milliseconds into microseconds
                }else if(strcmp(parameter,"readings_before_fill_stop")==0){
                  iterations = atoi(value);
                }else if(strcmp(parameter,"max_filling_time")==0){
                  maxfilltime = atof(value);
                }else if(strcmp(parameter,"buffer_size")==0){
                  circBufferSize = atoi(value);
                }else if(strcmp(parameter,"autosave")==0){
                  autosave = atoi(value);
                }else if(strcmp(parameter,"upload_loc")==0){
                  strcpy(networkloc,value);
                }else if(strcmp(parameter,"send_email")==0){
                  email = atoi(value);
                }else if(strcmp(parameter,"email_adress")==0){
                  strcpy(mailaddress,value);
                }
              }
            }
          }
        }    
    }

  printf("\nFile 'parameters.dat' read sucessfully!\n");
  printf("Sensor threshold to indicate LN2 overflow (V) = %.2f \n", threshold);
  printf("Weight at which tank needs refilling (kg) = %.2f \n", scale_threshold);
  printf("Time between readings when not filling (microsec) = %i \n", polling_time);
  printf("Number of measurements allowed above sensor threshold = %i \n", iterations);
  printf("Maximum length of time filling can take place (s) = %.0f \n", maxfilltime);
  printf("Number of saved data points = %i \n", circBufferSize);
  if(autosave==1){
    printf("Will autosave data to: %s\n", networkloc);
  }else{
    printf("Will not autosave data.\n");
  }
  if(email==1){
    printf("Will send email alerts to: %s\n", mailaddress);
  }else{
    printf("Will not send email alerts.\n");
  }
  
  fclose(parfile);

  return 1;
}

int readCalibration(void) {
  // Read scale/sensor calibration data from text file calibration.dat
  char *tok;
  char str[256],fullLine[256],parameter[256],value[256];

  FILE *parfile = fopen("calibration.dat", "r");

  while(!(feof(parfile)))//go until the end of file is reached
    {
			if(fgets(str,256,parfile)!=NULL) //get an entire line
				{
          strcpy(fullLine,str);
					tok=strtok(str,"[");
          if(tok!=NULL){
            tok[strcspn(tok, "\r\n")] = 0;//strips newline characters from the string
            strcpy(parameter,tok);
            tok = strtok (NULL, "]");
            if(tok!=NULL){
              tok[strcspn(tok, "\r\n")] = 0;//strips newline characters from the string
              strcpy(value,tok);
              if((parameter!=NULL)&&(value!=NULL)){
                if(strcmp(parameter,"V_to_weight_A")==0){
                  scaleFit[0] = atof(value);
                }else if(strcmp(parameter,"V_to_weight_B")==0){
                  scaleFit[1] = atof(value);
                }
              }
            }
          }
        }    
    }
  
  printf("\nFile 'calibration.dat' read sucessfully!\n");
  printf("Scale voltage to weight calibration parameters = %.6f, %.6f\n",scaleFit[0],scaleFit[1]);

  fclose(parfile);

  

  return 1;
}

void readSchedule(FillSched *s){

	char *tok,*tok2;
  char str[256];//string to be read from file (will be tokenized)
  char fullLine[256], line[256];
	int k;
	int currentEntry=0;
	int currentParameter=0;
	int val=0;

	FILE *schedfile = fopen("schedule.dat", "r");

	while(!(feof(schedfile)))//go until the end of file is reached
    {
			if(fgets(str,256,schedfile)!=NULL) //get an entire line
				{
					strcpy(fullLine,str);
					currentParameter=1;
					while(true){
						//printf("%s\n",str); //print the line (debug)
            s->sched[currentEntry].lastTriggerTime = 0.0;
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
								}else if(strcmp(line,"overflow_sensor")==0){
                  tok = strtok (NULL, "]");
									tok[strcspn(tok, "\r\n")] = 0;//strips newline characters from the string
									//printf("sensor: %s\n",tok);
                  s->sched[currentEntry].overflowSensor = atoi(tok);
                }else if(strcmp(line,"time")==0){
									tok = strtok (NULL, "]");
									tok[strcspn(tok, "\r\n")] = 0;//strips newline characters from the string
									strcpy(line,tok);
									tok2=strtok (line,",");
									tok2[strcspn(tok2, "\r\n")] = 0;//strips newline characters from the string
									if(strcmp(tok2,"sunday")==0){
										s->sched[currentEntry].schedMode=0;
									}else if(strcmp(tok2,"monday")==0){
										s->sched[currentEntry].schedMode=1;
									}else if(strcmp(tok2,"tuesday")==0){
										s->sched[currentEntry].schedMode=2;
									}else if(strcmp(tok2,"wednesday")==0){
										s->sched[currentEntry].schedMode=3;
									}else if(strcmp(tok2,"thursday")==0){
										s->sched[currentEntry].schedMode=4;
									}else if(strcmp(tok2,"friday")==0){
										s->sched[currentEntry].schedMode=5;
									}else if(strcmp(tok2,"saturday")==0){
										s->sched[currentEntry].schedMode=6;
									}else if(strcmp(tok2,"by_minute")==0){
										s->sched[currentEntry].schedMode=7;
									}else if(strcmp(tok2,"after_entry")==0){
										s->sched[currentEntry].schedMode=8;
									}else{
										printf("ERROR: Invalid schedule interval in schedule entry %i.  Valid values are [monday,tuesday,wednesday,thursday,friday,saturday,sunday,by_minute,after_entry].\n",currentEntry+1);
										exit(-1);
									}
									if(s->sched[currentEntry].schedMode==7){
										//get the interval in minutes
										tok2 = strtok (NULL, "]");
										tok2[strcspn(tok2, "\r\n")] = 0;//strips newline characters from the string
										s->sched[currentEntry].schedMin = atoi(tok2);
									}else if(s->sched[currentEntry].schedMode==8){
										//get the interval in minutes
										tok2 = strtok (NULL, "]");
										tok2[strcspn(tok2, "\r\n")] = 0;//strips newline characters from the string
										strcpy(s->sched[currentEntry].schedAfterEntryName,tok2);
									}else{
										tok2 = strtok (NULL, ":");
										tok2[strcspn(tok2, "\r\n")] = 0;//strips newline characters from the string
										val=atoi(tok2);
										if((val>23)||(val<0)){
											printf("ERROR: Invalid hour specified in schedule entry %i.  Valid range is [0,23].\n",currentEntry+1);
											exit(-1);
										}else{
											s->sched[currentEntry].schedHour = val;
										}										
										tok2 = strtok (NULL, "]");
										tok2[strcspn(tok2, "\r\n")] = 0;//strips newline characters from the string
										val=atoi(tok2);
										if((val>59)||(val<0)){
											printf("ERROR: Invalid minute specified in schedule entry %i.  Valid range is [0,59].\n",currentEntry+1);
											exit(-1);
										}else{
											s->sched[currentEntry].schedMin = val;
										}
										
									}
									

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
  s->numEntries = currentEntry-1;
	fclose(schedfile);


  //convert entry names to indices
  for(int i=0;i<s->numEntries;i++){
    if(s->sched[i].schedMode==8){
      
      s->sched[i].schedAfterEntry = -1;
      for(int j=0;j<s->numEntries;j++){
        if(strcmp(s->sched[i].schedAfterEntryName,s->sched[j].entryName)==0){
          if(i!=j){
            s->sched[i].schedAfterEntry = j;
          }else{
            printf("ERROR: schedule entry %i (%s) cannot be scheduled directly after itself!\n",i,s->sched[i].entryName);
            exit(-1);
          }
          
        }
      }
      if(s->sched[i].schedAfterEntry == -1){
        printf("ERROR: schedule entry %i (%s) cannot be scheduled after the non-existent entry: %s\n",i,s->sched[i].entryName,s->sched[i].schedAfterEntryName);
        exit(-1);
      }
    }
  }

	//report on fill schedule info that was read in
	
	printf("\nFill schedule read. %i entries found.\n",s->numEntries);
	for(int i=0;i<s->numEntries;i++){
		printf("Schedule entry %i: %s, valves: [",i+1,s->sched[i].entryName);
		for (int j=0;j<s->sched[i].numValves;j++){
			printf(" %i",s->sched[i].valves[j]);
		}
		printf(" ], overflow sensor [ %i ], filling ",s->sched[i].overflowSensor);
		if(s->sched[i].schedMode==0){
			printf("every sunday at %.2i:%.2i.\n",s->sched[i].schedHour,s->sched[i].schedMin);
		}else if(s->sched[i].schedMode==1){
			printf("every monday at %.2i:%.2i.\n",s->sched[i].schedHour,s->sched[i].schedMin);
		}else if(s->sched[i].schedMode==2){
			printf("every tuesday at %.2i:%.2i.\n",s->sched[i].schedHour,s->sched[i].schedMin);
		}else if(s->sched[i].schedMode==3){
			printf("every wednesday at %.2i:%.2i.\n",s->sched[i].schedHour,s->sched[i].schedMin);
		}else if(s->sched[i].schedMode==4){
			printf("every thursday at %.2i:%.2i.\n",s->sched[i].schedHour,s->sched[i].schedMin);
		}else if(s->sched[i].schedMode==5){
			printf("every friday at %.2i:%.2i.\n",s->sched[i].schedHour,s->sched[i].schedMin);
		}else if(s->sched[i].schedMode==6){
			printf("every saturday at %.2i:%.2i.\n",s->sched[i].schedHour,s->sched[i].schedMin);
		}else if(s->sched[i].schedMode==7){
			printf("every %i minute(s).\n",s->sched[i].schedMin);
		}else if(s->sched[i].schedMode==8){
			printf("directly after schedule entry: %s\n",s->sched[s->sched[i].schedAfterEntry].entryName);
		}else{
			printf("UNDEFINED\n");
			exit(-1);
		}
	}
	printf("\n");
}

// Function which converts scale voltage values into weight
// Currently using a very rough calibration defined in calibration.dat
double findWeight(double vScale) {

  // returns weight in kg, based on linear fit to calibration curve
  return (vScale * scaleFit[0] + scaleFit[1]);
}




int main()
{
  
  printf("\nSFU-NSL LN2 control system server program\n");
  printf("-----------------------------------------\n\n");

  int retval;
  FillSched *s=(FillSched*)calloc(1,sizeof(FillSched)); 

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

  retval = Boot(s);
  if (retval < 0)
    exit(retval);

  MainLoop(s); //run the main loop

  free(s);

  exit(EXIT_SUCCESS);
}
