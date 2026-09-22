#ifndef COMMON_H
#define COMMON_H
#include <string>
#include <vector>
#include <cstdint>

enum LoggingLevel {
    SILENT,
    COMMUNICATION,
    CRITICAL_ERROR,
    NONCRITICAL_ERROR,
    DIAGNOSTIC
};

constexpr uint16_t METADATA_MULT = 16;
constexpr size_t SERVER_FD = 0;
constexpr size_t STDIN_FD = 1;
constexpr uint32_t DEFAULT_TIMEOUT = 5000;
constexpr size_t SMALL_BUF = 1024; // 1kB
constexpr size_t BIG_BUF = 32*1024; // 16kB


// Funckje pomocnicze, do czytania z wejścia:
uint16_t read_port(const std::string& str);
uint32_t read_timeout(const std::string& str);
uint8_t to_uint8(uint32_t x);
uint32_t read_verbosity(const std::string& str);
std::vector<std::string> split(const std::string& s, char delimiter);
void trim(std::string& s);
void printCurrentTime();
std::string prepareForGetAddr(std::string& s);
void stringToLower(std::string& s);
size_t safe_stdout_write(const char* buf, size_t n);
#endif
