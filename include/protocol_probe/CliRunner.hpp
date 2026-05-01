#pragma once

#include <cstdint>
#include <cstddef>
#include <functional>
#include <string>

#include "protocol_probe/hardware/UartInterface.hpp"
#include "protocol_probe/hardware/SpiInterface.hpp"
#include "protocol_probe/hardware/I2cInterface.hpp"

namespace protocol_probe {

class CliRunner {
public:
    CliRunner(const std::string& cmd,
              const std::string& payload,
              int duration,
              uint32_t baud,
              uint32_t address,
              size_t length,
              uint8_t slaveId,
              uint16_t regCount,
              uint8_t pinTx,
              uint8_t pinRx,
              uint8_t pinMosi,
              uint8_t pinMiso,
              uint8_t pinSck,
              uint8_t pinCs,
              uint8_t pinScl,
              uint8_t pinSda,
              const std::function<void()>& displayHelp);

    int run();

private:
    void displayHelp() const;
    hardware::UartPins uartPins() const;
    hardware::SpiPins spiPins() const;
    hardware::I2cPins i2cPins() const;

private:
    const std::string& _cmd;
    const std::string& _payload;
    const int& _duration;
    const uint32_t& _baud;
    const uint32_t& _address;
    const size_t& _length;
    const uint8_t& _slaveId;
    const uint16_t& _regCount;
    const uint8_t& _pinTx;
    const uint8_t& _pinRx;
    const uint8_t& _pinMosi;
    const uint8_t& _pinMiso;
    const uint8_t& _pinSck;
    const uint8_t& _pinCs;
    const uint8_t& _pinScl;
    const uint8_t& _pinSda;
    const std::function<void()>& _displayHelp;
};

} // namespace protocol_probe
