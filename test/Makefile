# Host build of the detection pipelines (no Particle dependencies).
CXX ?= g++
CXXFLAGS ?= -std=c++17 -O2 -Wall -Wextra -Werror
LIB_SRCS = ../src/SoundLevel.cpp ../src/Spectrum.cpp ../src/Detectors.cpp ../src/Episode.cpp \
           ../src/NodeProcessor.cpp
SRCS = $(LIB_SRCS) test_main.cpp
LIGHTS_SRCS = ../flasher/src/FlashDetector.cpp test_lights.cpp

all: run

test_runner: $(SRCS) ../src/*.h
	$(CXX) $(CXXFLAGS) -I../src -o $@ $(SRCS)

lights_runner: $(LIGHTS_SRCS) ../flasher/src/*.h
	$(CXX) $(CXXFLAGS) -I../flasher/src -o $@ $(LIGHTS_SRCS)

# Replays a WAV plus meter trace through the pipeline (see tools/bench)
replay: $(LIB_SRCS) replay.cpp ../src/*.h
	$(CXX) $(CXXFLAGS) -I../src -o $@ $(LIB_SRCS) replay.cpp

run: test_runner lights_runner
	./test_runner
	./lights_runner

clean:
	rm -f test_runner lights_runner replay

.PHONY: all run clean
