#include "protocol_probe/api/Handlers.hpp"
#include "protocol_probe/Config.hpp"
#include "protocol_probe/hardware/FtdiDevice.hpp"
#include "protocol_probe/hardware/SpiInterface.hpp"
#include "protocol_probe/hardware/I2cInterface.hpp"
#include "protocol_probe/hardware/UartInterface.hpp"
#include "protocol_probe/exploits/SpiFlash.hpp"
#include "protocol_probe/exploits/SdCardExploit.hpp"
#include "protocol_probe/exploits/I2cEeprom.hpp"
#include "protocol_probe/exploits/SmBusExploit.hpp"
#include "protocol_probe/exploits/PmBusExploit.hpp"
#include "protocol_probe/exploits/ModbusExploit.hpp"
#include "protocol_probe/exploits/MavLinkExploit.hpp"
#include "protocol_probe/exploits/NmeaExploit.hpp"
#include "protocol_probe/exploits/GenericSensor.hpp"
#include "protocol_probe/exploits/GpioProbe.hpp"
#include "protocol_probe/exploits/UartScanner.hpp"
#include "protocol_probe/exploits/I2cScanner.hpp"
#include "protocol_probe/exploits/ProtocolDiscovery.hpp"
#include "protocol_probe/exploits/Utils.hpp"

#include <Poco/Net/HTTPServerRequest.h>
#include <Poco/Net/HTTPServerResponse.h>
#include <Poco/Net/HTMLForm.h>
#include <Poco/JSON/Object.h>
#include <Poco/JSON/Array.h>
#include <Poco/Logger.h>
#include <iostream>
#include <iomanip>
#include <sstream>

