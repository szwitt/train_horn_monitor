# Host build of the detection pipeline (no Particle dependencies).
CXX ?= g++
CXXFLAGS ?= -std=c++17 -O2 -Wall -Wextra -Werror -I../src
SRCS = ../src/SoundLevel.cpp ../src/Spectrum.cpp ../src/Detectors.cpp ../src/Episode.cpp \
       ../src/NodeProcessor.cpp test_main.cpp

all: run

test_runner: $(SRCS) ../src/*.h
	$(CXX) $(CXXFLAGS) -o $@ $(SRCS)

run: test_runner
	./test_runner

clean:
	rm -f test_runner

.PHONY: all run clean
