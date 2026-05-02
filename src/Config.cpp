#include "protocol_probe/Config.hpp"
#include <Poco/Logger.h>
#include <algorithm>
#include <cctype>
#include <sstream>
#include <stdexcept>

namespace protocol_probe {

// ---------------------------------------------------------------------------
// Static member definitions — built-in defaults match the original wiring
// ---------------------------------------------------------------------------
uint8_t ProbeConfig::_uartTx  = 0; // TXD
uint8_t ProbeConfig::_uartRx  = 1; // RXD
uint8_t ProbeConfig::_spiMosi = 2; // RTS
uint8_t ProbeConfig::_spiMiso = 3; // CTS
uint8_t ProbeConfig::_spiSck  = 4; // DTR
uint8_t ProbeConfig::_spiCs   = 7; // RI
uint8_t ProbeConfig::_i2cScl  = 5; // DSR
uint8_t ProbeConfig::_i2cSda  = 6; // DCD

uint32_t ProbeConfig::_uartBaud = 9600;
uint32_t ProbeConfig::_spiFreq  = 100000;
uint32_t ProbeConfig::_i2cFreq  = 100000;

uint32_t ProbeConfig::_mavBaud   = 57600;
uint8_t  ProbeConfig::_mavSysId  = 1;
uint8_t  ProbeConfig::_mavCompId = 1;

uint8_t ProbeConfig::_pmbusVoutMode = 0x20;
uint8_t ProbeConfig::_pmbusVout     = 0x8B;
uint8_t ProbeConfig::_pmbusIout     = 0x8C;
uint8_t ProbeConfig::_pmbusPout     = 0x96;
uint8_t ProbeConfig::_pmbusTemp1    = 0x8D;

int ProbeConfig::_sdInitRetries  = 100;
int ProbeConfig::_sdReadRetries  = 1000;
int ProbeConfig::_sdWriteRetries = 10000;

double ProbeConfig::_discoverySampleRate = 500000.0;
int    ProbeConfig::_discoveryDuration   = 2000;
int    ProbeConfig::_discoveryUartSniffMs = 2000;
int    ProbeConfig::_discoveryUartProbeMs = 2000;

unsigned short ProbeConfig::_serverPort = 8080;
int ProbeConfig::_vendorId  = 0x0403;
int ProbeConfig::_productId = 0x6001;

int     ProbeConfig::_cliDefaultLength = 256;
uint8_t ProbeConfig::_cliDefaultSlaveId = 1;
int     ProbeConfig::_modbusDefaultRegisterCount = 10;
int     ProbeConfig::_i2cRegisterDefaultEnd = 255;
int     ProbeConfig::_eepromDefaultAddrSize = 1;
uint32_t ProbeConfig::_gpioCaptureDefaultFrequency = 100000;
int      ProbeConfig::_gpioCaptureDefaultSampleCount = 1024;
int      ProbeConfig::_uartTerminalDefaultExpect = 64;
int      ProbeConfig::_uartTerminalDefaultTimeoutMs = 500;
int      ProbeConfig::_mavlinkDefaultTimeoutMs = 2000;
int      ProbeConfig::_nmeaDefaultTimeoutMs = 2000;
uint8_t  ProbeConfig::_pmbusDefaultAddress = 0x10;
std::string ProbeConfig::_fuzzDefaultMode = "scan";
uint8_t  ProbeConfig::_smbusDefaultCommand = 0;
std::string ProbeConfig::_smbusDefaultType = "byte";
std::string ProbeConfig::_smbusDefaultValue = "0";
std::string ProbeConfig::_pmbusDefaultType = "voltage";
uint32_t ProbeConfig::_sdcardDefaultBlock = 0;
int      ProbeConfig::_sensorDefaultChannel = -1;
int      ProbeConfig::_i2cRegisterDefaultStart = 0;
int      ProbeConfig::_modbusDefaultStart = 0;

uint8_t ProbeConfig::_eepromDefaultAddress = 0x50;
int     ProbeConfig::_eepromPageSizeSmall  = 8;
int     ProbeConfig::_eepromPageSizeLarge  = 32;
int     ProbeConfig::_eepromWriteDelayMs   = 10;

uint8_t ProbeConfig::_sensorI2cDefaultAddress = 0x48;
uint8_t ProbeConfig::_sensorChannelCount      = 4;

std::string ProbeConfig::_nmeaPrefix           = "$GP";
int         ProbeConfig::_nmeaSentenceMaxLength = 256;

uint8_t ProbeConfig::_sensorSpiReadMask = 0x80;

// FT232RL signal name → bit index (case-insensitive after normalisation)
const std::map<std::string, uint8_t> ProbeConfig::_pinNames = {
    {"TXD", 0}, {"RXD", 1}, {"RTS", 2}, {"CTS", 3},
    {"DTR", 4}, {"DSR", 5}, {"DCD", 6}, {"RI",  7},
    {"D0",  0}, {"D1",  1}, {"D2",  2}, {"D3",  3},
    {"D4",  4}, {"D5",  5}, {"D6",  6}, {"D7",  7},
};

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

static std::string toUpper(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c){ return (char)std::toupper(c); });
    return s;
}

