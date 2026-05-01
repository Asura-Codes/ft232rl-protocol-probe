#include "protocol_probe/CliRunner.hpp"

#include <Poco/Logger.h>
#include <Poco/Util/Application.h>

#include "protocol_probe/Config.hpp"
#include "protocol_probe/hardware/FtdiDevice.hpp"
#include "protocol_probe/exploits/ProtocolDiscovery.hpp"
#include "protocol_probe/exploits/SpiFlash.hpp"
#include "protocol_probe/exploits/I2cScanner.hpp"
#include "protocol_probe/exploits/UartScanner.hpp"
#include "protocol_probe/exploits/GpioProbe.hpp"
#include "protocol_probe/exploits/ModbusExploit.hpp"
#include "protocol_probe/exploits/MavLinkExploit.hpp"
#include "protocol_probe/exploits/NmeaExploit.hpp"
#include "protocol_probe/exploits/I2cEeprom.hpp"
#include "protocol_probe/exploits/SmBusExploit.hpp"
#include "protocol_probe/exploits/PmBusExploit.hpp"
#include "protocol_probe/exploits/SdCardExploit.hpp"
#include "protocol_probe/exploits/GenericSensor.hpp"
#include "protocol_probe/exploits/SpiSensor.hpp"

#include <iomanip>
#include <ostream>
#include <sstream>
#include <streambuf>
#include <vector>

using namespace Poco::Util;

namespace protocol_probe {
namespace {
class LoggerStreamBuf : public std::streambuf {
public:
    LoggerStreamBuf(Poco::Logger& logger, bool isError)
        : _logger(logger), _isError(isError) {}

protected:
    int overflow(int ch) override {
        if (ch == traits_type::eof()) {
            return sync() == 0 ? traits_type::not_eof(ch) : traits_type::eof();
        }

        const char c = static_cast<char>(ch);
        if (c == '\n') {
            emit();
        } else if (c != '\r') {
            _buffer.push_back(c);
        }
        return ch;
    }

