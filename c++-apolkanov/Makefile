CXX=g++-4.5
CXXFLAGS=-c -Wall -g -I$(BOOST_HOME)/include
LDFLAGS=-L$(BOOST_HOME)/lib -lboost_thread -pthread
SOURCES=echo_tcp.cpp
OBJECTS=$(SOURCES:.cpp=.o)
EXECUTABLE=echo_tcp

all: $(SOURCES) $(EXECUTABLE)
	
$(EXECUTABLE): $(OBJECTS) 
	$(CXX) $^ $(LDFLAGS) -o $@

clean:
	rm -rf *o $(EXECUTABLE)
