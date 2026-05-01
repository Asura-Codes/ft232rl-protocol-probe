#include "protocol_probe/hardware/SpiInterface.hpp"
#include <Poco/Logger.h>
#include <ftdi.h>
#include <algorithm>
#include <sstream>
#include <iomanip>

namespace protocol_probe {
namespace hardware {

SpiInterface::SpiInterface(FtdiDevice& device) 
    : _device(device), _currentOutput(0) {}

void SpiInterface::begin(const SpiPins& pins) {
    _pins = pins;
    _currentOutput = _pins.csMask(); // CS high by default
    Poco::Logger& log = Poco::Logger::get("hardware.SpiInterface");
    log.information("SPI begin: MOSI=D%d MISO=D%d SCK=D%d CS=D%d",
        (int)_pins.mosi, (int)_pins.miso, (int)_pins.sck, (int)_pins.cs);
    _device.setBitMode(_pins.directionMask(), BITMODE_SYNCBB);
    setFrequency(100000);
    deselect();
    log.debug("SPI ready (default 100 kHz).");
}

void SpiInterface::end() {
    Poco::Logger::get("hardware.SpiInterface").debug("SPI end: resetting bitmode.");
    _device.setBitMode(0x00, BITMODE_RESET);
}

void SpiInterface::setFrequency(uint32_t hz) {
    int baudrate = hz / 16;
    if (baudrate < 183) baudrate = 183;
    Poco::Logger::get("hardware.SpiInterface").debug("SPI setFrequency: %u Hz -> baudrate=%d", hz, baudrate);
    _device.setBaudRate(baudrate);
}

void SpiInterface::select() {
    Poco::Logger::get("hardware.SpiInterface").trace("SPI CS assert (low).");
    _currentOutput &= ~_pins.csMask();
    // Use transfer() to consume the echo byte that SYNCBB generates per written byte
    std::vector<uint8_t> echo(1);
    _device.transfer({_currentOutput}, echo);
}

void SpiInterface::deselect() {
    Poco::Logger::get("hardware.SpiInterface").trace("SPI CS deassert (high).");
    _currentOutput |= _pins.csMask();
    std::vector<uint8_t> echo(1);
    _device.transfer({_currentOutput}, echo);
}

uint8_t SpiInterface::transfer(uint8_t data) {
    std::vector<uint8_t> writeBuf;
    writeBuf.reserve(16);

    uint8_t mosiMask = _pins.mosiMask();
    uint8_t sckMask = _pins.sckMask();
    uint8_t misoMask = _pins.misoMask();

    for (int i = 7; i >= 0; --i) {
        uint8_t mosi = ((data >> i) & 0x01) ? mosiMask : 0;
        writeBuf.push_back(_currentOutput | mosi);
        writeBuf.push_back(_currentOutput | mosi | sckMask);
    }

    std::vector<uint8_t> readBuf(writeBuf.size());
    _device.transfer(writeBuf, readBuf);

    uint8_t result = 0;
    for (int i = 0; i < 8; ++i) {
        if (readBuf[i * 2 + 1] & misoMask) {
            result |= (1 << (7 - i));
        }
    }

    return result;
}

std::vector<uint8_t> SpiInterface::transfer(const std::vector<uint8_t>& data) {
    std::vector<uint8_t> writeBuf;
    writeBuf.reserve(data.size() * 16);

    uint8_t mosiMask = _pins.mosiMask();
    uint8_t sckMask = _pins.sckMask();
    uint8_t misoMask = _pins.misoMask();

    for (uint8_t byte : data) {
        for (int i = 7; i >= 0; --i) {
            uint8_t mosi = ((byte >> i) & 0x01) ? mosiMask : 0;
            writeBuf.push_back(_currentOutput | mosi);
            writeBuf.push_back(_currentOutput | mosi | sckMask);
        }
    }

    std::vector<uint8_t> readBuf(writeBuf.size());
    _device.transfer(writeBuf, readBuf);

    std::vector<uint8_t> result;
    result.reserve(data.size());
    for (size_t b = 0; b < data.size(); ++b) {
        uint8_t outByte = 0;
        for (int i = 0; i < 8; ++i) {
            if (readBuf[b * 16 + i * 2 + 1] & misoMask) {
                outByte |= (1 << (7 - i));
            }
        }
        result.push_back(outByte);
    }
    return result;
}

} // namespace hardware
} // namespace protocol_probe
