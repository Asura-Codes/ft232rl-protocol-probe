#include "protocol_probe/hardware/UartInterface.hpp"
#include <ftdi.h>
#include <chrono>
#include <thread>
#include <algorithm>
#include <Poco/Logger.h>

namespace protocol_probe {
namespace hardware {

UartInterface::UartInterface(FtdiDevice& device) 
    : _device(device), _baudRate(9600), _sampleFreq(9600) {}

void UartInterface::begin(uint32_t baudRate, const UartPins& pins) {
    _baudRate = baudRate;
    _pins = pins;
    Poco::Logger& log = Poco::Logger::get("hardware.UartInterface");
    log.information("UART begin: baud=%u TX=D%d RX=D%d", baudRate, (int)_pins.tx, (int)_pins.rx);
    _device.setBitMode(0x00, 0x00);
    _device.purgeBuffers(); // flush stale bit-bang data from previous mode
    _device.setBaudRate(baudRate);
    log.debug("UART mode active.");
}

void UartInterface::send(const std::vector<uint8_t>& data) {
    Poco::Logger::get("hardware.UartInterface").debug("UART send: " + std::to_string(data.size()) + " byte(s).");
    std::vector<uint8_t> dummy;
    _device.transfer(data, dummy);
}

void UartInterface::send(const std::string& text) {
    Poco::Logger::get("hardware.UartInterface").debug("UART send string: \"%s\"", text);
    std::vector<uint8_t> data(text.begin(), text.end());
    send(data);
}

std::vector<uint8_t> UartInterface::receive(size_t length, int timeoutMs) {
    Poco::Logger& log = Poco::Logger::get("hardware.UartInterface");
    log.debug("UART receive: expect=" + std::to_string(length) + " bytes timeout=" + std::to_string(timeoutMs) + " ms.");
    std::vector<uint8_t> result;
    auto start = std::chrono::steady_clock::now();

    while (result.size() < length) {
        auto now = std::chrono::steady_clock::now();
        if (std::chrono::duration_cast<std::chrono::milliseconds>(now - start).count() > timeoutMs) {
            log.debug("UART receive timeout after " + std::to_string(timeoutMs) + " ms ("
                + std::to_string(result.size()) + "/" + std::to_string(length) + " bytes).");
            break;
        }

        std::vector<uint8_t> readBuf(512);
        try {
            _device.transfer({}, readBuf);
            if (!readBuf.empty()) {
                for (uint8_t b : readBuf) {
                    if (result.size() < length) result.push_back(b);
                }
            } else {
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
            }
        } catch (...) {
            log.warning("UART receive: transfer exception, aborting.");
            break;
        }
    }
    log.debug("UART receive complete: " + std::to_string(result.size()) + " byte(s) received.");
    return result;
}

std::string UartInterface::receiveString(size_t maxLength, int timeoutMs) {
    auto data = receive(maxLength, timeoutMs);
    return std::string(data.begin(), data.end());
}

std::vector<uint8_t> UartInterface::exchange(const std::vector<uint8_t>& data, size_t responseLength, int waitMs) {
    Poco::Logger::get("hardware.UartInterface").debug(
        "UART exchange: send " + std::to_string(data.size()) + " bytes, expect "
        + std::to_string(responseLength) + ", wait " + std::to_string(waitMs) + " ms.");
    send(data);
    if (waitMs > 0) std::this_thread::sleep_for(std::chrono::milliseconds(waitMs));
    return receive(responseLength);
}

} // namespace hardware
} // namespace protocol_probe
