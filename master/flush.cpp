#include "msgtool.h"
#include <iostream>


using namespace std;

int main(int argc, char *arg[])
{
  
  MsgQ *test=new MsgQ();
  char *message=new char[4096];
  
  int retval= 1;
  while(retval==1)
    retval=test->read(message);
  
  return 1;
}
