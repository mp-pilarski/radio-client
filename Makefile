CXX     = g++
CXXFLAGS = -Wall -Wextra -std=c++20
LDFLAGS =

.PHONY: all clean

TARGET = sikradio

all: $(TARGET)

$(TARGET): $(TARGET).o common.o
	$(CXX) $(CXXFLAGS) $^ -o $@ $(LDFLAGS)

common.o: common.cpp common.h
sikradio.o: sikradio.cpp common.h

clean:
	rm -f $(TARGET) *.o *~