    int sync() override {
        emit();
        return 0;
    }

private:
    void emit() {
        if (_buffer.empty()) {
            return;
        }
        if (_isError) {
            _logger.error(_buffer);
        } else {
            _logger.information(_buffer);
        }
        _buffer.clear();
    }

private:
    Poco::Logger& _logger;
    bool _isError;
    std::string _buffer;
};
} // namespace

CliRunner::CliRunner(const std::string& cmd,
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
                     const std::function<void()>& displayHelp)
    : _cmd(cmd), _payload(payload), _duration(duration), _baud(baud), _address(address)
    , _length(length), _slaveId(slaveId), _regCount(regCount)
    , _pinTx(pinTx), _pinRx(pinRx), _pinMosi(pinMosi), _pinMiso(pinMiso)
    , _pinSck(pinSck), _pinCs(pinCs), _pinScl(pinScl), _pinSda(pinSda)
    , _displayHelp(displayHelp)
{}

int CliRunner::run() {
    Poco::Logger& logger = Poco::Logger::get("cli");
    LoggerStreamBuf outBuf(logger, false);
    LoggerStreamBuf errBuf(logger, true);
    std::ostream out(&outBuf);
    std::ostream err(&errBuf);

    try {
        hardware::FtdiDevice device;
        device.open(ProbeConfig::deviceVendorId(), ProbeConfig::deviceProductId());

        if (_cmd == "status") {
            out << "[OK] FT232RL connected." << std::endl;

        } else if (_cmd == "discover") {
            exploits::ProtocolDiscovery discovery(device);
            out << "Running discovery (" << _duration << " ms)..." << std::endl;
            auto results = discovery.discover(_duration);
            if (results.empty()) {
                out << "No protocols detected." << std::endl;
            } else {
                out << "Found " << results.size() << " result(s):" << std::endl;
                for (const auto& r : results) {
                    out << "  Protocol: " << r.protocol << std::endl;
                    for (const auto& [name, bit] : r.pins) {
                        out << "    " << name << ": D" << bit << std::endl;
                    }
                    for (const auto& [key, val] : r.details) {
                        out << "    " << key << ": " << val << std::endl;
                    }
                    if (r.protocol == "I2C" || r.protocol == "i2c") {
                        auto scl = r.pins.count("scl") ? r.pins.at("scl") : 5;
                        auto sda = r.pins.count("sda") ? r.pins.at("sda") : 6;
                        out << "    CLI hint: /scl:" << scl << " /sda:" << sda << std::endl;
                    } else if (r.protocol == "SPI" || r.protocol == "spi") {
                        auto mosi = r.pins.count("mosi") ? r.pins.at("mosi") : 2;
                        auto miso = r.pins.count("miso") ? r.pins.at("miso") : 3;
                        auto sck = r.pins.count("sck") ? r.pins.at("sck") : 4;
                        auto cs = r.pins.count("cs") ? r.pins.at("cs") : 7;
                        out << "    CLI hint: /mosi:" << mosi << " /miso:" << miso
                            << " /sck:" << sck << " /cs:" << cs << std::endl;
                    } else if (r.protocol == "UART" || r.protocol == "uart") {
                        auto tx = r.pins.count("tx") ? r.pins.at("tx") : 0;
                        auto rx = r.pins.count("rx") ? r.pins.at("rx") : 1;
                        out << "    CLI hint: /tx:" << tx << " /rx:" << rx << std::endl;
                    }
                }
            }

        } else if (_cmd == "spi-id") {
            hardware::SpiInterface spi(device);
            spi.begin(spiPins());
            exploits::SpiFlash flash(spi);
            flash.loadId();
            auto id = flash.getJedecId();
            out << "JEDEC ID : " << id.toString() << std::endl;
            out << "Device   : " << flash.getDeviceName() << std::endl;
            out << "Capacity : " << flash.getCapacityBytes() << " bytes" << std::endl;

        } else if (_cmd == "spi-dump") {
            hardware::SpiInterface spi(device);
            spi.begin(spiPins());
            exploits::SpiFlash flash(spi);
            out << "Reading " << _length << " bytes from 0x"
                << std::hex << _address << std::dec << " ..." << std::endl;
            auto data = flash.read(static_cast<uint32_t>(_address), _length);
            for (size_t i = 0; i < data.size(); ++i) {
                if (i % 16 == 0) {
                    out << std::hex << std::setw(6) << std::setfill('0') << (_address + i) << ": ";
                }
                out << std::hex << std::setw(2) << std::setfill('0') << (int)data[i] << ' ';
                if (i % 16 == 15) {
                    out << std::endl;
                }
            }
            if (data.size() % 16 != 0) {
                out << std::endl;
            }

        } else if (_cmd == "i2c-scan") {
            hardware::I2cInterface i2c(device);
            i2c.begin(i2cPins());
            exploits::I2cScanner fuzzer(i2c);
            out << "Scanning I2C bus..." << std::endl;
            auto found = fuzzer.scanDevices();
            if (found.empty()) {
                out << "No devices found." << std::endl;
            } else {
                out << "Found " << found.size() << " device(s):" << std::endl;
                for (uint8_t addr : found) {
                    out << "  0x" << std::hex << std::setw(2) << std::setfill('0') << (int)addr << std::endl;
                }
            }

        } else if (_cmd == "uart-scan") {
            exploits::UartScanner fuzzer(device);
            out << "Scanning UART baud rates (" << _duration << " ms)..." << std::endl;
            auto results = fuzzer.scanBaudRates(
                {1200, 2400, 4800, 9600, 19200, 38400, 57600, 115200, 230400}, _duration);
            for (const auto& r : results) {
                out << "  " << r.baudRate << " baud  score="
                    << r.printableRatio << "  sample=" << r.sample << std::endl;
            }

        } else if (_cmd == "gpio-scan") {
            exploits::GpioProbe fuzzer(device);
            out << "Scanning GPIO activity (" << _duration << " ms)..." << std::endl;
            auto results = fuzzer.scanActivity(_duration);
            for (const auto& [pin, r] : results) {
                out << "  D" << (int)pin
                    << (r.isStatic ? "  STATIC" : "  ACTIVE")
                    << "  transitions=" << r.transitions << std::endl;
            }

        } else if (_cmd == "modbus-read") {
            hardware::UartInterface uart(device);
            uart.begin(_baud, uartPins());
            exploits::ModbusExploit modbus(uart);
            out << "Modbus slave=" << (int)_slaveId
                << "  start=" << _address
                << "  count=" << _regCount << std::endl;
            auto raw = modbus.readHoldingRegisters(_slaveId, static_cast<uint16_t>(_address), _regCount);
            if (raw.size() >= 5) {
                size_t dataBytes = raw[2];
                for (size_t i = 0; i + 1 < dataBytes && 3 + i + 1 < raw.size(); i += 2) {
                    uint16_t val = ((uint16_t)raw[3 + i] << 8) | raw[3 + i + 1];
                    out << "  reg[" << (_address + i / 2) << "] = " << val << std::endl;
                }
            } else {
                out << "No valid response (" << raw.size() << " bytes)." << std::endl;
            }

        } else if (_cmd == "i2c-eeprom") {
            hardware::I2cInterface i2c(device);
            i2c.begin(i2cPins());
            uint8_t devAddr = (_slaveId != 1) ? static_cast<uint8_t>(_slaveId) : ProbeConfig::eepromDefaultAddress();
            uint8_t addrSize = 1;
            exploits::I2cEeprom eeprom(i2c, devAddr, addrSize);

            if (!_payload.empty()) {
                std::vector<uint8_t> writeData;
                for (size_t i = 0; i + 1 < _payload.size(); i += 2) {
                    writeData.push_back(static_cast<uint8_t>(std::stoul(_payload.substr(i, 2), nullptr, 16)));
                }
                out << "Writing " << writeData.size() << " bytes to EEPROM 0x"
                    << std::hex << (int)devAddr << " at offset 0x" << _address << std::dec << " ..." << std::endl;
                eeprom.write(static_cast<uint32_t>(_address), writeData);
                out << "Write complete. Reading back..." << std::endl;
                auto readback = eeprom.read(static_cast<uint32_t>(_address), writeData.size());
                bool ok = (readback == writeData);
                out << "Verify: " << (ok ? "PASS" : "FAIL") << std::endl;
                for (size_t i = 0; i < readback.size(); ++i) {
                    if (i % 16 == 0) {
                        out << std::hex << std::setw(4) << std::setfill('0') << (_address + i) << ": ";
                    }
                    out << std::hex << std::setw(2) << std::setfill('0') << (int)readback[i] << ' ';
                    if (i % 16 == 15) {
                        out << std::endl;
                    }
                }
                if (readback.size() % 16 != 0) {
                    out << std::endl;
                }
            } else {
                out << "Reading " << _length << " bytes from EEPROM 0x"
                    << std::hex << (int)devAddr << " at offset 0x" << _address
                    << std::dec << " ..." << std::endl;
                auto data = eeprom.read(static_cast<uint32_t>(_address), _length);
                for (size_t i = 0; i < data.size(); ++i) {
                    if (i % 16 == 0) {
                        out << std::hex << std::setw(4) << std::setfill('0') << (_address + i) << ": ";
                    }
                    out << std::hex << std::setw(2) << std::setfill('0') << (int)data[i] << ' ';
                    if (i % 16 == 15) {
                        out << std::endl;
                    }
                }
                if (data.size() % 16 != 0) {
                    out << std::endl;
                }
            }

        } else if (_cmd == "nmea") {
            hardware::UartInterface uart(device);
            uart.begin(_baud, uartPins());
            exploits::NmeaExploit nmea(uart);
            out << "Waiting for NMEA sentence (" << _duration << " ms)..." << std::endl;
            auto sentence = nmea.receiveSentence(ProbeConfig::nmeaPrefix(), _duration);
            if (sentence.empty()) {
                out << "Timeout - no sentence received." << std::endl;
            } else {
                out << sentence << std::endl;
            }

        } else if (_cmd == "mavlink-rx") {
            hardware::UartInterface uart(device);
            uart.begin(_baud, uartPins());
            exploits::MAVLinkExploit mavlink(uart);
            out << "Waiting for MAVLink packet (" << _duration << " ms, " << _baud << " baud)..." << std::endl;
            auto packet = mavlink.receivePacket(_duration);
            if (packet.empty()) {
                out << "Timeout - no packet received." << std::endl;
            } else {
                out << "Received " << packet.size() << " bytes:" << std::endl;
                for (size_t i = 0; i < packet.size(); ++i) {
                    out << std::hex << std::setw(2) << std::setfill('0') << (int)packet[i] << ' ';
                }
                out << std::endl;
                if (packet.size() >= 6) {
                    out << "  STX=0x" << std::hex << (int)packet[0]
                        << " len=" << std::dec << (int)packet[1]
                        << " seq=" << (int)packet[2]
                        << " sysid=" << (int)packet[3]
                        << " compid=" << (int)packet[4]
                        << " msgid=" << (int)packet[5] << std::endl;
                }
            }

        } else if (_cmd == "smbus-read") {
            hardware::I2cInterface i2c(device);
            i2c.begin(i2cPins());
            uint8_t devAddr = (_slaveId != 1) ? static_cast<uint8_t>(_slaveId) : 0x48;
            uint8_t cmd = static_cast<uint8_t>(_address);
            exploits::SmBusExploit smbus(i2c);
            if (_regCount >= 2) {
                uint16_t val = smbus.readWord(devAddr, cmd);
                out << "SMBus 0x" << std::hex << (int)devAddr
                    << " cmd=0x" << (int)cmd
                    << " word=0x" << std::setw(4) << std::setfill('0') << (int)val
                    << " (" << std::dec << val << ")" << std::endl;
            } else {
                uint8_t val = smbus.readByte(devAddr, cmd);
                out << "SMBus 0x" << std::hex << (int)devAddr
                    << " cmd=0x" << (int)cmd
                    << " byte=0x" << std::setw(2) << std::setfill('0') << (int)val
                    << " (" << std::dec << val << ")" << std::endl;
            }

        } else if (_cmd == "pmbus-read") {
            hardware::I2cInterface i2c(device);
            i2c.begin(i2cPins());
            uint8_t devAddr = (_slaveId != 1) ? static_cast<uint8_t>(_slaveId) : 0x10;
            exploits::PmBusExploit pmbus(i2c);
            uint8_t cmdVout = static_cast<uint8_t>(_address ? _address : ProbeConfig::pmbusReadVout());
            uint8_t cmdIout = ProbeConfig::pmbusReadIout();
            uint8_t cmdPout = ProbeConfig::pmbusReadPout();
            uint8_t cmdTemp = ProbeConfig::pmbusReadTemp1();
            int8_t n = pmbus.readVoutMode(devAddr);
            float volts = pmbus.readVoltage(devAddr, cmdVout);
            float amps = pmbus.readCurrent(devAddr, cmdIout);
            float watts = pmbus.readPower(devAddr, cmdPout);
            float degC = pmbus.readTemperature(devAddr, cmdTemp);
            out << "PMBus 0x" << std::hex << (int)devAddr << std::dec
                << " (VOUT_MODE N=" << (int)n << ")" << std::endl;
            out << "  VOUT  (0x" << std::hex << (int)cmdVout << "): " << std::dec << volts << " V" << std::endl;
            out << "  IOUT  (0x" << std::hex << (int)cmdIout << "): " << std::dec << amps << " A" << std::endl;
            out << "  POUT  (0x" << std::hex << (int)cmdPout << "): " << std::dec << watts << " W" << std::endl;
            out << "  TEMP1 (0x" << std::hex << (int)cmdTemp << "): " << std::dec << degC << " C" << std::endl;

        } else if (_cmd == "sd-read") {
            hardware::SpiInterface spi(device);
            spi.begin(spiPins());
            exploits::SdCardExploit sd(spi);
            if (!sd.begin()) {
                err << "SD card initialisation failed." << std::endl;
                return Application::EXIT_SOFTWARE;
            }
            uint32_t block = static_cast<uint32_t>(_address);
            out << "Reading SD block " << block << " ..." << std::endl;
            auto data = sd.readBlock(block);
            if (data.empty()) {
                err << "Read failed." << std::endl;
            } else {
                for (size_t i = 0; i < data.size(); ++i) {
                    if (i % 16 == 0) {
                        out << std::hex << std::setw(4) << std::setfill('0') << i << ": ";
                    }
                    out << std::hex << std::setw(2) << std::setfill('0') << (int)data[i] << ' ';
                    if (i % 16 == 15) {
                        out << std::endl;
                    }
                }
                if (data.size() % 16 != 0) {
                    out << std::endl;
                }
            }

        } else if (_cmd == "sd-write") {
            if (_payload.empty()) {
                err << "sd-write requires /payload:<1024 hex chars>" << std::endl;
                return Application::EXIT_USAGE;
            }
            std::vector<uint8_t> blockData;
            blockData.reserve(512);
            for (size_t i = 0; i + 1 < _payload.size(); i += 2) {
                blockData.push_back(static_cast<uint8_t>(std::stoul(_payload.substr(i, 2), nullptr, 16)));
            }
            blockData.resize(512, 0xFF);
            hardware::SpiInterface spi(device);
            spi.begin(spiPins());
            exploits::SdCardExploit sd(spi);
            if (!sd.begin()) {
                err << "SD card initialisation failed." << std::endl;
                return Application::EXIT_SOFTWARE;
            }
            uint32_t block = static_cast<uint32_t>(_address);
            out << "Writing 512 bytes to SD block " << block << " ..." << std::endl;
            if (sd.writeBlock(block, blockData)) {
                out << "Write OK." << std::endl;
            } else {
                err << "Write failed." << std::endl;
            }

        } else if (_cmd == "spi-sensor") {
            hardware::SpiInterface spi(device);
            spi.begin(spiPins());
            exploits::SpiSensor sensor(spi);
            uint8_t startReg = static_cast<uint8_t>(_address);
            size_t count = _regCount ? static_cast<size_t>(_regCount) : 16;
            out << "Reading " << count << " register(s) from SPI sensor starting at 0x"
                << std::hex << (int)startReg << std::dec << " ..." << std::endl;
            auto regs = sensor.readRegisters(startReg, count);
            for (size_t i = 0; i < regs.size(); ++i) {
                out << "  reg[0x" << std::hex << std::setw(2) << std::setfill('0') << (int)(startReg + i)
                    << "] = 0x" << std::setw(2) << std::setfill('0') << (int)regs[i]
                    << " (" << std::dec << (int)regs[i] << ")" << std::endl;
            }

        } else if (_cmd == "sensor-read") {
            hardware::I2cInterface i2c(device);
            i2c.begin(i2cPins());
            uint8_t devAddr = (_slaveId != 1) ? static_cast<uint8_t>(_slaveId) : ProbeConfig::sensorI2cDefaultAddress();
            exploits::GenericSensor sensor(i2c, devAddr);
            out << sensor.getSensorName() << std::endl;
            if (!sensor.begin()) {
                out << "No device found at 0x" << std::hex << (int)devAddr << std::endl;
            } else {
                auto readings = sensor.readAll();
                for (const auto& [name, val] : readings) {
                    out << "  " << name << ": " << val << " V" << std::endl;
                }
            }

        } else if (_cmd == "i2c-registers") {
            hardware::I2cInterface i2c(device);
            i2c.begin(i2cPins());
            uint8_t devAddr = (_slaveId != 1) ? static_cast<uint8_t>(_slaveId) : 0x50;
            uint32_t startReg = static_cast<uint32_t>(_address);
            uint32_t endReg = (_regCount > 0) ? static_cast<uint32_t>(_regCount) : 255u;
            exploits::I2cScanner fuzzer(i2c);
            out << "Scanning registers of I2C device 0x"
                << std::hex << (int)devAddr
                << " [0x" << startReg << "..0x" << endReg << "]..." << std::dec << std::endl;
            auto regs = fuzzer.scanRegisters(devAddr, startReg, endReg);
            int found = 0;
            for (const auto& r : regs) {
                if (!r.exists) {
                    continue;
                }
                out << "  [0x" << std::hex << std::setw(2) << std::setfill('0') << r.address
                    << "] = 0x" << std::setw(2) << std::setfill('0') << (int)r.value
                    << std::dec << std::endl;
                ++found;
            }
            if (found == 0) {
                out << "No readable registers found." << std::endl;
            }

        } else if (_cmd == "gpio-fuzz") {
            exploits::GpioProbe fuzzer(device);
            if (_slaveId == 1) {
                uint32_t freq = static_cast<uint32_t>(_baud ? _baud : 100000);
                size_t cnt = static_cast<size_t>(_length ? _length : 1024);
                out << "Capturing " << cnt << " samples at " << freq << " Hz..." << std::endl;
                auto data = fuzzer.capture(freq, cnt);
                for (size_t i = 0; i < data.size(); ++i) {
                    if (i % 16 == 0) {
                        out << std::hex << std::setw(4) << std::setfill('0') << i << ": ";
                    }
                    out << std::hex << std::setw(2) << std::setfill('0') << (int)data[i] << ' ';
                    if (i % 16 == 15) {
                        out << std::endl;
                    }
                }
                if (data.size() % 16 != 0) {
                    out << std::endl;
                }
            } else if (_slaveId == 2) {
                out << "Running GPIO crosstalk scan..." << std::endl;
                auto results = fuzzer.performCrosstalkScan();
                for (const auto& r : results) {
                    out << "  Driver D" << (int)r.driverPin << " affects:";
                    for (uint8_t p : r.reactingPins) {
                        out << " D" << (int)p;
                    }
                    out << std::endl;
                }
            } else {
                out << "Scanning GPIO activity (" << _duration << " ms)..." << std::endl;
                auto results = fuzzer.scanActivity(_duration);
                for (const auto& [pin, r] : results) {
                    out << "  D" << (int)pin
                        << (r.isStatic ? "  STATIC" : "  ACTIVE")
                        << "  transitions=" << r.transitions << std::endl;
                }
            }

        } else if (_cmd == "uart-fuzz") {
            exploits::UartScanner fuzzer(device);
            std::vector<uint8_t> payload;
            if (!_payload.empty()) {
                for (size_t i = 0; i + 1 < _payload.size(); i += 2) {
                    uint8_t byte = static_cast<uint8_t>(std::stoul(_payload.substr(i, 2), nullptr, 16));
                    payload.push_back(byte);
                }
            } else {
                for (int i = 0; i < 16; ++i) {
                    payload.push_back(static_cast<uint8_t>(i));
                }
            }
            out << "Injecting " << payload.size() << " malformed UART bytes at "
                << _baud << " baud..." << std::endl;
            fuzzer.injectMalformed(_baud, payload);
            out << "Done." << std::endl;

        } else if (_cmd == "i2c-fuzz") {
            hardware::I2cInterface i2c(device);
            i2c.begin(i2cPins());
            exploits::I2cScanner fuzzer(i2c);
            out << "Fuzzing I2C bus (scanning all 127 addresses)..." << std::endl;
            auto found = fuzzer.scanDevices();
            if (found.empty()) {
                out << "No devices found." << std::endl;
            } else {
                out << "Found " << found.size() << " device(s):" << std::endl;
                for (uint8_t addr : found) {
                    out << "  0x" << std::hex << std::setw(2) << std::setfill('0') << (int)addr << std::endl;
                }
            }

        } else if (_cmd == "uart-terminal") {
            hardware::UartInterface uart(device);
            uart.begin(_baud, uartPins());
            out << "UART terminal at " << _baud << " baud" << std::endl;
            if (!_payload.empty()) {
                std::vector<uint8_t> bytes;
                for (size_t i = 0; i + 1 < _payload.size(); i += 2) {
                    uint8_t byte = static_cast<uint8_t>(std::stoul(_payload.substr(i, 2), nullptr, 16));
                    bytes.push_back(byte);
                }
                uart.send(bytes);
                out << "Sent " << bytes.size() << " bytes." << std::endl;
            }
            size_t expect = static_cast<size_t>(_regCount ? _regCount : 64);
            out << "Waiting for up to " << expect << " bytes ("
                << _duration << " ms timeout)..." << std::endl;
            auto resp = uart.receive(expect, _duration);
            if (resp.empty()) {
                out << "No response received." << std::endl;
            } else {
                out << "Received " << resp.size() << " bytes:" << std::endl;
                for (size_t i = 0; i < resp.size(); ++i) {
                    if (i % 16 == 0) {
                        out << std::hex << std::setw(4) << std::setfill('0') << i << ": ";
                    }
                    out << std::hex << std::setw(2) << std::setfill('0') << (int)resp[i] << ' ';
                    if (i % 16 == 15) {
                        out << std::endl;
                    }
                }
                if (resp.size() % 16 != 0) {
                    out << std::endl;
                }
                out << "ASCII: ";
                for (uint8_t b : resp) {
                    out << static_cast<char>((b >= 0x20 && b < 0x7F) ? b : '.');
                }
                out << std::endl;
            }

        } else if (_cmd == "spi-read") {
            hardware::SpiInterface spi(device);
            spi.begin(spiPins());
            exploits::SpiFlash flash(spi);
            out << "Reading " << _length << " bytes from 0x"
                << std::hex << _address << std::dec << " ..." << std::endl;
            auto data = flash.read(static_cast<uint32_t>(_address), _length);
            for (size_t i = 0; i < data.size(); ++i) {
                if (i % 16 == 0) {
                    out << std::hex << std::setw(6) << std::setfill('0') << (_address + i) << ": ";
                }
                out << std::hex << std::setw(2) << std::setfill('0') << (int)data[i] << ' ';
                if (i % 16 == 15) {
                    out << std::endl;
                }
            }
            if (data.size() % 16 != 0) {
                out << std::endl;
            }

        } else if (_cmd == "spi-write") {
            if (_payload.empty()) {
                err << "spi-write requires /payload:<hex>" << std::endl;
                return Application::EXIT_USAGE;
            }
            hardware::SpiInterface spi(device);
            spi.begin(spiPins());
            exploits::SpiFlash flash(spi);
            std::vector<uint8_t> data;
            for (size_t i = 0; i + 1 < _payload.size(); i += 2) {
                data.push_back(static_cast<uint8_t>(std::stoul(_payload.substr(i, 2), nullptr, 16)));
            }
            out << "Writing " << data.size() << " bytes to 0x"
                << std::hex << _address << std::dec << " ..." << std::endl;
            flash.write(static_cast<uint32_t>(_address), data);
            out << "Done." << std::endl;

        } else if (_cmd == "spi-erase") {
            hardware::SpiInterface spi(device);
            spi.begin(spiPins());
            exploits::SpiFlash flash(spi);
            out << "Erasing " << _length << " bytes at 0x"
                << std::hex << _address << std::dec << " ..." << std::endl;
            flash.erase(static_cast<uint32_t>(_address), _length);
            out << "Done." << std::endl;

        } else if (_cmd == "i2c-read") {
            hardware::I2cInterface i2c(device);
            i2c.begin(i2cPins());
            uint8_t devAddr = static_cast<uint8_t>(_slaveId);
            out << "Reading " << _length << " bytes from device 0x"
                << std::hex << (int)devAddr << " reg 0x" << _address
                << std::dec << " ..." << std::endl;
            auto data = i2c.read(devAddr, static_cast<uint32_t>(_address), _length);
            for (size_t i = 0; i < data.size(); ++i) {
                if (i % 16 == 0) {
                    out << std::hex << std::setw(4) << std::setfill('0') << (_address + i) << ": ";
                }
                out << std::hex << std::setw(2) << std::setfill('0') << (int)data[i] << ' ';
                if (i % 16 == 15) {
                    out << std::endl;
                }
            }
            if (data.size() % 16 != 0) {
                out << std::endl;
            }

        } else if (_cmd == "i2c-write") {
            if (_payload.empty()) {
                err << "i2c-write requires /payload:<hex>" << std::endl;
                return Application::EXIT_USAGE;
            }
            hardware::I2cInterface i2c(device);
            i2c.begin(i2cPins());
            uint8_t devAddr = static_cast<uint8_t>(_slaveId);
            std::vector<uint8_t> data;
            for (size_t i = 0; i + 1 < _payload.size(); i += 2) {
                data.push_back(static_cast<uint8_t>(std::stoul(_payload.substr(i, 2), nullptr, 16)));
            }
            out << "Writing " << data.size() << " bytes to device 0x"
                << std::hex << (int)devAddr << " reg 0x" << _address << std::dec << " ..." << std::endl;
            bool ok = i2c.write(devAddr, static_cast<uint32_t>(_address), data);
            out << (ok ? "Done." : "Write failed (NACK).") << std::endl;

        } else if (_cmd == "i2c-erase") {
            hardware::I2cInterface i2c(device);
            i2c.begin(i2cPins());
            uint8_t devAddr = static_cast<uint8_t>(_slaveId);
            exploits::I2cEeprom eeprom(i2c, devAddr, 1);
            out << "Erasing " << _length << " bytes at device 0x"
                << std::hex << (int)devAddr << " offset 0x" << _address << std::dec << " ..." << std::endl;
            eeprom.erase(static_cast<uint32_t>(_address), _length);
            out << "Done." << std::endl;

        } else if (_cmd == "smbus-write") {
            hardware::I2cInterface i2c(device);
            i2c.begin(i2cPins());
            uint8_t devAddr = static_cast<uint8_t>(_slaveId);
            uint8_t cmd8 = static_cast<uint8_t>(_address);
            exploits::SmBusExploit smbus(i2c);
            if (_payload.size() >= 4) {
                uint16_t val = static_cast<uint16_t>(std::stoul(_payload, nullptr, 16));
                bool ok = smbus.writeWord(devAddr, cmd8, val);
                out << "SMBus writeWord 0x" << std::hex << (int)devAddr
                    << " cmd=0x" << (int)cmd8 << " -> " << (ok ? "OK" : "FAIL") << std::endl;
            } else {
                uint8_t val = static_cast<uint8_t>(std::stoul(_payload, nullptr, 16));
                bool ok = smbus.writeByte(devAddr, cmd8, val);
                out << "SMBus writeByte 0x" << std::hex << (int)devAddr
                    << " cmd=0x" << (int)cmd8 << " -> " << (ok ? "OK" : "FAIL") << std::endl;
            }

        } else if (_cmd == "uart-send") {
            if (_payload.empty()) {
                err << "uart-send requires /payload:<hex>" << std::endl;
                return Application::EXIT_USAGE;
            }
            hardware::UartInterface uart(device);
            uart.begin(_baud, uartPins());
            std::vector<uint8_t> bytes;
            for (size_t i = 0; i + 1 < _payload.size(); i += 2) {
                bytes.push_back(static_cast<uint8_t>(std::stoul(_payload.substr(i, 2), nullptr, 16)));
            }
            uart.send(bytes);
            out << "Sent " << bytes.size() << " bytes at " << _baud << " baud." << std::endl;

        } else if (_cmd == "uart-receive") {
            hardware::UartInterface uart(device);
            uart.begin(_baud, uartPins());
            size_t expect = _length ? _length : 256;
            out << "Receiving up to " << expect << " bytes (" << _duration << " ms timeout)..." << std::endl;
            auto resp = uart.receive(expect, _duration);
            if (resp.empty()) {
                out << "No data received." << std::endl;
            } else {
                for (size_t i = 0; i < resp.size(); ++i) {
                    if (i % 16 == 0) {
                        out << std::hex << std::setw(4) << std::setfill('0') << i << ": ";
                    }
                    out << std::hex << std::setw(2) << std::setfill('0') << (int)resp[i] << ' ';
                    if (i % 16 == 15) {
                        out << std::endl;
                    }
                }
                if (resp.size() % 16 != 0) {
                    out << std::endl;
                }
                out << "ASCII: ";
                for (uint8_t b : resp) {
                    out << static_cast<char>((b >= 0x20 && b < 0x7F) ? b : '.');
                }
                out << std::endl;
            }

        } else if (_cmd == "modbus-write") {
            if (_payload.empty()) {
                err << "modbus-write requires /payload:<XXXX> (4 hex digits)" << std::endl;
                return Application::EXIT_USAGE;
            }
            hardware::UartInterface uart(device);
            uart.begin(_baud, uartPins());
            exploits::ModbusExploit modbus(uart);
            uint16_t val = static_cast<uint16_t>(std::stoul(_payload, nullptr, 16));
            bool ok = modbus.writeSingleRegister(_slaveId, static_cast<uint16_t>(_address), val);
            out << "Modbus FC06 slave=" << (int)_slaveId
                << " reg=" << _address << " val=0x"
                << std::hex << (int)val << " -> " << (ok ? "OK" : "FAIL") << std::endl;

        } else if (_cmd == "modbus-write-multi") {
            if (_payload.empty()) {
                err << "modbus-write-multi requires /payload:<XXXX...>" << std::endl;
                return Application::EXIT_USAGE;
            }
            hardware::UartInterface uart(device);
            uart.begin(_baud, uartPins());
            exploits::ModbusExploit modbus(uart);
            std::vector<uint16_t> vals;
            for (size_t i = 0; i + 3 < _payload.size(); i += 4) {
                vals.push_back(static_cast<uint16_t>(std::stoul(_payload.substr(i, 4), nullptr, 16)));
            }
            bool ok = modbus.writeMultipleRegisters(_slaveId, static_cast<uint16_t>(_address), vals);
            out << "Modbus FC16 slave=" << (int)_slaveId
                << " start=" << _address << " count=" << vals.size()
                << " -> " << (ok ? "OK" : "FAIL") << std::endl;

        } else if (_cmd == "mavlink-heartbeat") {
            hardware::UartInterface uart(device);
            uart.begin(_baud, uartPins());
            exploits::MAVLinkExploit mavlink(uart);
            mavlink.sendHeartbeat();
            out << "MAVLink heartbeat sent." << std::endl;

        } else if (_cmd == "gpio-capture") {
            exploits::GpioProbe fuzzer(device);
            uint32_t freq = static_cast<uint32_t>(_baud ? _baud : 100000);
            size_t cnt = static_cast<size_t>(_length ? _length : 1024);
            out << "Capturing " << cnt << " samples at " << freq << " Hz..." << std::endl;
            auto data = fuzzer.capture(freq, cnt);
            for (size_t i = 0; i < data.size(); ++i) {
                if (i % 16 == 0) {
                    out << std::hex << std::setw(4) << std::setfill('0') << i << ": ";
                }
                out << std::hex << std::setw(2) << std::setfill('0') << (int)data[i] << ' ';
                if (i % 16 == 15) {
                    out << std::endl;
                }
            }
            if (data.size() % 16 != 0) {
                out << std::endl;
            }

        } else if (_cmd == "gpio-crosstalk") {
            exploits::GpioProbe fuzzer(device);
            out << "Running GPIO crosstalk scan..." << std::endl;
            auto results = fuzzer.performCrosstalkScan();
            for (const auto& r : results) {
                out << "  Driver D" << (int)r.driverPin << " affects:";
                for (uint8_t p : r.reactingPins) {
                    out << " D" << (int)p;
                }
                out << std::endl;
            }

        } else {
            err << "Unknown command: " << _cmd << std::endl;
            displayHelp();
            return Application::EXIT_USAGE;
        }
    } catch (const std::exception& e) {
        err << "Error: " << e.what() << std::endl;
        return Application::EXIT_SOFTWARE;
    }

    out.flush();
    err.flush();
    return Application::EXIT_OK;
}

void CliRunner::displayHelp() const {
    _displayHelp();
}

hardware::UartPins CliRunner::uartPins() const {
    return hardware::UartPins{_pinTx, _pinRx};
}

hardware::SpiPins CliRunner::spiPins() const {
    hardware::SpiPins p;
    p.mosi = _pinMosi;
    p.miso = _pinMiso;
    p.sck = _pinSck;
    p.cs = _pinCs;
    return p;
}

hardware::I2cPins CliRunner::i2cPins() const {
    hardware::I2cPins p;
    p.scl = _pinScl;
    p.sda = _pinSda;
    return p;
}

} // namespace protocol_probe
