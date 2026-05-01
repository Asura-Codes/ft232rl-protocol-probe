#pragma once

#include "protocol_probe/hardware/FtdiDevice.hpp"
#include <vector>
#include <string>
#include <cstdint>

namespace protocol_probe {
namespace hardware {

struct UartPins {
    uint8_t tx = 0; // Bit index
    uint8_t rx = 1; // Bit index

    uint8_t txMask() const { return 1 << tx; }
    uint8_t rxMask() const { return 1 << rx; }
    uint8_t directionMask() const { return txMask(); }
};

class UartInterface {
public:
    explicit UartInterface(hardware::FtdiDevice& device);

    void begin(uint32_t baudRate, const UartPins& pins = UartPins{});
    
    // Standard communication
    void send(const std::vector<uint8_t>& data);
    void send(const std::string& text);
    
    std::vector<uint8_t> receive(size_t length, int timeoutMs = 1000);
    std::string receiveString(size_t maxLength, int timeoutMs = 1000);

    // High-level exchange
    std::vector<uint8_t> exchange(const std::vector<uint8_t>& data, size_t responseLength, int waitMs = 100);

private:
    hardware::FtdiDevice& _device;
    UartPins _pins;
    uint32_t _baudRate;
    uint32_t _sampleFreq;
};

} // namespace hardware
} // namespace protocol_probe
