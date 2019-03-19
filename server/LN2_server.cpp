#include "LN2_server.h"
#include "crate.h"
#include "crate.cpp"

int main()
{
  int retval;
  FillSched *s=(FillSched*)calloc(1,sizeof(FillSched)); 

  Crate *crate=new Crate();
  retval = crate->Boot();
  if (retval < 0)
    exit(retval);

  crate->MainLoop(s); //run the main loop

  free(s);

  exit(EXIT_SUCCESS);
}
