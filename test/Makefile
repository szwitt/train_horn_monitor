# Host build of the detection pipeline (no Particle dependencies).
CXX ?= g++
CXXFLAGS ?= -std=c++17 -O2 -Wall -Wextra -Werror -I../src
LIB_SRCS = ../src/SoundLevel.cpp ../src/Spectrum.cpp ../src/Detectors.cpp ../src/Episode.cpp \
           ../src/NodeProcessor.cpp
SRCS = $(LIB_SRCS) test_main.cpp

all: run

test_runner: $(SRCS) ../src/*.h
	$(CXX) $(CXXFLAGS) -o $@ $(SRCS)

# Replays a WAV plus meter trace through the pipeline (see tools/bench)
replay: $(LIB_SRCS) replay.cpp ../src/*.h
	$(CXX) $(CXXFLAGS) -o $@ $(LIB_SRCS) replay.cpp

run: test_runner
	./test_runner

clean:
	rm -f test_runner replay

.PHONY: all run clean
