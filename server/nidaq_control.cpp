#include "nidaq_control.h"

/*------------------------------------------------------------*/
/*Functions controlling the DAQ------------------------------*/
/*----------------------------------------------------------*/
int chanOn(int chan) {

  if(chan<0){
    printf("Invalid channel specified (%i), not taking any action.",chan);
    return;
  }

  printf("Turning on channel %i.\n", chan);

  int32 error = 0;
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
  printf("Turned on channel %i.\n", chan);
  //getchar();
  return 1;
}
/*--------------------------------------------------------------*/
int chanOff(void) {

  printf("Turning off all channels.\n");

  int32 error = 0;
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
  printf("Turned off all channels.\n");
  //getchar();*/
  return 0;
}
/*--------------------------------------------------------------*/
float measure(int channel) {

  if(channel<0){
    printf("Invalid channel specified (%i), returning 10 V.",channel);
    return 10.0f;
  }

  //Generate the DAQ channel (eg. Dev1/ai1) that will be measured
  char *mchannel = "Dev1/ai";
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
  return avg;
}