namespace protocol_probe {
namespace api {

using namespace Poco::Net;
using namespace Poco::JSON;

// Helper to parse SPI pins
hardware::SpiPins parseSpiPins(const HTMLForm& form) {
    hardware::SpiPins pins{
        ProbeConfig::spiMosiPin(),
        ProbeConfig::spiMisoPin(),
        ProbeConfig::spiSckPin(),
        ProbeConfig::spiCsPin()
    };
    if (form.has("mosi")) pins.mosi = ProbeConfig::pinIndex(form.get("mosi"));
    if (form.has("miso")) pins.miso = ProbeConfig::pinIndex(form.get("miso"));
    if (form.has("sck"))  pins.sck  = ProbeConfig::pinIndex(form.get("sck"));
    if (form.has("cs"))   pins.cs   = ProbeConfig::pinIndex(form.get("cs"));
    return pins;
}

// Helper to parse I2C pins
hardware::I2cPins parseI2cPins(const HTMLForm& form) {
    hardware::I2cPins pins{ProbeConfig::i2cSclPin(), ProbeConfig::i2cSdaPin()};
    if (form.has("scl")) pins.scl = ProbeConfig::pinIndex(form.get("scl"));
    if (form.has("sda")) pins.sda = ProbeConfig::pinIndex(form.get("sda"));
    return pins;
}

// Helper to parse UART pins
hardware::UartPins parseUartPins(const HTMLForm& form) {
    hardware::UartPins pins{ProbeConfig::uartTxPin(), ProbeConfig::uartRxPin()};
    if (form.has("tx")) pins.tx = ProbeConfig::pinIndex(form.get("tx"));
    if (form.has("rx")) pins.rx = ProbeConfig::pinIndex(form.get("rx"));
    return pins;
}

void DeviceStatusHandler::handleRequest(HTTPServerRequest& request, HTTPServerResponse& response) {
    Poco::Logger::get("api").information("REQ %s %s", request.getMethod(), request.getURI());
    response.setChunkedTransferEncoding(true);
    response.setContentType("application/json");

    Object::Ptr jsonResponse = new Object();
    try {
        hardware::FtdiDevice device;
        device.open();
        jsonResponse->set("status", "connected");
        device.close();
    } catch (const std::exception& e) {
        jsonResponse->set("status", "disconnected");
        jsonResponse->set("error", e.what());
    }

    std::ostream& ostr = response.send();
    jsonResponse->stringify(ostr);
}

void FlashDumpHandler::handleRequest(HTTPServerRequest& request, HTTPServerResponse& response) {
    Poco::Logger::get("api").information("REQ %s %s", request.getMethod(), request.getURI());
    response.setChunkedTransferEncoding(true);
    response.setContentType("application/json");

    HTMLForm form(request);
    uint32_t addr = 0;
    size_t len = static_cast<size_t>(ProbeConfig::cliDefaultLength());
    uint32_t frequency = ProbeConfig::spiDefaultFrequency();

    if (form.has("address")) addr = std::stoul(form.get("address"), nullptr, 0);
    if (form.has("length")) len = std::stoul(form.get("length"), nullptr, 0);
    if (form.has("frequency")) frequency = std::stoul(form.get("frequency"), nullptr, 0);
    auto pins = parseSpiPins(form);

    Object::Ptr jsonResponse = new Object();
    try {
        hardware::FtdiDevice device;
        device.open();
        hardware::SpiInterface spi(device);
        spi.begin(pins);
        spi.setFrequency(frequency);

        auto driver = exploits::SpiFlash::createDriver(spi);

        if (request.getMethod() == HTTPServerRequest::HTTP_POST) {
            if (request.getURI().find("/api/flash/write") != std::string::npos) {
                std::vector<uint8_t> data;
                std::istream& istr = request.stream();
                uint8_t buffer[1024];
                while (istr.good()) {
                    istr.read(reinterpret_cast<char*>(buffer), sizeof(buffer));
                    data.insert(data.end(), buffer, buffer + istr.gcount());
                }
                driver->write(addr, data);
                jsonResponse->set("status", "success");
            } else if (request.getURI().find("/api/flash/erase") != std::string::npos) {
                driver->erase(addr, len);
                jsonResponse->set("status", "success");
            } else {
                auto data = driver->read(addr, len);
                jsonResponse->set("status", "success");
                jsonResponse->set("data_len", (int)data.size());
            }
        } else {
            auto data = driver->read(addr, len);
            jsonResponse->set("status", "success");
            jsonResponse->set("protocol", "SPI");
            Array::Ptr dataArray = new Array();
            for (uint8_t b : data) dataArray->add(static_cast<int>(b));
            jsonResponse->set("data", dataArray);
        }
    } catch (const std::exception& e) {
        jsonResponse->set("status", "error");
        jsonResponse->set("error", e.what());
    }
    std::ostream& ostr = response.send();
    jsonResponse->stringify(ostr);
}

void I2cDumpHandler::handleRequest(HTTPServerRequest& request, HTTPServerResponse& response) {
    response.setChunkedTransferEncoding(true);
    response.setContentType("application/json");
    HTMLForm form(request);
    uint32_t addr = 0;
    size_t len = static_cast<size_t>(ProbeConfig::cliDefaultLength());
    uint8_t deviceAddr = ProbeConfig::eepromDefaultAddress();
    uint8_t addrSize = static_cast<uint8_t>(ProbeConfig::eepromDefaultAddrSize());

    if (form.has("address"))   addr       = std::stoul(form.get("address"),   nullptr, 0);
    if (form.has("length"))    len        = std::stoul(form.get("length"),    nullptr, 0);
    if (form.has("device"))    deviceAddr = (uint8_t)std::stoul(form.get("device"),    nullptr, 0);
    if (form.has("addr_size")) addrSize   = (uint8_t)std::stoul(form.get("addr_size"), nullptr, 0);
    auto pins = parseI2cPins(form);

    Object::Ptr jsonResponse = new Object();
    try {
        hardware::FtdiDevice device;
        device.open();
        hardware::I2cInterface i2c(device);
        i2c.begin(pins);
        exploits::I2cEeprom eeprom(i2c, deviceAddr, addrSize);

        if (request.getMethod() == HTTPServerRequest::HTTP_POST) {
            // Write: body contains raw bytes to write starting at address
            std::vector<uint8_t> data;
            std::istream& istr = request.stream();
            uint8_t buf[512];
            while (istr.good()) {
                istr.read(reinterpret_cast<char*>(buf), sizeof(buf));
                std::streamsize n = istr.gcount();
                if (n > 0) data.insert(data.end(), buf, buf + n);
            }
            if (data.empty()) throw std::runtime_error("Empty body");
            eeprom.write(addr, data);
            jsonResponse->set("status",        "success");
            jsonResponse->set("bytes_written", (int)data.size());
        } else {
            auto data = eeprom.read(addr, len);
            jsonResponse->set("status", "success");
            jsonResponse->set("protocol", "I2C/EEPROM");
            jsonResponse->set("device_hex", [&]() {
                std::ostringstream h;
                h << "0x" << std::hex << std::uppercase
                  << std::setw(2) << std::setfill('0') << (int)deviceAddr;
                return h.str();
            }());
            Array::Ptr dataArray = new Array();
            for (uint8_t b : data) dataArray->add(static_cast<int>(b));
            jsonResponse->set("data", dataArray);
        }
    } catch (const std::exception& e) {
        jsonResponse->set("status", "error");
        jsonResponse->set("error",  e.what());
    }
    jsonResponse->stringify(response.send());
}

void GpioFuzzHandler::handleRequest(HTTPServerRequest& request, HTTPServerResponse& response) {
    Poco::Logger::get("api").information("REQ %s %s", request.getMethod(), request.getURI());
    response.setChunkedTransferEncoding(true);
    response.setContentType("application/json");
    HTMLForm form(request);
    std::string mode = form.get("mode", ProbeConfig::fuzzDefaultMode());
    int duration = std::stoi(form.get("duration", std::to_string(ProbeConfig::discoveryDefaultDuration())));
    
    Object::Ptr jsonResponse = new Object();
    try {
        hardware::FtdiDevice device;
        device.open();
        exploits::GpioProbe fuzzer(device);
        if (mode == "scan") {
            auto results = fuzzer.scanActivity(duration);
            Array::Ptr pins = new Array();
            for (auto const& [p, r] : results) {
                Object::Ptr o = new Object();
                o->set("pin", (int)p);
                o->set("is_static", r.isStatic);
                o->set("transitions", (int)r.transitions);
                pins->add(o);
            }
            jsonResponse->set("pins", pins);
        } else if (mode == "capture") {
            uint32_t freq = std::stoul(form.get("frequency", std::to_string(ProbeConfig::gpioCaptureDefaultFrequency())));
            size_t count = std::stoul(form.get("count", std::to_string(ProbeConfig::gpioCaptureDefaultSampleCount())));
            auto data = fuzzer.capture(freq, count);
            Array::Ptr arr = new Array();
            for (uint8_t b : data) arr->add((int)b);
            jsonResponse->set("data", arr);
        }
        jsonResponse->set("status", "success");
    } catch (const std::exception& e) {
        jsonResponse->set("status", "error");
        jsonResponse->set("error", e.what());
    }
    std::ostream& ostr = response.send();
    jsonResponse->stringify(ostr);
}

void UartFuzzHandler::handleRequest(HTTPServerRequest& request, HTTPServerResponse& response) {
    Poco::Logger::get("api").information("REQ %s %s", request.getMethod(), request.getURI());
    response.setChunkedTransferEncoding(true);
    response.setContentType("application/json");
    HTMLForm form(request);
    std::string mode = form.get("mode", ProbeConfig::fuzzDefaultMode());
    Object::Ptr jsonResponse = new Object();
    try {
        hardware::FtdiDevice device;
        device.open();
        exploits::UartScanner fuzzer(device);
        if (mode == "scan") {
            int duration = std::stoi(form.get("duration", std::to_string(ProbeConfig::discoveryDefaultDuration())));
            auto results = fuzzer.scanBaudRates({9600, 115200}, duration);
            Array::Ptr arr = new Array();
            for (auto const& r : results) {
                Object::Ptr o = new Object();
                o->set("baud", (int)r.baudRate);
                o->set("score", r.printableRatio);
                arr->add(o);
            }
            jsonResponse->set("results", arr);
        }
        jsonResponse->set("status", "success");
    } catch (const std::exception& e) {
        jsonResponse->set("status", "error");
        jsonResponse->set("error", e.what());
    }
    std::ostream& ostr = response.send();
    jsonResponse->stringify(ostr);
}

void UartTerminalHandler::handleRequest(HTTPServerRequest& request, HTTPServerResponse& response) {
    Poco::Logger::get("api").information("REQ %s %s", request.getMethod(), request.getURI());
    response.setChunkedTransferEncoding(true);
    response.setContentType("application/json");
    HTMLForm form(request);
    uint32_t baud = std::stoul(form.get("baud", std::to_string(ProbeConfig::uartDefaultBaud())));
    std::string hexPayload = form.get("payload", "");
    size_t expect = std::stoul(form.get("expect", std::to_string(ProbeConfig::uartTerminalDefaultExpect())));
    int timeout = std::stoi(form.get("timeout", std::to_string(ProbeConfig::uartTerminalDefaultTimeoutMs())));
    auto pins = parseUartPins(form);

    Object::Ptr jsonResponse = new Object();
    try {
        hardware::FtdiDevice device;
        device.open();
        hardware::UartInterface uart(device);
        uart.begin(baud, pins);
        
        if (!hexPayload.empty()) {
            auto data = exploits::Utils::hexToBytes(hexPayload);
            uart.send(data);
        }
        
        auto resp = uart.receive(expect, timeout);
        Array::Ptr arr = new Array();
        for (uint8_t b : resp) arr->add((int)b);
        jsonResponse->set("response", arr);
        jsonResponse->set("status", "success");
    } catch (const std::exception& e) {
        jsonResponse->set("status", "error");
        jsonResponse->set("error", e.what());
    }
    std::ostream& ostr = response.send();
    jsonResponse->stringify(ostr);
}

void I2cFuzzHandler::handleRequest(HTTPServerRequest& request, HTTPServerResponse& response) {
    Poco::Logger::get("api").information("REQ %s %s", request.getMethod(), request.getURI());
    response.setChunkedTransferEncoding(true);
    response.setContentType("application/json");
    HTMLForm form(request);
    auto pins = parseI2cPins(form);

    Object::Ptr jsonResponse = new Object();
    try {
        hardware::FtdiDevice device;
        device.open();
        hardware::I2cInterface i2c(device);
        i2c.begin(pins);
        exploits::I2cScanner fuzzer(i2c);
        auto found = fuzzer.scanDevices();
        Array::Ptr arr = new Array();
        for (uint8_t a : found) arr->add((int)a);
        jsonResponse->set("found", arr);
        jsonResponse->set("status", "success");
    } catch (const std::exception& e) {
        jsonResponse->set("status", "error");
        jsonResponse->set("error", e.what());
    }
    std::ostream& ostr = response.send();
    jsonResponse->stringify(ostr);
}

void ProtocolDiscoveryHandler::handleRequest(HTTPServerRequest& request, HTTPServerResponse& response) {
    Poco::Logger::get("api").information("REQ %s %s", request.getMethod(), request.getURI());
    response.setChunkedTransferEncoding(true);
    response.setContentType("application/json");
    HTMLForm form(request);
    int duration = std::stoi(form.get("duration", std::to_string(ProbeConfig::discoveryDefaultDuration())));

    Object::Ptr jsonResponse = new Object();
    try {
        hardware::FtdiDevice device;
        device.open();
        exploits::ProtocolDiscovery discovery(device);
        auto results = discovery.discover(duration);
        
        Array::Ptr resultsArray = new Array();
        for (const auto& r : results) {
            Object::Ptr o = new Object();
            o->set("protocol", r.protocol);
            Object::Ptr pins = new Object();
            for (auto const& [name, bit] : r.pins) pins->set(name, bit);
            o->set("pins", pins);
            Object::Ptr details = new Object();
            for (auto const& [key, val] : r.details) details->set(key, val);
            o->set("details", details);
            resultsArray->add(o);
        }
        jsonResponse->set("results", resultsArray);
        jsonResponse->set("status", "success");
    } catch (const std::exception& e) {
        jsonResponse->set("status", "error");
        jsonResponse->set("error", e.what());
    }
    std::ostream& ostr = response.send();
    jsonResponse->stringify(ostr);
}

Poco::Net::HTTPRequestHandler* RequestHandlerFactory::createRequestHandler(const HTTPServerRequest& request) {
    std::string uri = request.getURI();
    // More-specific paths must come before their prefixes
    if (uri.find("/api/device/status")    != std::string::npos) return new DeviceStatusHandler();
    if (uri.find("/api/spi/id")           != std::string::npos) return new SpiIdHandler();
    if (uri.find("/api/spi/sd")           != std::string::npos) return new SdCardHandler();
    if (uri.find("/api/spi/")             != std::string::npos) return new FlashDumpHandler();
    if (uri.find("/api/i2c/scan")         != std::string::npos) return new I2cScanHandler();
    if (uri.find("/api/i2c/registers")    != std::string::npos) return new I2cRegisterScanHandler();
    if (uri.find("/api/i2c/")             != std::string::npos) return new I2cDumpHandler();
    if (uri.find("/api/smbus/")           != std::string::npos) return new SmBusHandler();
    if (uri.find("/api/pmbus/")           != std::string::npos) return new PmBusHandler();
    if (uri.find("/api/sensor/")          != std::string::npos) return new GenericSensorHandler();
    if (uri.find("/api/discover")         != std::string::npos) return new ProtocolDiscoveryHandler();
    if (uri.find("/api/gpio/scan")        != std::string::npos) return new GpioFuzzHandler();
    if (uri.find("/api/gpio/capture")     != std::string::npos) return new GpioFuzzHandler();
    if (uri.find("/api/gpio/crosstalk")   != std::string::npos) return new GpioFuzzHandler();
    if (uri.find("/api/uart/modbus")      != std::string::npos) return new ModbusHandler();
    if (uri.find("/api/uart/mavlink")     != std::string::npos) return new MavLinkHandler();
    if (uri.find("/api/uart/nmea")        != std::string::npos) return new NmeaHandler();
    if (uri.find("/api/uart/terminal")    != std::string::npos) return new UartTerminalHandler();
    if (uri.find("/api/uart/send")        != std::string::npos) return new UartTerminalHandler();
    if (uri.find("/api/fuzz/uart")        != std::string::npos) return new UartFuzzHandler();
    if (uri.find("/api/fuzz/i2c")         != std::string::npos) return new I2cFuzzHandler();
    // Legacy aliases (kept for backward compat)
    if (uri.find("/api/flash/")           != std::string::npos) return new FlashDumpHandler();
    if (uri.find("/api/pins/discover")    != std::string::npos) return new ProtocolDiscoveryHandler();
    if (uri.find("/api/pins/")            != std::string::npos) return new GpioFuzzHandler();
    return nullptr;
}

// ---------------------------------------------------------------------------
// MavLinkHandler  GET /api/uart/mavlink  → receive packet
//                 POST /api/uart/mavlink → send heartbeat
// ---------------------------------------------------------------------------
void MavLinkHandler::handleRequest(HTTPServerRequest& request, HTTPServerResponse& response) {
    Poco::Logger::get("api").information("REQ %s %s", request.getMethod(), request.getURI());
    response.setChunkedTransferEncoding(true);
    response.setContentType("application/json");
    HTMLForm form(request);
    auto     pins    = parseUartPins(form);
    uint32_t baud    = std::stoul(form.get("baud", std::to_string(ProbeConfig::mavlinkBaud())));
    int      timeout = std::stoi(form.get("timeout", std::to_string(ProbeConfig::mavlinkDefaultTimeoutMs())));

    Object::Ptr json = new Object();
    try {
        hardware::FtdiDevice device;
        device.open();
        hardware::UartInterface uart(device);
        uart.begin(baud, pins);
        exploits::MAVLinkExploit mavlink(uart);

        if (request.getMethod() == HTTPServerRequest::HTTP_POST) {
            mavlink.sendHeartbeat();
            json->set("status", "success");
            json->set("action", "heartbeat_sent");
            json->set("baud",   (int)baud);
        } else {
        auto packet = mavlink.receivePacket(timeout);
        json->set("status",  packet.empty() ? "timeout" : "success");
        json->set("baud",    (int)baud);
        json->set("length",  (int)packet.size());

        Array::Ptr arr = new Array();
        std::ostringstream hexStr;
        for (uint8_t b : packet) {
            arr->add((int)b);
            hexStr << std::hex << std::setw(2) << std::setfill('0') << (int)b;
        }
        json->set("raw",     arr);
        json->set("raw_hex", hexStr.str());

        // Decode MAVLink v1 header fields if enough bytes present
        if (packet.size() >= 6) {
            json->set("mavlink_stx",    (int)packet[0]);
            json->set("payload_length", (int)packet[1]);
            json->set("sequence",       (int)packet[2]);
            json->set("system_id",      (int)packet[3]);
            json->set("component_id",   (int)packet[4]);
            json->set("message_id",     (int)packet[5]);
        }
        }
    } catch (const std::exception& e) {
        json->set("status", "error");
        json->set("error",  e.what());
    }
    json->stringify(response.send());
}

// ---------------------------------------------------------------------------
// SmBusHandler  GET /api/smbus/read  → read byte or word
//               POST /api/smbus/write → write byte or word
// ---------------------------------------------------------------------------
void SmBusHandler::handleRequest(HTTPServerRequest& request, HTTPServerResponse& response) {
    Poco::Logger::get("api").information("REQ %s %s", request.getMethod(), request.getURI());
    response.setChunkedTransferEncoding(true);
    response.setContentType("application/json");
    HTMLForm form(request);
    auto    pins    = parseI2cPins(form);
    uint8_t devAddr = (uint8_t)std::stoul(form.get("device",  "0x" + [&]() {
        std::ostringstream h;
        h << std::hex << std::uppercase << (int)ProbeConfig::sensorI2cDefaultAddress();
        return h.str();
    }()), nullptr, 0);
    uint8_t cmd     = (uint8_t)std::stoul(form.get("command", std::to_string(ProbeConfig::smbusDefaultCommand())), nullptr, 0);
    std::string type = form.get("type", ProbeConfig::smbusDefaultType());

    Object::Ptr json = new Object();
    try {
        hardware::FtdiDevice device;
        device.open();
        hardware::I2cInterface i2c(device);
        i2c.begin(pins);
        exploits::SmBusExploit smbus(i2c);

        json->set("protocol",    "SMBus");
        json->set("device_addr", (int)devAddr);
        json->set("command",     (int)cmd);
        json->set("type",        type);

        if (request.getMethod() == HTTPServerRequest::HTTP_POST) {
            std::string hexVal = form.get("value", ProbeConfig::smbusDefaultValue());
            if (type == "word") {
                uint16_t val = (uint16_t)std::stoul(hexVal, nullptr, 0);
                bool ok = smbus.writeWord(devAddr, cmd, val);
                json->set("status", ok ? "success" : "error");
                json->set("value",  (int)val);
            } else {
                uint8_t val = (uint8_t)std::stoul(hexVal, nullptr, 0);
                bool ok = smbus.writeByte(devAddr, cmd, val);
                json->set("status", ok ? "success" : "error");
                json->set("value",  (int)val);
            }
        } else {
            json->set("status", "success");
            if (type == "word") {
                uint16_t val = smbus.readWord(devAddr, cmd);
                json->set("value",     (int)val);
                json->set("value_hex", [&]() {
                    std::ostringstream h;
                    h << "0x" << std::hex << std::uppercase
                      << std::setw(4) << std::setfill('0') << (int)val;
                    return h.str();
                }());
            } else {
                uint8_t val = smbus.readByte(devAddr, cmd);
                json->set("value",     (int)val);
                json->set("value_hex", [&]() {
                    std::ostringstream h;
                    h << "0x" << std::hex << std::uppercase
                      << std::setw(2) << std::setfill('0') << (int)val;
                    return h.str();
                }());
            }
        }
    } catch (const std::exception& e) {
        json->set("status", "error");
        json->set("error",  e.what());
    }
    json->stringify(response.send());
}

// ---------------------------------------------------------------------------
// PmBusHandler  GET /api/pmbus/read
// Params: scl, sda, device (addr), command, type (voltage|current)
// ---------------------------------------------------------------------------
void PmBusHandler::handleRequest(HTTPServerRequest& request, HTTPServerResponse& response) {
    Poco::Logger::get("api").information("REQ %s %s", request.getMethod(), request.getURI());
    response.setChunkedTransferEncoding(true);
    response.setContentType("application/json");
    HTMLForm form(request);
    auto    pins    = parseI2cPins(form);
    uint8_t devAddr = (uint8_t)std::stoul(form.get("device", "0x" + [&]() {
        std::ostringstream h;
        h << std::hex << std::uppercase << (int)ProbeConfig::pmbusDefaultAddress();
        return h.str();
    }()), nullptr, 0);
    uint8_t cmd     = (uint8_t)std::stoul(form.get("command", "0x" + [&]() {
        std::ostringstream h;
        h << std::hex << std::uppercase << (int)ProbeConfig::pmbusReadVout();
        return h.str();
    }()), nullptr, 0);
    std::string type = form.get("type", ProbeConfig::pmbusDefaultType());

    Object::Ptr json = new Object();
    try {
        hardware::FtdiDevice device;
        device.open();
        hardware::I2cInterface i2c(device);
        i2c.begin(pins);
        exploits::PmBusExploit pmbus(i2c);

        float value = (type == "current")
                      ? pmbus.readCurrent(devAddr, cmd)
                      : pmbus.readVoltage(devAddr, cmd);

        json->set("status",      "success");
        json->set("protocol",    "PMBus");
        json->set("device_addr", (int)devAddr);
        json->set("command",     (int)cmd);
        json->set("type",        type);
        json->set("value",       value);
        json->set("unit",        type == "current" ? "A" : "V");
    } catch (const std::exception& e) {
        json->set("status", "error");
        json->set("error",  e.what());
    }
    json->stringify(response.send());
}

// ---------------------------------------------------------------------------
// SdCardHandler  GET /api/spi/sd  → read block
//                POST /api/spi/sd → write block (payload = hex bytes, 512 bytes)
// ---------------------------------------------------------------------------
void SdCardHandler::handleRequest(HTTPServerRequest& request, HTTPServerResponse& response) {
    Poco::Logger::get("api").information("REQ %s %s", request.getMethod(), request.getURI());
    response.setChunkedTransferEncoding(true);
    response.setContentType("application/json");
    HTMLForm form(request);
    auto     pins      = parseSpiPins(form);
    uint32_t blockAddr = std::stoul(form.get("block", std::to_string(ProbeConfig::sdcardDefaultBlock())));
    uint32_t frequency = std::stoul(form.get("frequency", std::to_string(ProbeConfig::spiDefaultFrequency())));

    Object::Ptr json = new Object();
    try {
        hardware::FtdiDevice device;
        device.open();
        hardware::SpiInterface spi(device);
        spi.begin(pins);
        spi.setFrequency(frequency);
        exploits::SdCardExploit sd(spi);

        if (!sd.begin()) throw std::runtime_error("SD card initialisation failed");

        if (request.getMethod() == HTTPServerRequest::HTTP_POST) {
            std::string hexPayload = form.get("payload", "");
            if (hexPayload.empty()) throw std::runtime_error("payload (hex) required for write");
            std::vector<uint8_t> data;
            for (size_t i = 0; i + 1 < hexPayload.size(); i += 2)
                data.push_back((uint8_t)std::stoul(hexPayload.substr(i, 2), nullptr, 16));
            if (data.size() != 512) throw std::runtime_error("SD block write requires exactly 512 bytes");
            bool ok = sd.writeBlock(blockAddr, data);
            json->set("status",  ok ? "success" : "error");
            json->set("block",   (int)blockAddr);
        } else {
            auto block = sd.readBlock(blockAddr);
            json->set("status",      "success");
            json->set("protocol",    "SD/SPI");
            json->set("block",       (int)blockAddr);
            json->set("bytes_read",  (int)block.size());

            Array::Ptr arr = new Array();
            for (uint8_t b : block) arr->add((int)b);
            json->set("data", arr);
        }
    } catch (const std::exception& e) {
        json->set("status", "error");
        json->set("error",  e.what());
    }
    json->stringify(response.send());
}

// ---------------------------------------------------------------------------
// GenericSensorHandler  GET /api/sensor/read
// Params: scl, sda, device [addr, default 0x48], channel [optional, -1 = all]
// ---------------------------------------------------------------------------
void GenericSensorHandler::handleRequest(HTTPServerRequest& request, HTTPServerResponse& response) {
    Poco::Logger::get("api").information("REQ %s %s", request.getMethod(), request.getURI());
    response.setChunkedTransferEncoding(true);
    response.setContentType("application/json");
    HTMLForm form(request);
    auto    pins    = parseI2cPins(form);
    uint8_t devAddr = (uint8_t)std::stoul(form.get("device", "0x" + [&]() {
        std::ostringstream h;
        h << std::hex << std::uppercase << (int)ProbeConfig::sensorI2cDefaultAddress();
        return h.str();
    }()), nullptr, 0);
    int     channel = std::stoi(form.get("channel", std::to_string(ProbeConfig::sensorDefaultChannel())));

    Object::Ptr json = new Object();
    try {
        hardware::FtdiDevice device;
        device.open();
        hardware::I2cInterface i2c(device);
        i2c.begin(pins);
        exploits::GenericSensor sensor(i2c, devAddr);

        json->set("sensor_name", sensor.getSensorName());
        json->set("channels",    (int)sensor.getChannelCount());

        if (!sensor.begin()) {
            json->set("status", "not_found");
            json->set("message", "No sensor responded at the given address");
        } else if (channel >= 0) {
            float val = sensor.readChannel(static_cast<uint8_t>(channel));
            json->set("status",  "success");
            json->set("channel", channel);
            json->set("value",   val);
            json->set("unit",    "V");
        } else {
            auto readings = sensor.readAll();
            Object::Ptr data = new Object();
            for (const auto& [name, val] : readings) data->set(name, val);
            json->set("status",   "success");
            json->set("readings", data);
            json->set("unit",     "V");
        }
    } catch (const std::exception& e) {
        json->set("status", "error");
        json->set("error",  e.what());
    }
    json->stringify(response.send());
}

// ---------------------------------------------------------------------------
// SpiIdHandler  GET /api/spi/id
// ---------------------------------------------------------------------------
void SpiIdHandler::handleRequest(HTTPServerRequest& request, HTTPServerResponse& response) {
    Poco::Logger::get("api").information("REQ %s %s", request.getMethod(), request.getURI());
    response.setChunkedTransferEncoding(true);
    response.setContentType("application/json");
    HTMLForm form(request);
    auto     pins      = parseSpiPins(form);
    uint32_t frequency = std::stoul(form.get("frequency", std::to_string(ProbeConfig::spiDefaultFrequency())));

    Object::Ptr json = new Object();
    try {
        hardware::FtdiDevice device;
        device.open();
        hardware::SpiInterface spi(device);
        spi.begin(pins);
        spi.setFrequency(frequency);

        exploits::SpiFlash flash(spi);
        auto id = flash.readId();

        std::ostringstream hexStr;
        hexStr << "0x" << std::hex << std::uppercase
               << std::setw(2) << std::setfill('0') << (int)id.manufacturer
               << std::setw(2) << std::setfill('0') << (int)id.memoryType
               << std::setw(2) << std::setfill('0') << (int)id.capacity;

        json->set("status",       "success");
        json->set("manufacturer", (int)id.manufacturer);
        json->set("memory_type",  (int)id.memoryType);
        json->set("capacity",     (int)id.capacity);
        json->set("jedec_hex",    hexStr.str());
        json->set("device_name",  flash.getDeviceName());
        json->set("capacity_bytes", (int)flash.getCapacityBytes());
    } catch (const std::exception& e) {
        json->set("status", "error");
        json->set("error",  e.what());
    }
    json->stringify(response.send());
}

// ---------------------------------------------------------------------------
// I2cScanHandler  GET /api/i2c/scan
// ---------------------------------------------------------------------------
void I2cScanHandler::handleRequest(HTTPServerRequest& request, HTTPServerResponse& response) {
    Poco::Logger::get("api").information("REQ %s %s", request.getMethod(), request.getURI());
    response.setChunkedTransferEncoding(true);
    response.setContentType("application/json");
    HTMLForm form(request);
    auto pins = parseI2cPins(form);

    Object::Ptr json = new Object();
    try {
        hardware::FtdiDevice device;
        device.open();
        hardware::I2cInterface i2c(device);
        i2c.begin(pins);
        exploits::I2cScanner fuzzer(i2c);
        auto found = fuzzer.scanDevices();

        Array::Ptr arr = new Array();
        for (uint8_t addr : found) {
            Object::Ptr dev = new Object();
            std::ostringstream hex;
            hex << "0x" << std::hex << std::uppercase
                << std::setw(2) << std::setfill('0') << (int)addr;
            dev->set("address",     (int)addr);
            dev->set("address_hex", hex.str());
            arr->add(dev);
        }
        json->set("status",  "success");
        json->set("count",   (int)found.size());
        json->set("devices", arr);
    } catch (const std::exception& e) {
        json->set("status", "error");
        json->set("error",  e.what());
    }
    json->stringify(response.send());
}

// ---------------------------------------------------------------------------
// I2cRegisterScanHandler  GET /api/i2c/registers
// ---------------------------------------------------------------------------
void I2cRegisterScanHandler::handleRequest(HTTPServerRequest& request, HTTPServerResponse& response) {
    Poco::Logger::get("api").information("REQ %s %s", request.getMethod(), request.getURI());
    response.setChunkedTransferEncoding(true);
    response.setContentType("application/json");
    HTMLForm form(request);
    auto    pins       = parseI2cPins(form);
    uint8_t deviceAddr = (uint8_t)std::stoul(form.get("device", "0x" + [&]() {
        std::ostringstream h;
        h << std::hex << std::uppercase << (int)ProbeConfig::eepromDefaultAddress();
        return h.str();
    }()), nullptr, 0);
    uint32_t startReg  = std::stoul(form.get("start", std::to_string(ProbeConfig::i2cRegisterDefaultStart())), nullptr, 0);
    uint32_t endReg    = std::stoul(form.get("end", std::to_string(ProbeConfig::i2cRegisterDefaultEnd())), nullptr, 0);
    uint8_t  addrSize  = (uint8_t)std::stoul(form.get("addr_size", std::to_string(ProbeConfig::eepromDefaultAddrSize())), nullptr, 0);

    Object::Ptr json = new Object();
    try {
        hardware::FtdiDevice device;
        device.open();
        hardware::I2cInterface i2c(device);
        i2c.begin(pins);
        exploits::I2cScanner fuzzer(i2c);
        auto regs = fuzzer.scanRegisters(deviceAddr, startReg, endReg, addrSize);

        Array::Ptr arr = new Array();
        for (const auto& r : regs) {
            if (!r.exists) continue;
            Object::Ptr reg = new Object();
            reg->set("address", (int)r.address);
            reg->set("value",   (int)r.value);
            arr->add(reg);
        }
        json->set("status",    "success");
        json->set("registers", arr);
    } catch (const std::exception& e) {
        json->set("status", "error");
        json->set("error",  e.what());
    }
    json->stringify(response.send());
}

// ---------------------------------------------------------------------------
// ModbusHandler  GET /api/uart/modbus  → read FC03
//                POST /api/uart/modbus → write FC06 (single) or FC16 (multi)
// ---------------------------------------------------------------------------
void ModbusHandler::handleRequest(HTTPServerRequest& request, HTTPServerResponse& response) {
    Poco::Logger::get("api").information("REQ %s %s", request.getMethod(), request.getURI());
    response.setChunkedTransferEncoding(true);
    response.setContentType("application/json");
    HTMLForm form(request);
    auto     pins      = parseUartPins(form);
    uint32_t baud      = std::stoul(form.get("baud", std::to_string(ProbeConfig::uartDefaultBaud())));
    uint8_t  slaveId   = (uint8_t)std::stoul(form.get("slave", std::to_string(ProbeConfig::cliDefaultSlaveId())));
    uint16_t startAddr = (uint16_t)std::stoul(form.get("start", std::to_string(ProbeConfig::modbusDefaultStart())), nullptr, 0);
    uint16_t count     = (uint16_t)std::stoul(form.get("count", std::to_string(ProbeConfig::modbusDefaultRegisterCount())), nullptr, 0);

    Object::Ptr json = new Object();
    try {
        hardware::FtdiDevice device;
        device.open();
        hardware::UartInterface uart(device);
        uart.begin(baud, pins);
        exploits::ModbusExploit modbus(uart);

        if (request.getMethod() == HTTPServerRequest::HTTP_POST) {
            // Write: payload is hex register values (4 hex chars per register)
            std::string hexPayload = form.get("payload", "");
            if (hexPayload.size() == 4) {
                // FC06: single register write
                uint16_t val = (uint16_t)std::stoul(hexPayload, nullptr, 16);
                bool ok = modbus.writeSingleRegister(slaveId, startAddr, val);
                json->set("status",    ok ? "success" : "error");
                json->set("function",  "FC06");
                json->set("register",  (int)startAddr);
                json->set("value",     (int)val);
            } else if (hexPayload.size() >= 4) {
                // FC16: multiple register write
                std::vector<uint16_t> vals;
                for (size_t i = 0; i + 3 < hexPayload.size(); i += 4)
                    vals.push_back((uint16_t)std::stoul(hexPayload.substr(i, 4), nullptr, 16));
                bool ok = modbus.writeMultipleRegisters(slaveId, startAddr, vals);
                json->set("status",   ok ? "success" : "error");
                json->set("function", "FC16");
                json->set("start",    (int)startAddr);
                json->set("count",    (int)vals.size());
            } else {
                throw std::runtime_error("payload must be 4+ hex chars (one 16-bit register = 4 chars)");
            }
        } else {
            auto raw = modbus.readHoldingRegisters(slaveId, startAddr, count);

            Array::Ptr arr = new Array();
            if (raw.size() >= 5) {
                size_t dataBytes = raw[2];
                for (size_t i = 0; i + 1 < dataBytes && 3 + i + 1 < raw.size(); i += 2) {
                    uint16_t val = ((uint16_t)raw[3 + i] << 8) | raw[3 + i + 1];
                    arr->add((int)val);
                }
            }
            json->set("status",    "success");
            json->set("slave_id",  (int)slaveId);
            json->set("start",     (int)startAddr);
            json->set("registers", arr);
        }
    } catch (const std::exception& e) {
        json->set("status", "error");
        json->set("error",  e.what());
    }
    json->stringify(response.send());
}

// ---------------------------------------------------------------------------
// NmeaHandler  GET /api/uart/nmea
// ---------------------------------------------------------------------------
void NmeaHandler::handleRequest(HTTPServerRequest& request, HTTPServerResponse& response) {
    Poco::Logger::get("api").information("REQ %s %s", request.getMethod(), request.getURI());
    response.setChunkedTransferEncoding(true);
    response.setContentType("application/json");
    HTMLForm form(request);
    auto        pins    = parseUartPins(form);
    uint32_t    baud    = std::stoul(form.get("baud", std::to_string(ProbeConfig::uartDefaultBaud())));
    std::string prefix  = form.get("prefix", ProbeConfig::nmeaPrefix());
    int         timeout = std::stoi(form.get("timeout", std::to_string(ProbeConfig::nmeaDefaultTimeoutMs())));

    Object::Ptr json = new Object();
    try {
        hardware::FtdiDevice device;
        device.open();
        hardware::UartInterface uart(device);
        uart.begin(baud, pins);
        exploits::NmeaExploit nmea(uart);

        auto sentence = nmea.receiveSentence(prefix, timeout);
        json->set("status",   sentence.empty() ? "timeout" : "success");
        json->set("sentence", sentence);
    } catch (const std::exception& e) {
        json->set("status", "error");
        json->set("error",  e.what());
    }
    json->stringify(response.send());
}

} // namespace api
} // namespace protocol_probe
