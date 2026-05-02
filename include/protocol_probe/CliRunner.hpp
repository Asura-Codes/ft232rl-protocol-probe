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
    std::string _cmd;
    std::string _payload;
    int _duration;
    uint32_t _baud;
    uint32_t _address;
    size_t _length;
    uint8_t _slaveId;
    uint16_t _regCount;
    uint8_t _pinTx;
    uint8_t _pinRx;
    uint8_t _pinMosi;
    uint8_t _pinMiso;
    uint8_t _pinSck;
    uint8_t _pinCs;
    uint8_t _pinScl;
    uint8_t _pinSda;
    std::function<void()> _displayHelp;
};

} // namespace protocol_probe
