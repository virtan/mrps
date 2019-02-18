CXXFLAGS = -Os -march=native -mtune=native -std=c++17 -Wall
DEBUG_CXXFLAGS = -g -O0
CPPFLAGS = -I/opt/local/include
LDFLAGS = -L/opt/local/lib -lboost_system-mt -pthread
SERVER = server

default: $(SERVER)
	
$(SERVER): $(addsuffix .cc, $(SERVER))

debug:
	@CXXFLAGS="$(CXXFLAGS) $(DEBUG_CXXFLAGS)" make -e $(SERVER)

.PHONY: clean
clean:
	@rm -rf server server.dSYM
