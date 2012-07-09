CXXFLAGS = -Os -march=native -mtune=native -std=c++0x -Wall
DEBUG_CXXFLAGS = -g -O0
CPPFLAGS = -I/opt/local/include
LDFLAGS = -L/opt/local/lib -lboost_thread-mt -lboost_system-mt -pthread
CLIENT = client

default: $(CLIENT)
	
$(CLIENT): $(addsuffix .cc, $(CLIENT))

debug:
	@CXXFLAGS="$(CXXFLAGS) $(DEBUG_CXXFLAGS)" make -e $(CLIENT)

.PHONY: clean
clean:
	@rm -rf client client.dSYM
