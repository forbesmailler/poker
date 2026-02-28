#pragma once

#include <string>
#include <vector>
#include <cstdint>
#include <fstream>
#include <iostream>
#include <chrono>

namespace poker {

// Simple timer for profiling
class Timer {
public:
    Timer() : start_(std::chrono::high_resolution_clock::now()) {}

    void reset() { start_ = std::chrono::high_resolution_clock::now(); }

    double elapsed_seconds() const {
        auto now = std::chrono::high_resolution_clock::now();
        return std::chrono::duration<double>(now - start_).count();
    }

    double elapsed_ms() const { return elapsed_seconds() * 1000.0; }

private:
    std::chrono::high_resolution_clock::time_point start_;
};

// Logging
inline void log_info(const std::string& msg) {
    std::cout << "[INFO] " << msg << "\n";
}

inline void log_error(const std::string& msg) {
    std::cerr << "[ERROR] " << msg << "\n";
}

// Binary I/O helpers
template <typename T>
void write_binary(std::ofstream& out, const T& val) {
    out.write(reinterpret_cast<const char*>(&val), sizeof(T));
}

template <typename T>
void read_binary(std::ifstream& in, T& val) {
    in.read(reinterpret_cast<char*>(&val), sizeof(T));
}

template <typename T>
void write_vector_binary(std::ofstream& out, const std::vector<T>& vec) {
    uint64_t size = vec.size();
    write_binary(out, size);
    if (size > 0) {
        out.write(reinterpret_cast<const char*>(vec.data()),
                  static_cast<std::streamsize>(size * sizeof(T)));
    }
}

template <typename T>
void read_vector_binary(std::ifstream& in, std::vector<T>& vec) {
    uint64_t size;
    read_binary(in, size);
    vec.resize(static_cast<size_t>(size));
    if (size > 0) {
        in.read(reinterpret_cast<char*>(vec.data()),
                static_cast<std::streamsize>(size * sizeof(T)));
    }
}

} // namespace poker
