#include "hw.h"

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <thread>

namespace hw {

namespace {
const char* const kDirs[] = {"/sys/kernel/debug/asus-nb-wmi", "/sys/kernel/debug/asus-wmi"};

std::string hex(uint32_t v) {
    char b[16];
    std::snprintf(b, sizeof b, "0x%08x", v);
    return b;
}

std::optional<uint32_t> parseResult(const std::string& line) {
    auto p = line.rfind("= 0x");
    if (p == std::string::npos) return std::nullopt;
    return static_cast<uint32_t>(std::strtoul(line.c_str() + p + 2, nullptr, 16));
}
}

bool writeFile(const std::string& path, const std::string& value) {
    std::ofstream f(path);
    f << value << std::endl;
    return f.good();
}

std::optional<std::string> debugfsDir() {
    for (const char* d : kDirs)
        if (std::ifstream(std::string(d) + "/dsts").good()) return d;
    return std::nullopt;
}

std::optional<uint32_t> Wmi::get(uint32_t id) {
    if (!writeFile(dir_ + "/dev_id", hex(id))) return std::nullopt;
    std::ifstream f(dir_ + "/dsts");
    std::string line;
    if (!std::getline(f, line)) return std::nullopt;
    return parseResult(line);
}

bool Wmi::set(uint32_t id, uint32_t value) {
    if (!writeFile(dir_ + "/dev_id", hex(id))) return false;
    if (!writeFile(dir_ + "/ctrl_param", hex(value))) return false;
    std::ifstream f(dir_ + "/devs");
    std::string line;
    return static_cast<bool>(std::getline(f, line));
}

bool isOn(Wmi& w) {
    auto s = w.get(kPower);
    return s && (*s & 0xffff) != 0;
}

std::optional<int> brightness(Wmi& w) {
    auto b = w.get(kLight);
    if (!b) return std::nullopt;
    return static_cast<int>(*b & 0xff);
}

bool setBrightness(Wmi& w, int value) {
    if (value < 1 || value > 255) return false;
    return w.set(kLight, static_cast<uint32_t>(value));
}

bool setPower(Wmi& w, bool on) {
    if (!w.set(kPower, on ? 1 : 0)) return false;
    std::this_thread::sleep_for(std::chrono::milliseconds(800));
    return true;
}

}
