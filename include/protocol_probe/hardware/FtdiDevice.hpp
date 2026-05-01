#pragma once

#ifdef _WIN32
#include <winsock2.h>
#endif
#include <ftdi.h>
#include <string>
#include <vector>
#include <memory>
#include <stdexcept>

namespace protocol_probe {
namespace hardware {

class FtdiDevice {
public:
    FtdiDevice();
    ~FtdiDevice();

    FtdiDevice(const FtdiDevice&) = delete;
    FtdiDevice& operator=(const FtdiDevice&) = delete;

    void open();
    void open(int vendor, int product);
    void close();
    void setBitMode(unsigned char mask, unsigned char mode);
    void setBaudRate(int baudrate);
    
    void write(unsigned char data);
    unsigned char read();

    void transfer(const std::vector<uint8_t>& writeBuffer, std::vector<uint8_t>& readBuffer);

    /// Flush the FT232RL USB RX and TX FIFOs.  Call after switching between
    /// bit-bang and UART modes to discard any stale bytes.
    void purgeBuffers();

    bool isConnected() const { return _connected; }
    std::string getLastError() const;

private:
    struct ftdi_context* _ftdi;
    bool _connected;
};

} // namespace hardware
} // namespace protocol_probe
