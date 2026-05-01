#pragma once

#include "protocol_probe/hardware/FtdiDevice.hpp"
#include <cstdint>
#include <vector>

namespace protocol_probe {
namespace hardware {

struct I2cPins {
    uint8_t scl = 5; // Bit index (D5 = DSR/SCL)
    uint8_t sda = 6; // Bit index (D6 = DCD/SDA)

    uint8_t sclMask() const { return 1 << scl; }
    uint8_t sdaMask() const { return 1 << sda; }
    uint8_t directionMask() const { return sclMask() | sdaMask(); }
};

class I2cInterface {
public:
    explicit I2cInterface(hardware::FtdiDevice& device);

    void begin(const I2cPins& pins = I2cPins{});
    
    void setFrequency(uint32_t hz);

    bool write(uint8_t deviceAddr, uint32_t regAddr, const std::vector<uint8_t>& data, uint8_t addrSize = 1);
    std::vector<uint8_t> read(uint8_t deviceAddr, uint32_t regAddr, size_t length, uint8_t addrSize = 1);
    bool ping(uint8_t deviceAddr);

private:
    hardware::FtdiDevice& _device;
    I2cPins _pins;
    uint8_t _currentOutput;

    void addStart(std::vector<uint8_t>& buf);
    void addStop(std::vector<uint8_t>& buf);
    void addByteWrite(std::vector<uint8_t>& buf, uint8_t byte);
    void addByteRead(std::vector<uint8_t>& buf, bool ack);

    // Send one byte and clock the ACK bit with SDA as input; returns true if device ACKed
    bool sendByteGetAck(uint8_t byte);
    // Read one byte with SDA as input, then send ACK (true) or NACK (false) to device
    uint8_t recvByteAck(bool ack);
};

} // namespace hardware
} // namespace protocol_probe
