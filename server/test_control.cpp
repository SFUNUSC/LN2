#include "test_control.h"

/*------------------------------------------------------------*/
/*Functions controlling the DAQ------------------------------*/
/*----------------------------------------------------------*/
int chanOn(int* chan, int numChans) {

  for(int i=0;i<numChans;i++){
    printf("Turning on channel %i (test controller).\n",chan[i]);
  }
  
  return 1;
}
/*--------------------------------------------------------------*/
int chanOn(int chan) {
  
  printf("Turning on channel %i (test controller).\n",chan);
  
  return 1;
}
/*--------------------------------------------------------------*/
int chanOff(void) {

  printf("Turning off all channels (test controller).\n");
  return 0;
}
/*--------------------------------------------------------------*/
float measure(int channel) {
	return 10.0;
}
