#include <string>
#include <cstdio>
#include <cstdlib>
using namespace std;

class lock
{
public:
  lock(std::string name);
  ~lock(void);
  void unlock(void);
private:
  FILE *l;
  std::string name_base;
};
