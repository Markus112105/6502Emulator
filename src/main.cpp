#include <cstdint>
#include <cstdio>
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

constexpr uint8_t kBrkOpcode = 0x00;

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

// One line per executed instruction: where it was, what it was, and the
// register values it left behind.
void print_trace_line(uint16_t pc, const char* mnemonic, const emulator::CPU& cpu) {
    const auto& registers = cpu.registers();
    std::printf("$%04X  %s  A=%02X X=%02X Y=%02X SP=%02X P=%02X  cycles=%llu\n",
                static_cast<unsigned>(pc), mnemonic,
                static_cast<unsigned>(registers.accumulator), static_cast<unsigned>(registers.x),
                static_cast<unsigned>(registers.y), static_cast<unsigned>(registers.stack_pointer),
                static_cast<unsigned>(registers.status), static_cast<unsigned long long>(cpu.cycles()));
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
    CPU cpu(bus);
    bus.load_program(options.start_address, program);
    cpu.reset();

    std::cout << "Loaded program: " << options.program_path << std::endl;
    std::cout << "Start address: $" << format_hex(options.start_address)
              << ", cycle budget: " << options.max_cycles << std::endl;

    while (cpu.cycles() < options.max_cycles) {
        const uint16_t pc = cpu.registers().program_counter;
        const uint8_t opcode = bus.read(pc);

        try {
            cpu.step();
        } catch (const std::exception& error) {
            std::cerr << "Execution aborted at $" << format_hex(pc) << ": " << error.what() << std::endl;
            return 1;
        }

        if (options.trace) {
            print_trace_line(pc, cpu.mnemonic(opcode), cpu);
        }

        if (opcode == kBrkOpcode) {
            std::cout << "BRK encountered at $" << format_hex(pc)
                      << " after " << cpu.cycles() << " cycles." << std::endl;
            return 0;
        }
    }

    std::cerr << "Execution stopped after reaching " << options.max_cycles
              << " cycles without encountering BRK." << std::endl;
    return 2;
}
