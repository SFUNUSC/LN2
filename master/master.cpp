#include "msgtool.h"
#include <iostream>
#include <string>

using namespace std;

int main(int argc, char *argv[])
{

  if(argc==1)
    {
      printf("Usage: %s message\n",argv[0]);
      exit(1);
    }
  
  MsgQ *test=new MsgQ();

  
  
  if(argc==3)
    {
      string one,two,three;
      one=argv[1]; two=argv[2];
      three=" ";
      one=one+three+two;
      test->send((char *)one.c_str());
    }
  else

    test->send(argv[1]);

  
  return 1;
}
