CXX=g++        
CXXFLAGS:=-g -Wall -I.
LIBS:=


INCLUDES:=

OBJECTS:=


SRCS:=

all: client LN2_master flush

client: msgtool.cpp msgtool.h client.cpp
	$(CXX) -o  $@ client.cpp  msgtool.cpp $(CXXFLAGS)
flush: msgtool.cpp msgtool.h flush.cpp
	$(CXX) -o  $@ flush.cpp  msgtool.cpp $(CXXFLAGS)
LN2_master: msgtool.cpp msgtool.h master.cpp
	$(CXX) -o  $@  master.cpp  msgtool.cpp $(CXXFLAGS)


%.o: %.cpp 
	$(CXX) $(CXXFLAGS) -c $< 



clean: 
	@rm -f *.o *~ client LN2_master flush

very-clean:
	@rm -f client LN2_master flush *.o *~

.PHONY: clean very-clean
