#ifndef COMMON_H
#define COMMON_H
#include <string>
#include <vector>

enum LoggingLevel {
    SILENT,
    COMMUNICATION,
    CRITICAL_ERROR,
    NONCRITICAL_ERROR,
    DIAGNOSTIC
};

// Funckje pomocnicze, do czytania z wejścia:
uint16_t read_port(const std::string& str);
uint32_t read_timeout(const std::string& str);
uint8_t to_uint8(uint32_t x);
uint32_t read_verbosity(const std::string& str);
std::vector<std::string> split(const std::string& s, char delimiter);
void trim(std::string& s);
#endif
