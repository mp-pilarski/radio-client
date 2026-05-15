#include <cstdio>
#include <netdb.h>
#include <stdexcept>
#include <limits>
#include "common.h"

// name to opis pochodzenia tekstu, potrzebny do ładnego informowania użytkownika o błędnych danych.
static long long read_number(const std::string& str, long long min_val, long long max_val, const std::string& name) {
    try {
        size_t processed_chars = 0;
        long long val = std::stoll(str, &processed_chars);

        // W tekście są znaki inne niż cyfry.
        if (processed_chars != str.length()) {
            throw std::invalid_argument("Not a number");
        }
        // Tekst opisuje wartość spoza zakresu.
        if (val < min_val || val > max_val) {
            throw std::invalid_argument("Value out of range [" + std::to_string(min_val) + ", " + std::to_string(max_val) + "]");
        }
        return val;
    } catch (const std::exception& e) {
        throw std::invalid_argument("Error in " + name + ": " + e.what());
    }
}

uint16_t read_port(const std::string& str) {
    return static_cast<uint16_t>(read_number(str, 0, std::numeric_limits<uint16_t>::max(), "-p"));
}

uint8_t read_timeout(const std::string& str) {
    return static_cast<uint8_t>(read_number(str, 100, 100000, "-t"));
}

uint32_t read_field(const std::string& str) {
    return static_cast<uint32_t>(read_number(str, 0, std::numeric_limits<uint32_t>::max(), "message"));
}

uint8_t to_uint8(uint32_t x) {
    uint8_t max_val = std::numeric_limits<uint8_t>::max();
    if (x > max_val) {
        throw std::invalid_argument("Value out of range [0 , " + std::to_string(max_val) + "]");
    }
    return static_cast<uint8_t>(x);
}