uint8_t ProbeConfig::pinIndex(const std::string& name) {
    const std::string upper = toUpper(name);
    auto it = _pinNames.find(upper);
    if (it != _pinNames.end()) return it->second;
    // Accept bare integer strings ("0".."7")
    try {
        const size_t idx = std::stoul(name);
        if (idx < 8) return static_cast<uint8_t>(idx);
    } catch (...) {}
    throw std::invalid_argument("Unknown pin name: '" + name + "'. "
        "Use a signal label (TXD, RTS, DSR …) or a digit 0-7.");
}

// Read a string key and resolve it as a pin name/index
static uint8_t getPin(Poco::Util::AbstractConfiguration& cfg,
                      const std::string& key, uint8_t def) {
    if (!cfg.hasProperty(key)) return def;
    return ProbeConfig::pinIndex(cfg.getString(key));
}

// Read a string key and parse as unsigned integer (supports 0x prefix)
static uint32_t getUInt32(Poco::Util::AbstractConfiguration& cfg,
                          const std::string& key, uint32_t def) {
    if (!cfg.hasProperty(key)) return def;
    return static_cast<uint32_t>(std::stoul(cfg.getString(key), nullptr, 0));
}

// Read a string key and parse as signed integer (supports 0x prefix)
static int getInt(Poco::Util::AbstractConfiguration& cfg,
                  const std::string& key, int def) {
    if (!cfg.hasProperty(key)) return def;
    return static_cast<int>(std::stol(cfg.getString(key), nullptr, 0));
}

// ---------------------------------------------------------------------------
// load()
// ---------------------------------------------------------------------------

