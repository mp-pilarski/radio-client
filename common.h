#ifndef COMMON_H
#define COMMON_H
#include <string>

// Funckje pomocnicze, do czytania z wejścia:
uint16_t read_port(const std::string& str);
uint8_t read_timeout(const std::string& str);
uint8_t to_uint8(uint32_t x);
uint32_t read_field(const std::string& str);
#endif
