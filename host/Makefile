CXX ?= c++
CXXFLAGS ?= -std=c++17 -O1 -g -Wall -Wextra -Wno-unused-parameter -I../core -I.
CORE := $(wildcard ../core/*.cpp)
HOST := StdioFs.cpp

all: repl tests

repl: repl.cpp $(CORE) $(HOST) ../core/*.h
	$(CXX) $(CXXFLAGS) -o $@ repl.cpp $(CORE) $(HOST)

tests: tests.cpp $(CORE) $(HOST) ../core/*.h
	$(CXX) $(CXXFLAGS) -o $@ tests.cpp $(CORE) $(HOST)

test: tests
	rm -rf ./testdata && ./tests

clean:
	rm -rf repl tests testdata bbsdata *.dSYM

.PHONY: all test clean
