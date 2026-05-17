CXX     = g++
#CXXFLAGS = -Wall -Wextra -std=c++20
#LDFLAGS =
CXXFLAGS = -Wall -Wextra -std=c++20 -fsanitize=address -Wformat-security -Wduplicated-cond -Wfloat-equal -Wshadow -Wconversion -Wjump-misses-init -Wlogical-not-parentheses -Wnull-dereference -fstack-protector-strong -fsanitize=undefined -fno-sanitize-recover -g -fno-omit-frame-pointer -lssl -lcrypto
LDFLAGS = -Wall -Wextra -std=c++20 -fsanitize=address -Wformat-security -Wduplicated-cond -Wfloat-equal -Wshadow -Wconversion -Wjump-misses-init -Wlogical-not-parentheses -Wnull-dereference -fstack-protector-strong -fsanitize=undefined -fno-sanitize-recover -g -fno-omit-frame-pointer -lssl -lcrypto

.PHONY: all clean

TARGET = sikradio

all: $(TARGET)

$(TARGET): $(TARGET).o common.o connection.o
	$(CXX) $(CXXFLAGS) $^ -o $@ $(LDFLAGS)

connection.o: connection.cpp connection.h
common.o: common.cpp common.h
sikradio.o: sikradio.cpp common.h

clean:
	rm -f $(TARGET) *.o *~