void ProbeConfig::load(Poco::Util::AbstractConfiguration& cfg) {
    Poco::Logger& log = Poco::Logger::get("ProbeConfig");

    // Pin assignments
    _uartTx  = getPin(cfg, "pins.uart.tx",   _uartTx);
    _uartRx  = getPin(cfg, "pins.uart.rx",   _uartRx);
    _spiMosi = getPin(cfg, "pins.spi.mosi",  _spiMosi);
    _spiMiso = getPin(cfg, "pins.spi.miso",  _spiMiso);
    _spiSck  = getPin(cfg, "pins.spi.sck",   _spiSck);
    _spiCs   = getPin(cfg, "pins.spi.cs",    _spiCs);
    _i2cScl  = getPin(cfg, "pins.i2c.scl",  _i2cScl);
    _i2cSda  = getPin(cfg, "pins.i2c.sda",  _i2cSda);

    // Transport defaults
    _uartBaud = getUInt32(cfg, "uart.default_baud",       _uartBaud);
    _spiFreq  = getUInt32(cfg, "spi.default_frequency_hz", _spiFreq);
    _i2cFreq  = getUInt32(cfg, "i2c.default_frequency_hz", _i2cFreq);

    // MAVLink
    _mavBaud   = getUInt32(cfg, "mavlink.baud",   _mavBaud);
    _mavSysId  = static_cast<uint8_t>(getUInt32(cfg, "mavlink.sysid",  _mavSysId));
    _mavCompId = static_cast<uint8_t>(getUInt32(cfg, "mavlink.compid", _mavCompId));

    // PMBus registers
    _pmbusVoutMode = static_cast<uint8_t>(getUInt32(cfg, "pmbus.register.vout_mode",  _pmbusVoutMode));
    _pmbusVout     = static_cast<uint8_t>(getUInt32(cfg, "pmbus.register.read_vout",  _pmbusVout));
    _pmbusIout     = static_cast<uint8_t>(getUInt32(cfg, "pmbus.register.read_iout",  _pmbusIout));
    _pmbusPout     = static_cast<uint8_t>(getUInt32(cfg, "pmbus.register.read_pout",  _pmbusPout));
    _pmbusTemp1    = static_cast<uint8_t>(getUInt32(cfg, "pmbus.register.read_temp1", _pmbusTemp1));

    // SD card
    _sdInitRetries  = getInt(cfg, "sdcard.init_retries",  _sdInitRetries);
    _sdReadRetries  = getInt(cfg, "sdcard.read_retries",  _sdReadRetries);
    _sdWriteRetries = getInt(cfg, "sdcard.write_retries", _sdWriteRetries);

    // Discovery
    _discoverySampleRate = static_cast<double>(
        getUInt32(cfg, "discovery.sample_rate_hz", static_cast<uint32_t>(_discoverySampleRate)));
    _discoveryDuration   = getInt(cfg, "discovery.default_duration_ms", _discoveryDuration);
    _discoveryUartSniffMs = getInt(cfg, "discovery.uart_sniff_ms",      _discoveryUartSniffMs);
    _discoveryUartProbeMs = getInt(cfg, "discovery.uart_probe_ms",      _discoveryUartProbeMs);

    // Device / server
    _serverPort = static_cast<unsigned short>(getUInt32(cfg, "server.port", _serverPort));
    _vendorId   = getInt(cfg, "device.vendor_id",  _vendorId);
    _productId  = getInt(cfg, "device.product_id", _productId);

    // CLI / API defaults
    _cliDefaultLength = getInt(cfg, "cli.default_length", _cliDefaultLength);
    _cliDefaultSlaveId = static_cast<uint8_t>(getUInt32(cfg, "cli.default_slave_id", _cliDefaultSlaveId));
    _modbusDefaultRegisterCount = getInt(cfg, "modbus.default_register_count", _modbusDefaultRegisterCount);
    _i2cRegisterDefaultEnd = getInt(cfg, "i2c.register_scan.default_end", _i2cRegisterDefaultEnd);
    _eepromDefaultAddrSize = getInt(cfg, "eeprom.i2c.default_addr_size", _eepromDefaultAddrSize);
    _gpioCaptureDefaultFrequency = getUInt32(cfg, "gpio.capture.default_frequency_hz", _gpioCaptureDefaultFrequency);
    _gpioCaptureDefaultSampleCount = getInt(cfg, "gpio.capture.default_sample_count", _gpioCaptureDefaultSampleCount);
    _uartTerminalDefaultExpect = getInt(cfg, "uart.terminal.default_expect", _uartTerminalDefaultExpect);
    _uartTerminalDefaultTimeoutMs = getInt(cfg, "uart.terminal.default_timeout_ms", _uartTerminalDefaultTimeoutMs);
    _mavlinkDefaultTimeoutMs = getInt(cfg, "mavlink.default_timeout_ms", _mavlinkDefaultTimeoutMs);
    _nmeaDefaultTimeoutMs = getInt(cfg, "nmea.default_timeout_ms", _nmeaDefaultTimeoutMs);
    _pmbusDefaultAddress = static_cast<uint8_t>(getUInt32(cfg, "pmbus.default_device_address", _pmbusDefaultAddress));
    if (cfg.hasProperty("fuzz.default_mode")) _fuzzDefaultMode = cfg.getString("fuzz.default_mode");
    _smbusDefaultCommand = static_cast<uint8_t>(getUInt32(cfg, "smbus.default_command", _smbusDefaultCommand));
    if (cfg.hasProperty("smbus.default_type")) _smbusDefaultType = cfg.getString("smbus.default_type");
    if (cfg.hasProperty("smbus.default_value")) _smbusDefaultValue = cfg.getString("smbus.default_value");
    if (cfg.hasProperty("pmbus.default_type")) _pmbusDefaultType = cfg.getString("pmbus.default_type");
    _sdcardDefaultBlock = getUInt32(cfg, "sdcard.default_block", _sdcardDefaultBlock);
    _sensorDefaultChannel = getInt(cfg, "sensor.default_channel", _sensorDefaultChannel);
    _i2cRegisterDefaultStart = getInt(cfg, "i2c.register_scan.default_start", _i2cRegisterDefaultStart);
    _modbusDefaultStart = getInt(cfg, "modbus.default_start", _modbusDefaultStart);

    // EEPROM
    _eepromDefaultAddress = static_cast<uint8_t>(getUInt32(cfg, "eeprom.i2c.default_address", _eepromDefaultAddress));
    _eepromPageSizeSmall  = getInt(cfg, "eeprom.i2c.page_size_small", _eepromPageSizeSmall);
    _eepromPageSizeLarge  = getInt(cfg, "eeprom.i2c.page_size_large", _eepromPageSizeLarge);
    _eepromWriteDelayMs   = getInt(cfg, "eeprom.i2c.write_delay_ms",  _eepromWriteDelayMs);

    // Generic I2C Sensor
    _sensorI2cDefaultAddress = static_cast<uint8_t>(getUInt32(cfg, "sensor.i2c.default_address",   _sensorI2cDefaultAddress));
    _sensorChannelCount      = static_cast<uint8_t>(getUInt32(cfg, "sensor.ads1x15.channel_count", _sensorChannelCount));

    // NMEA
    if (cfg.hasProperty("nmea.prefix")) _nmeaPrefix = cfg.getString("nmea.prefix");
    _nmeaSentenceMaxLength = getInt(cfg, "nmea.sentence_max_length", _nmeaSentenceMaxLength);

    // SPI Sensor
    _sensorSpiReadMask = static_cast<uint8_t>(getUInt32(cfg, "sensor.spi.read_mask", _sensorSpiReadMask));

    std::ostringstream msg;
    msg << "ProbeConfig loaded - "
        << "UART TX=D" << (int)_uartTx << "/RX=D" << (int)_uartRx
        << "  SPI MOSI=D" << (int)_spiMosi << "/MISO=D" << (int)_spiMiso
        << "/SCK=D" << (int)_spiSck << "/CS=D" << (int)_spiCs
        << "  I2C SCL=D" << (int)_i2cScl << "/SDA=D" << (int)_i2cSda;
    log.information(msg.str());
}

