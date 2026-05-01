#pragma once

#include <vector>
#include <cstdint>
#include <string>

namespace protocol_probe {
namespace hardware {

class IFlashDriver {
public:
    virtual ~IFlashDriver() = default;

    virtual std::vector<uint8_t> read(uint32_t address, size_t length) = 0;
    virtual void write(uint32_t address, const std::vector<uint8_t>& data) = 0;
    virtual void erase(uint32_t address, size_t length) = 0;
    
    virtual std::string getDeviceName() const = 0;
    virtual size_t getCapacityBytes() const = 0;
    
    virtual bool isProtected() = 0;
    virtual void unlock() = 0;
};

} // namespace hardware
} // namespace protocol_probe
