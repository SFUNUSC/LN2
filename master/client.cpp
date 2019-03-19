#include "msgtool.h"
#include <iostream>


using namespace std;

int main(int argc, char *arg[])
{
  MsgQ *test=new MsgQ();
  char *message=new char[4096];
  
  int retval=test->read(message);
  if(retval==1)
    cout<<message<<endl;
  else
    cout<<"no message\n";
  return 1;
}
