CXX=g++        
CXXFLAGS:=-m32 -g -Wall -O2 -fPIC -ansi
NILIBS= -lnidaqmxbase
INCLUDES:=-I/usr/local/natinst/nidaqmxbase/include/ 
OBJECTS_TEST:=LN2_server.o msgtool.o lock.o test_control.o
OBJECTS_NIDAQ:=LN2_server.o msgtool.o lock.o nidaq_control.o
SRCS:=LN2_server.cpp msgtool.cpp lock.cpp output.cpp


all: LN2_server_nidaq

LN2_server_nidaq: $(OBJECTS_NIDAQ) LN2_server.h msgtool.h lock.h crate.h
	$(CXX) -o  LN2_server $(OBJECTS_NIDAQ) $(CXXFLAGS) $(INCLUDES) $(NILIBS) $(ROOT) -lm -ldl

LN2_server_test: $(OBJECTS_TEST) LN2_server.h msgtool.h lock.h crate.h
	$(CXX) -o  LN2_server $(OBJECTS_TEST) $(CXXFLAGS) $(INCLUDES) $(ROOT) -lm -ldl

crate.o:crate.cpp crate.h
	$(CXX) -c crate.cpp -o crate.o $(CXXFLAGS) $(INCLUDES) 

LN2_server.o:LN2_server.cpp crate.h
	$(CXX) -c LN2_server.cpp -o LN2_server.o $(CXXFLAGS) $(INCLUDES)

test_control.o:test_control.cpp test_control.h
	$(CXX) -c test_control.cpp -o test_control.o $(CXXFLAGS) $(INCLUDES) 

nidaq_control.o:nidaq_control.cpp nidaq_control.h
	$(CXX) -c nidaq_control.cpp -o nidaq_control.o $(CXXFLAGS) $(INCLUDES) 

lock.o:lock.cpp lock.h
	$(CXX) -c lock.cpp -o lock.o $(CXXFLAGS) $(INCLUDES) 

msgtool.o:msgtool.cpp msgtool.h
	$(CXX) -c msgtool.cpp -o msgtool.o $(CXXFLAGS) $(INCLUDES) 

clean: 
	@rm -f LN2_server *.o *~

very-clean:
	@rm -f LN2 LN2_server *.o *~
