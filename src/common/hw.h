#pragma once

#include <cstdint>
#include <optional>
#include <string>

namespace hw {

constexpr uint32_t kPower = 0x00050031;
constexpr uint32_t kLight = 0x00050032;

bool writeFile(const std::string& path, const std::string& value);
std::optional<std::string> debugfsDir();

class Wmi {
public:
    explicit Wmi(std::string dir) : dir_(std::move(dir)) {}
    std::optional<uint32_t> get(uint32_t id);
    bool set(uint32_t id, uint32_t value);

private:
    std::string dir_;
};

bool isOn(Wmi& w);
std::optional<int> brightness(Wmi& w);
bool setBrightness(Wmi& w, int value);
bool setPower(Wmi& w, bool on);

}
