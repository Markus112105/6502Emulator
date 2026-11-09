#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

#include "bus.hpp"
#include "cpu.hpp"

namespace {

struct Options {
    std::filesystem::path program_path;
    uint16_t start_address = 0x8000;
    uint64_t max_cycles = 1'000'000;
    bool trace = false;
};

void print_usage(const char* exe) {
    std::cerr << "Usage: " << exe << " <program.bin> [--start 0x8000] [--max-cycles 1000000] [--trace]\n";
}

std::optional<uint64_t> parse_number(const std::string& token) {
    std::size_t consumed = 0;
    uint64_t value = 0;
    try {
        if (token.rfind("0x", 0) == 0 || token.rfind("0X", 0) == 0) {
            value = std::stoull(token, &consumed, 16);
        } else {
            value = std::stoull(token, &consumed, 10);
        }
    } catch (...) {
        return std::nullopt;
    }

    if (consumed != token.size()) {
        return std::nullopt;
    }
    return value;
}

bool parse_arguments(int argc, char* argv[], Options& options) {
    for (int index = 1; index < argc; ++index) {
        const std::string arg = argv[index];
        if (arg == "--start") {
            if (index + 1 >= argc) {
                std::cerr << "--start requires an address" << std::endl;
                return false;
            }
            const auto value = parse_number(argv[++index]);
            if (!value || *value > 0xFFFF) {
                std::cerr << "Invalid start address: " << argv[index] << std::endl;
                return false;
            }
            options.start_address = static_cast<uint16_t>(*value);
        } else if (arg == "--max-cycles") {
            if (index + 1 >= argc) {
                std::cerr << "--max-cycles requires a value" << std::endl;
                return false;
            }
            const auto value = parse_number(argv[++index]);
            if (!value) {
                std::cerr << "Invalid cycle budget: " << argv[index] << std::endl;
                return false;
            }
            options.max_cycles = *value;
        } else if (arg == "--trace") {
            options.trace = true;
        } else if (!arg.empty() && arg.front() == '-') {
            std::cerr << "Unknown option: " << arg << std::endl;
            return false;
        } else if (options.program_path.empty()) {
            options.program_path = arg;
        } else {
            std::cerr << "Unexpected argument: " << arg << std::endl;
            return false;
        }
    }

    if (options.program_path.empty()) {
        std::cerr << "No program specified." << std::endl;
        return false;
    }

    return true;
}

std::vector<uint8_t> load_program(const std::filesystem::path& path) {
    std::ifstream file(path, std::ios::binary | std::ios::ate);
    if (!file) {
        throw std::runtime_error("Failed to open program: " + path.string());
    }

    const std::streamsize size = file.tellg();
    file.seekg(0, std::ios::beg);
    std::vector<uint8_t> buffer(static_cast<std::size_t>(size));
    if (!file.read(reinterpret_cast<char*>(buffer.data()), size)) {
        throw std::runtime_error("Failed to read program: " + path.string());
    }
    return buffer;
}

std::string format_hex(uint16_t value) {
    std::ostringstream stream;
    stream << std::hex << std::uppercase << std::setfill('0') << std::setw(4) << value;
    return stream.str();
}

}  // namespace

int main(int argc, char* argv[]) {
    using namespace emulator;

    Options options;
    if (!parse_arguments(argc, argv, options)) {
        print_usage(argv[0]);
        return 1;
    }

    std::vector<uint8_t> program;
    try {
        program = load_program(options.program_path);
    } catch (const std::exception& error) {
        std::cerr << error.what() << std::endl;
        return 1;
    }

    Bus bus;
    CPU cpu;

    cpu.attach_bus(&bus);
    bus.attach_cpu(&cpu);
    bus.load_program(options.start_address, program);
    cpu.reset();

    std::cout << "Loaded program: " << options.program_path << std::endl;
    std::cout << "Start address: $" << format_hex(options.start_address)
              << ", cycle budget: " << options.max_cycles << std::endl;

    const uint64_t start_cycles = cpu.cycles();
    uint64_t executed_cycles = 0;

    while (executed_cycles < options.max_cycles) {
        const uint16_t pc = cpu.registers().program_counter;
        const uint8_t opcode = bus.read(pc);

        if (options.trace) {
            std::cout << "[PC=$" << format_hex(pc) << "] opcode $" << std::hex
                      << std::uppercase << std::setw(2) << std::setfill('0')
                      << static_cast<int>(opcode) << std::dec << std::setfill(' ') << std::endl;
        }

        try {
            cpu.step();
        } catch (const std::exception& error) {
            std::cerr << "Execution aborted: " << error.what() << std::endl;
            return 1;
        }

        executed_cycles = cpu.cycles() - start_cycles;
        if (opcode == 0x00) {
            std::cout << "BRK encountered at $" << format_hex(pc)
                      << " after " << executed_cycles << " cycles." << std::endl;
            return 0;
        }
    }

    std::cerr << "Execution stopped after reaching " << options.max_cycles
              << " cycles without encountering BRK." << std::endl;
    return 2;
}
