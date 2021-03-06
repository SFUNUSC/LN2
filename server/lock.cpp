#include "lock.h"

lock::lock(std::string name) {
  name_base = ""; //base directory for the lockfile (for now just use the server program directory)
  name_base += name;
  l = fopen(name_base.c_str(), "r");
  if (l == NULL) //if the file does not exist, the device is free
  {
    l = fopen(name_base.c_str(), "w");
    if (l == NULL) {
      printf("%s ", name_base.c_str());
      printf("can't make a lock file\n");
      printf("cowardly bailing out ...\n");
      exit(1);
    }
    fprintf(l, "lock");
    fclose(l);
  } else {
    printf("the device seems to be locked by another process (%s)\n", name_base.c_str());
    exit(1);
  }
}

lock::~lock() {
  unlock();
}

void lock::unlock() {
  // printf(name_base.c_str());
  remove(name_base.c_str());
}
