#pragma once

#include "protocol_probe/hardware/FtdiDevice.hpp"
#include <vector>
#include <cstdint>

namespace protocol_probe {
namespace hardware {

struct SpiPins {
    uint8_t mosi = 2; // Bit index (D2 = RTS/MOSI)
    uint8_t miso = 3; // Bit index (D3 = CTS/MISO)
    uint8_t sck  = 4; // Bit index (D4 = DTR/SCK)
    uint8_t cs   = 7; // Bit index (D7 = RI/NSS)

    uint8_t mosiMask() const { return 1 << mosi; }
    uint8_t misoMask() const { return 1 << miso; }
    uint8_t sckMask()  const { return 1 << sck; }
    uint8_t csMask()   const { return 1 << cs; }
    uint8_t directionMask() const { return mosiMask() | sckMask() | csMask(); }
};

class SpiInterface {
public:
    explicit SpiInterface(hardware::FtdiDevice& device);

    void begin(const SpiPins& pins = SpiPins{});
    void end();

    void setFrequency(uint32_t hz);

    uint8_t transfer(uint8_t data);
    std::vector<uint8_t> transfer(const std::vector<uint8_t>& data);

    void select();
    void deselect();

private:
    hardware::FtdiDevice& _device;
    SpiPins _pins;
    uint8_t _currentOutput;
};

} // namespace hardware
} // namespace protocol_probe