// ---------------------------------------------------------------------------
// Getters
// ---------------------------------------------------------------------------

uint8_t ProbeConfig::uartTxPin()    { return _uartTx;  }
uint8_t ProbeConfig::uartRxPin()    { return _uartRx;  }
uint8_t ProbeConfig::spiMosiPin()   { return _spiMosi; }
uint8_t ProbeConfig::spiMisoPin()   { return _spiMiso; }
uint8_t ProbeConfig::spiSckPin()    { return _spiSck;  }
uint8_t ProbeConfig::spiCsPin()     { return _spiCs;   }
uint8_t ProbeConfig::i2cSclPin()    { return _i2cScl;  }
uint8_t ProbeConfig::i2cSdaPin()    { return _i2cSda;  }

uint32_t ProbeConfig::uartDefaultBaud()     { return _uartBaud; }
uint32_t ProbeConfig::spiDefaultFrequency() { return _spiFreq;  }
uint32_t ProbeConfig::i2cDefaultFrequency() { return _i2cFreq;  }

uint32_t ProbeConfig::mavlinkBaud()    { return _mavBaud;   }
uint8_t  ProbeConfig::mavlinkSysId()   { return _mavSysId;  }
uint8_t  ProbeConfig::mavlinkCompId()  { return _mavCompId; }

uint8_t ProbeConfig::pmbusVoutMode()  { return _pmbusVoutMode; }
uint8_t ProbeConfig::pmbusReadVout()  { return _pmbusVout;     }
uint8_t ProbeConfig::pmbusReadIout()  { return _pmbusIout;     }
uint8_t ProbeConfig::pmbusReadPout()  { return _pmbusPout;     }
uint8_t ProbeConfig::pmbusReadTemp1() { return _pmbusTemp1;    }

