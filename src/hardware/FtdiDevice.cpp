#include "protocol_probe/hardware/FtdiDevice.hpp"
#include "protocol_probe/Config.hpp"
#include <Poco/Logger.h>
#include <iostream>
#include <algorithm>
#include <thread>
#include <chrono>
#include <sstream>
#include <iomanip>

namespace protocol_probe {
namespace hardware {

FtdiDevice::FtdiDevice() : _ftdi(nullptr), _connected(false) {
    _ftdi = ftdi_new();
    if (!_ftdi) {
        throw std::runtime_error("Failed to allocate ftdi_context");
    }
}

FtdiDevice::~FtdiDevice() {
    close();
    if (_ftdi) {
        ftdi_free(_ftdi);
    }
}

void FtdiDevice::open() {
    open(ProbeConfig::deviceVendorId(), ProbeConfig::deviceProductId());
}

void FtdiDevice::open(int vendor, int product) {
    if (_connected) return;

    Poco::Logger::get("hardware.FtdiDevice").information("Opening FTDI device...");
    int ret = ftdi_usb_open(_ftdi, vendor, product);
    if (ret < 0) {
        throw std::runtime_error("Unable to open ftdi device: " + getLastError());
    }
    
    ftdi_set_latency_timer(_ftdi, 2);
    ftdi_usb_purge_buffers(_ftdi);
    
    _connected = true;
    Poco::Logger::get("hardware.FtdiDevice").information("FTDI device opened successfully.");
}

void FtdiDevice::close() {
    if (_connected && _ftdi) {
        Poco::Logger::get("hardware.FtdiDevice").information("Closing FTDI device...");
        ftdi_usb_close(_ftdi);
        _connected = false;
    }
}

void FtdiDevice::purgeBuffers() {
    if (_connected && _ftdi)
        ftdi_usb_purge_buffers(_ftdi);
}

void FtdiDevice::setBitMode(unsigned char mask, unsigned char mode) {
    if (!_connected) throw std::runtime_error("Device not connected");
    
    std::ostringstream oss;
    oss << "Setting bitmode: mask=0x" << std::hex << std::setw(2) << std::setfill('0') << static_cast<int>(mask) 
        << ", mode=0x" << std::hex << std::setw(2) << std::setfill('0') << static_cast<int>(mode);
    Poco::Logger::get("hardware.FtdiDevice").debug(oss.str());

    if (ftdi_set_bitmode(_ftdi, mask, mode) < 0) {
        throw std::runtime_error("Failed to set bitmode: " + getLastError());
    }
}

void FtdiDevice::setBaudRate(int baudrate) {
    if (!_connected) throw std::runtime_error("Device not connected");
    Poco::Logger::get("hardware.FtdiDevice").debug("Setting baudrate: " + std::to_string(baudrate));
    if (ftdi_set_baudrate(_ftdi, baudrate) < 0) {
        throw std::runtime_error("Failed to set baudrate: " + getLastError());
    }
}

void FtdiDevice::write(unsigned char data) {
    if (!_connected) throw std::runtime_error("Device not connected");
    
    if (ftdi_write_data(_ftdi, &data, 1) < 0) {
        throw std::runtime_error("Failed to write data: " + getLastError());
    }
}

unsigned char FtdiDevice::read() {
    if (!_connected) throw std::runtime_error("Device not connected");
    
    unsigned char pins;
    if (ftdi_read_pins(_ftdi, &pins) < 0) {
        throw std::runtime_error("Failed to read pins: " + getLastError());
    }
    return pins;
}

void FtdiDevice::transfer(const std::vector<uint8_t>& writeBuffer, std::vector<uint8_t>& readBuffer) {
    if (!_connected) throw std::runtime_error("Device not connected");

    if (writeBuffer.empty() && !readBuffer.empty()) {
        int ret = ftdi_read_data(_ftdi, readBuffer.data(), static_cast<int>(readBuffer.size()));
        if (ret < 0) throw std::runtime_error("Pure read failed: " + getLastError());
        readBuffer.resize(ret);
        return;
    }

    const int chunkSize = 256;
    int totalProcessed = 0;
    int totalToProcess = static_cast<int>(writeBuffer.size());

    while (totalProcessed < totalToProcess) {
        int toWrite = (std::min)(chunkSize, totalToProcess - totalProcessed);

        int written = ftdi_write_data(_ftdi, writeBuffer.data() + totalProcessed, toWrite);
        if (written < 0) {
            throw std::runtime_error("Buffer write failed: " + getLastError());
        }

        if (!readBuffer.empty()) {
            int totalRead = 0;
            // Use a generous time-based timeout: at slow I2C rates (6250 baud),
            // 256 bytes take ~41ms; add 2ms latency timer + margin = 500ms max.
            auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(500);
            while (totalRead < written) {
                int ret = ftdi_read_data(_ftdi,
                    readBuffer.data() + totalProcessed + totalRead,
                    written - totalRead);
                if (ret < 0) throw std::runtime_error("Buffer read failed: " + getLastError());
                if (ret > 0) {
                    totalRead += ret;
                } else {
                    if (std::chrono::steady_clock::now() > deadline) break;
                    std::this_thread::sleep_for(std::chrono::microseconds(100));
                }
            }
        }
        totalProcessed += written;
    }
}

std::string FtdiDevice::getLastError() const {
    if (!_ftdi) return "Context not initialized";
    return std::string(ftdi_get_error_string(_ftdi));
}

} // namespace hardware
} // namespace protocol_probe
