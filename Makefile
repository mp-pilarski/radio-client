CXX     = g++
CXXFLAGS = -Wall -Wextra -std=c++20 -O2
LDLIBS = -lssl -lcrypto

.PHONY: all clean

TARGET = sikradio

all: $(TARGET)

$(TARGET): $(TARGET).o common.o connection.o cookie.o
	$(CXX) $(CXXFLAGS) $^ -o $@ $(LDLIBS)

connection.o: connection.cpp connection.h
common.o: common.cpp common.h
sikradio.o: sikradio.cpp common.h connection.h cookie.h
cookie.o: cookie.cpp cookie.h common.h common.cpp

clean:
	rm -f $(TARGET) *.o *~