int ProbeConfig::sdcardInitRetries()   { return _sdInitRetries;  }
int ProbeConfig::sdcardReadRetries()   { return _sdReadRetries;  }
int ProbeConfig::sdcardWriteRetries()  { return _sdWriteRetries; }

double ProbeConfig::discoverySampleRate()     { return _discoverySampleRate; }
int    ProbeConfig::discoveryDefaultDuration(){ return _discoveryDuration;  }
int    ProbeConfig::discoveryUartSniffMs()    { return _discoveryUartSniffMs; }
int    ProbeConfig::discoveryUartProbeMs()    { return _discoveryUartProbeMs; }

unsigned short ProbeConfig::serverPort()   { return _serverPort; }
int ProbeConfig::deviceVendorId()  { return _vendorId;  }
int ProbeConfig::deviceProductId() { return _productId; }

int     ProbeConfig::cliDefaultLength()            { return _cliDefaultLength; }
uint8_t ProbeConfig::cliDefaultSlaveId()           { return _cliDefaultSlaveId; }
int     ProbeConfig::modbusDefaultRegisterCount()  { return _modbusDefaultRegisterCount; }
int     ProbeConfig::i2cRegisterDefaultEnd()       { return _i2cRegisterDefaultEnd; }
int     ProbeConfig::eepromDefaultAddrSize()       { return _eepromDefaultAddrSize; }
uint32_t ProbeConfig::gpioCaptureDefaultFrequency(){ return _gpioCaptureDefaultFrequency; }
int      ProbeConfig::gpioCaptureDefaultSampleCount(){ return _gpioCaptureDefaultSampleCount; }
int      ProbeConfig::uartTerminalDefaultExpect()  { return _uartTerminalDefaultExpect; }
int      ProbeConfig::uartTerminalDefaultTimeoutMs(){ return _uartTerminalDefaultTimeoutMs; }
int      ProbeConfig::mavlinkDefaultTimeoutMs()    { return _mavlinkDefaultTimeoutMs; }
int      ProbeConfig::nmeaDefaultTimeoutMs()       { return _nmeaDefaultTimeoutMs; }
uint8_t  ProbeConfig::pmbusDefaultAddress()        { return _pmbusDefaultAddress; }
std::string ProbeConfig::fuzzDefaultMode()         { return _fuzzDefaultMode; }
uint8_t  ProbeConfig::smbusDefaultCommand()        { return _smbusDefaultCommand; }
std::string ProbeConfig::smbusDefaultType()        { return _smbusDefaultType; }
std::string ProbeConfig::smbusDefaultValue()       { return _smbusDefaultValue; }
std::string ProbeConfig::pmbusDefaultType()        { return _pmbusDefaultType; }
uint32_t ProbeConfig::sdcardDefaultBlock()         { return _sdcardDefaultBlock; }
int      ProbeConfig::sensorDefaultChannel()       { return _sensorDefaultChannel; }
int      ProbeConfig::i2cRegisterDefaultStart()    { return _i2cRegisterDefaultStart; }
int      ProbeConfig::modbusDefaultStart()         { return _modbusDefaultStart; }

uint8_t ProbeConfig::eepromDefaultAddress() { return _eepromDefaultAddress; }
int     ProbeConfig::eepromPageSizeSmall()  { return _eepromPageSizeSmall;  }
int     ProbeConfig::eepromPageSizeLarge()  { return _eepromPageSizeLarge;  }
int     ProbeConfig::eepromWriteDelayMs()   { return _eepromWriteDelayMs;   }

uint8_t ProbeConfig::sensorI2cDefaultAddress() { return _sensorI2cDefaultAddress; }
uint8_t ProbeConfig::sensorChannelCount()      { return _sensorChannelCount;      }

std::string ProbeConfig::nmeaPrefix()           { return _nmeaPrefix;            }
int         ProbeConfig::nmeaSentenceMaxLength() { return _nmeaSentenceMaxLength; }

uint8_t ProbeConfig::sensorSpiReadMask() { return _sensorSpiReadMask; }

} // namespace protocol_probe
