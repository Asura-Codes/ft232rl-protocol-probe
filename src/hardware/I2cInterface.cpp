#include "protocol_probe/hardware/I2cInterface.hpp"
#include <Poco/Logger.h>
#include <ftdi.h>
#include <sstream>
#include <iomanip>

namespace protocol_probe {
namespace hardware {

I2cInterface::I2cInterface(FtdiDevice& device) 
    : _device(device), _currentOutput(0) {}

void I2cInterface::begin(const I2cPins& pins) {
    _pins = pins;
    _currentOutput = _pins.sclMask(); // SDA is open-drain (input), only SCL output
    Poco::Logger& log = Poco::Logger::get("hardware.I2cInterface");
    log.information("I2C begin: SCL=D%d SDA=D%d", (int)_pins.scl, (int)_pins.sda);
    // Set ONLY SCL as output; SDA is input (relies on PCB pull-up)
    _device.setBitMode(_pins.sclMask(), BITMODE_SYNCBB);
    setFrequency(100000);
    log.debug("I2C ready (default 100 kHz, SDA open-drain via input).");
}

void I2cInterface::setFrequency(uint32_t hz) {
    int baudrate = hz / 16;
    if (baudrate < 183) baudrate = 183;
    Poco::Logger::get("hardware.I2cInterface").debug("I2C setFrequency: %u Hz -> baudrate=%d", hz, baudrate);
    _device.setBaudRate(baudrate);
}

void I2cInterface::addStart(std::vector<uint8_t>& buf) {
    // START: SDA goes low while SCL is high
    // We use SCL output only; SDA is controlled by direction register:
    // To drive SDA low: add sdaMask to the _direction_ not to the output state
    // Since we can't change direction per-byte in one transfer, we simulate by:
    // Writing the bit-bang output byte. SDA is INPUT (high from pull-up).
    // To pull SDA LOW, we need to make it output-LOW momentarily.
    // For START: SDA must go from HIGH (pull-up) to LOW while SCL is HIGH.
    // We'll approximate: SCL=1,SDA=1 -> SCL=1,SDA=0 -> SCL=0,SDA=0
    // With SDA as input, write 0 to output doesn't drive anything.
    // HACK: Set directionMask to include SDA to pull it low, then release.
    // This requires setBitMode calls which reset the chip.
    // For now, build the buf assuming SDA controlled via output bit
    // (This works only if SDA is made output in begin() – see alternate approach below)
    uint8_t sclMask = _pins.sclMask();
    uint8_t sdaMask = _pins.sdaMask();
    buf.push_back(sclMask | sdaMask); // SCL=1, SDA=1 (idle)
    buf.push_back(sclMask);           // SCL=1, SDA=0 (START)
    buf.push_back(0x00);              // SCL=0, SDA=0
}

void I2cInterface::addStop(std::vector<uint8_t>& buf) {
    // STOP: SDA goes high while SCL is high
    uint8_t sclMask = _pins.sclMask();
    uint8_t sdaMask = _pins.sdaMask();
    buf.push_back(0x00);              // SCL=0, SDA=0
    buf.push_back(sclMask);           // SCL=1, SDA=0
    buf.push_back(sclMask | sdaMask); // SCL=1, SDA=1 (STOP)
}

void I2cInterface::addByteWrite(std::vector<uint8_t>& buf, uint8_t byte) {
    uint8_t sclMask = _pins.sclMask();
    uint8_t sdaMask = _pins.sdaMask();
    for (int i = 7; i >= 0; --i) {
        uint8_t sda = (byte >> i) & 0x01 ? sdaMask : 0;
        buf.push_back(sda);
        buf.push_back(sda | sclMask);
        buf.push_back(sda);
    }
    // ACK slot: SDA released (driven high per output direction mask)
    // Caller must switch SDA to input before clocking this
    buf.push_back(sdaMask);
    buf.push_back(sdaMask | sclMask);
    buf.push_back(sdaMask);
}

// Send one byte and read back ACK.
// We switch SDA to input for the ACK bit, then back to output.
bool I2cInterface::sendByteGetAck(uint8_t byte) {
    uint8_t sclMask = _pins.sclMask();
    uint8_t sdaMask = _pins.sdaMask();
    uint8_t outMask = sclMask | sdaMask;

    // NOTE: caller must have already set bitmode to outMask before the first call.
    // The mode-switch at the top was removed because ftdi_set_bitmode resets the
    // chip state mid-transaction, glitching SCL and causing NACK.

    // Send 8 data bits
    std::vector<uint8_t> txBuf;
    txBuf.reserve(24);
    for (int i = 7; i >= 0; --i) {
        uint8_t sda = (byte >> i) & 0x01 ? sdaMask : 0;
        txBuf.push_back(sda);
        txBuf.push_back(sda | sclMask);
        txBuf.push_back(sda);
    }
    std::vector<uint8_t> rxBuf(24);
    _device.transfer(txBuf, rxBuf);

    // Switch SDA to input for ACK bit
    _device.setBitMode(sclMask, BITMODE_SYNCBB); // Only SCL is output
    
    // 4-sample window: ackRx[2] reflects state when SCL=HIGH (1-sample SYNCBB lag)
    std::vector<uint8_t> ackTx = {0x00, sclMask, 0x00, 0x00};
    std::vector<uint8_t> ackRx(4);
    _device.transfer(ackTx, ackRx);
    
    bool acked = !(ackRx[2] & sdaMask); // sample after SCL-HIGH edge

    // Switch SDA back to output
    _device.setBitMode(outMask, BITMODE_SYNCBB);

    return acked;
}

// Read one byte (SDA input), then send ACK or NACK
uint8_t I2cInterface::recvByteAck(bool ackToDevice) {
    uint8_t sclMask = _pins.sclMask();
    uint8_t sdaMask = _pins.sdaMask();
    uint8_t outMask = sclMask | sdaMask;

    // Switch SDA to input for 8 data bits
    _device.setBitMode(sclMask, BITMODE_SYNCBB);

    std::vector<uint8_t> txBuf(32, 0x00);
    for (int i = 0; i < 8; ++i) {
        txBuf[i * 4 + 0] = 0x00;
        txBuf[i * 4 + 1] = sclMask;
        txBuf[i * 4 + 2] = 0x00;
        txBuf[i * 4 + 3] = 0x00;  // extra sample for 1-sample lag
    }
    std::vector<uint8_t> rxBuf(32);
    _device.transfer(txBuf, rxBuf);

    uint8_t val = 0;
    for (int i = 0; i < 8; ++i) {
        // rxBuf[i*4+2] reflects state when SCL was HIGH (1-sample pipeline lag)
        if (rxBuf[i * 4 + 2] & sdaMask) {
            val |= (1 << (7 - i));
        }
    }

    // Switch back to output and send ACK (drive SDA low) or NACK (SDA high)
    _device.setBitMode(outMask, BITMODE_SYNCBB);
    uint8_t sda = ackToDevice ? 0 : sdaMask;
    std::vector<uint8_t> nackTx = {sda, (uint8_t)(sda | sclMask), sda};
    std::vector<uint8_t> nackRx(3);
    _device.transfer(nackTx, nackRx);

    return val;
}

void I2cInterface::addByteRead(std::vector<uint8_t>& buf, bool ack) {
    uint8_t sclMask = _pins.sclMask();
    uint8_t sdaMask = _pins.sdaMask();
    for (int i = 0; i < 8; ++i) {
        buf.push_back(sdaMask);
        buf.push_back(sdaMask | sclMask);
        buf.push_back(sdaMask);
    }
    uint8_t sda = ack ? 0 : sdaMask;
    buf.push_back(sda);
    buf.push_back(sda | sclMask);
    buf.push_back(sda);
}

bool I2cInterface::ping(uint8_t deviceAddr) {
    std::ostringstream addrMsg;
    addrMsg << "I2C ping: addr=0x" << std::hex << std::uppercase << std::setw(2) << std::setfill('0') << (int)deviceAddr;
    Poco::Logger::get("hardware.I2cInterface").debug(addrMsg.str());

    // START + address byte: both SCL and SDA as outputs
    _device.setBitMode(_pins.sclMask() | _pins.sdaMask(), BITMODE_SYNCBB);
    std::vector<uint8_t> txBuf;
    addStart(txBuf);
    // Add 8 bits of address (write direction = bit0=0)
    uint8_t addrByte = deviceAddr << 1;
    uint8_t sclMask = _pins.sclMask();
    uint8_t sdaMask = _pins.sdaMask();
    for (int i = 7; i >= 0; --i) {
        uint8_t sda = (addrByte >> i) & 0x01 ? sdaMask : 0;
        txBuf.push_back(sda);
        txBuf.push_back(sda | sclMask);
        txBuf.push_back(sda);
    }
    std::vector<uint8_t> rxBuf(txBuf.size());
    _device.transfer(txBuf, rxBuf);

    // ACK clock with SDA as input
    _device.setBitMode(sclMask, BITMODE_SYNCBB);
    // 4-sample window: due to 1-sample SYNCBB pipeline lag, sample[N] reflects
    // state after byte N-1. ackRx[2] captures pin state when SCL was HIGH (byte[1]).
    std::vector<uint8_t> ackTx = {0x00, sclMask, 0x00, 0x00};
    std::vector<uint8_t> ackRx(4);
    _device.transfer(ackTx, ackRx);
    bool acked = !(ackRx[2] & sdaMask); // SDA LOW = device ACK
    { std::ostringstream d; d<<"I2C ping ACK raw[1]=0x"<<std::hex<<(int)ackRx[1]<<" raw[2]=0x"<<(int)ackRx[2]<<" sda=0x"<<(int)sdaMask<<" acked="<<acked; Poco::Logger::get("hardware.I2cInterface").trace(d.str()); }
    _device.setBitMode(sclMask | sdaMask, BITMODE_SYNCBB);
    std::vector<uint8_t> stopBuf;
    addStop(stopBuf);
    std::vector<uint8_t> stopRx(stopBuf.size());
    _device.transfer(stopBuf, stopRx);

    std::ostringstream resultMsg;
    resultMsg << "I2C ping 0x" << std::hex << std::uppercase << std::setw(2) << std::setfill('0') << (int)deviceAddr
              << " -> " << (acked ? "ACK" : "NACK");
    Poco::Logger::get("hardware.I2cInterface").debug(resultMsg.str());
    return acked;
}

bool I2cInterface::write(uint8_t deviceAddr, uint32_t regAddr, const std::vector<uint8_t>& data, uint8_t addrSize) {
    std::ostringstream msg;
    msg << "I2C write: dev=0x" << std::hex << std::uppercase << std::setw(2) << std::setfill('0') << (int)deviceAddr
        << " reg=0x" << std::setw(4) << std::setfill('0') << regAddr
        << " len=" << std::dec << data.size() << " addrSize=" << (int)addrSize;
    Poco::Logger::get("hardware.I2cInterface").debug(msg.str());

    // Both outputs for START and data
    _device.setBitMode(_pins.sclMask() | _pins.sdaMask(), BITMODE_SYNCBB);
    std::vector<uint8_t> startBuf;
    addStart(startBuf);
    std::vector<uint8_t> startRx(startBuf.size());
    _device.transfer(startBuf, startRx);

    // Device address (write)
    if (!sendByteGetAck(deviceAddr << 1)) {
        std::vector<uint8_t> stop; addStop(stop);
        std::vector<uint8_t> stopRx(stop.size());
        _device.transfer(stop, stopRx);
        return false;
    }

    // Register address
    if (addrSize == 2 && !sendByteGetAck((uint8_t)((regAddr >> 8) & 0xFF))) {
        std::vector<uint8_t> stop; addStop(stop);
        std::vector<uint8_t> stopRx(stop.size());
        _device.transfer(stop, stopRx);
        return false;
    }
    if (!sendByteGetAck((uint8_t)(regAddr & 0xFF))) {
        std::vector<uint8_t> stop; addStop(stop);
        std::vector<uint8_t> stopRx(stop.size());
        _device.transfer(stop, stopRx);
        return false;
    }

    // Data bytes
    for (uint8_t b : data) {
        if (!sendByteGetAck(b)) {
            std::vector<uint8_t> stop; addStop(stop);
            std::vector<uint8_t> stopRx(stop.size());
            _device.transfer(stop, stopRx);
            return false;
        }
    }

    // STOP
    std::vector<uint8_t> stopBuf;
    addStop(stopBuf);
    std::vector<uint8_t> stopRx(stopBuf.size());
    _device.transfer(stopBuf, stopRx);
    return true;
}

std::vector<uint8_t> I2cInterface::read(uint8_t deviceAddr, uint32_t regAddr, size_t length, uint8_t addrSize) {
    std::ostringstream msg;
    msg << "I2C read: dev=0x" << std::hex << std::uppercase << std::setw(2) << std::setfill('0') << (int)deviceAddr
        << " reg=0x" << std::setw(4) << std::setfill('0') << regAddr
        << " len=" << std::dec << length << " addrSize=" << (int)addrSize;
    Poco::Logger::get("hardware.I2cInterface").debug(msg.str());

    // Both outputs for START and data
    _device.setBitMode(_pins.sclMask() | _pins.sdaMask(), BITMODE_SYNCBB);
    std::vector<uint8_t> startBuf;
    addStart(startBuf);
    std::vector<uint8_t> rx(startBuf.size());
    _device.transfer(startBuf, rx);

    // Device address (write for register address phase)
    auto sendStop = [&]() {
        std::vector<uint8_t> s; addStop(s);
        std::vector<uint8_t> sr(s.size());
        _device.transfer(s, sr);
    };
    if (!sendByteGetAck(deviceAddr << 1)) { sendStop(); return {}; }
    if (addrSize == 2 && !sendByteGetAck((uint8_t)((regAddr >> 8) & 0xFF))) { sendStop(); return {}; }
    if (!sendByteGetAck((uint8_t)(regAddr & 0xFF))) { sendStop(); return {}; }

    // Repeated START
    std::vector<uint8_t> rstart;
    addStart(rstart);
    std::vector<uint8_t> rstartRx(rstart.size());
    _device.transfer(rstart, rstartRx);

    // Device address (read)
    if (!sendByteGetAck((deviceAddr << 1) | 1)) { sendStop(); return {}; }

    // Read bytes
    std::vector<uint8_t> result;
    result.reserve(length);
    for (size_t i = 0; i < length; ++i) {
        result.push_back(recvByteAck(i < length - 1));
    }

    // STOP
    std::vector<uint8_t> stopBuf;
    addStop(stopBuf);
    std::vector<uint8_t> stopRx(stopBuf.size());
    _device.transfer(stopBuf, stopRx);

    return result;
}

} // namespace hardware
} // namespace protocol_probe
