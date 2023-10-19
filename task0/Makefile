# Set you prefererred CFLAGS/compiler compiler here.
# Our github runner provides gcc-10 by default.
CC ?= cc
CFLAGS ?= -g -Wall -O2
CXX ?= c++
CXXFLAGS ?= -g -Wall -O0
LDFLAGS = -lprotobuf -lpthread
CURRENT_DIR := $(dir $(abspath $(lastword $(MAKEFILE_LIST))))

.PHONY: all clean

all: libutils.so server client

clean:
	-rm -f server client libutils.so message.pb.*
	make -C tests clean

message.pb.cc: message.proto
	protoc --cpp_out=. $^

libutils.so: utils.cpp message.pb.cc
	$(CXX) $(CXXFLAGS) -shared -fPIC -o $@ utils.cpp message.pb.cc $(LDFLAGS)

server: server.cpp libutils.so message.pb.cc
	$(CXX) $(CXXFLAGS) -o $@ server.cpp message.pb.cc -L. -Wl,-rpath=$(CURRENT_DIR) -lutils $(LDFLAGS)

client: client.cpp libutils.so message.pb.cc
	$(CXX) $(CXXFLAGS) -o $@ client.cpp message.pb.cc -L. -Wl,-rpath=$(CURRENT_DIR) -lutils $(LDFLAGS)

# Usually there is no need to modify this
check: all
	$(MAKE) -C tests check
