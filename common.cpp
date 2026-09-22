#include "common.h"

#include <netdb.h>
#include <unistd.h>

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <limits>
#include <sstream>
#include <stdexcept>

// name to opis pochodzenia tekstu, potrzebny do ładnego informowania
// użytkownika o błędnych danych.
static long long read_number(const std::string& str, long long min_val,
                             long long max_val, const std::string& name) {
    try {
        size_t processed_chars = 0;
        long long val = std::stoll(str, &processed_chars);

        // W tekście są znaki inne niż cyfry.
        if (processed_chars != str.length()) {
            throw std::invalid_argument("Not a number");
        }
        // Tekst opisuje wartość spoza zakresu.
        if (val < min_val || val > max_val) {
            throw std::invalid_argument("Value out of range [" +
                                        std::to_string(min_val) + ", " +
                                        std::to_string(max_val) + "]");
        }
        return val;
    } catch (const std::exception& e) {
        throw std::invalid_argument("Error in " + name + ": " + e.what());
    }
}

uint16_t read_port(const std::string& str) {
    return static_cast<uint16_t>(
        read_number(str, 0, std::numeric_limits<uint16_t>::max(), "-p"));
}

uint32_t read_timeout(const std::string& str) {
    return static_cast<uint32_t>(read_number(str, 100, 100000, "-t"));
}

uint32_t read_verbosity(const std::string& str) {
    return static_cast<uint32_t>(read_number(str, 0, 4, "-v"));
}

uint8_t to_uint8(uint32_t x) {
    uint8_t max_val = std::numeric_limits<uint8_t>::max();
    if (x > max_val) {
        throw std::invalid_argument("Value out of range [0 , " +
                                    std::to_string(max_val) + "]");
    }
    return static_cast<uint8_t>(x);
}

// Funkcja dzieląca string wg separatora (np. średnika)
std::vector<std::string> split(const std::string& s, char delimiter) {
    std::vector<std::string> tokens;
    std::string token;
    std::istringstream tokenStream(s);
    while (std::getline(tokenStream, token, delimiter)) {
        trim(token);
        tokens.push_back(token);
    }
    return tokens;
}

void trim(std::string& s) {
    s.erase(0, s.find_first_not_of(" \t\r\n"));
    s.erase(s.find_last_not_of(" \t\r\n") + 1);
}

std::string prepareForGetAddr(std::string& s) {
    if (s[0] == '[' && s.back() == ']') {
        return s.substr(1, s.length() - 2);
    }
    return s;
}

void stringToLower(std::string& s) {
    std::transform(s.begin(), s.end(), s.begin(),
                   [](unsigned char c) { return std::tolower(c); });
}

size_t safe_stdout_write(const char* buf, size_t n) {
    size_t written = 0;
    while (written < n) {
        ssize_t res = write(STDOUT_FILENO, buf + written, n - written);
        if (res < 0) {
            if (errno == EINTR) continue;
            throw std::system_error(errno, std::generic_category(), "write to stdout");
        }
        written += res;
    }
    return written;
}