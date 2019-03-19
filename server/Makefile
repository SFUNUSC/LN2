CXX=g++        
CXXFLAGS:=-m32 -g -Wall -O2 -fPIC -ansi
NILIBS= -lnidaqmxbase
INCLUDES:=-I/usr/local/natinst/nidaqmxbase/include/ 
OBJECTS:=LN2_server.o msgtool.o lock.o
SRCS:=LN2_server.cpp msgtool.cpp lock.cpp output.cpp


all: no_nidaqmx

LN2_server: $(OBJECTS) msgtool.h lock.h crate.h
	$(CXX) -o  LN2_server  $(OBJECTS) $(CXXFLAGS) $(INCLUDES) $(NILIBS) $(ROOT) -lm -ldl
	
no_nidaqmx: $(OBJECTS) LN2_server.h msgtool.h lock.h crate.h
	$(CXX) -o  LN2_server  $(OBJECTS) $(CXXFLAGS) $(INCLUDES) $(ROOT) -lm -ldl

crate.o:crate.cpp crate.h
	$(CXX) -c crate.cpp -o crate.o $(CXXFLAGS) $(INCLUDES) 

LN2_server.o:LN2_server.cpp crate.h
	$(CXX) -c LN2_server.cpp -o LN2_server.o $(CXXFLAGS) $(INCLUDES)

lock.o:lock.cpp lock.h
	$(CXX) -c lock.cpp -o lock.o $(CXXFLAGS) $(INCLUDES) 

msgtool.o:msgtool.cpp msgtool.h
	$(CXX) -c msgtool.cpp -o msgtool.o $(CXXFLAGS) $(INCLUDES) 

clean: 
	@rm -f *.o *~

very-clean:
	@rm -f LN2 *.o *~
