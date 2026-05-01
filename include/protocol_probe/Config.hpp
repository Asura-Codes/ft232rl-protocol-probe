#pragma once

#include <Poco/Util/AbstractConfiguration.h>
#include <cstdint>
#include <string>
#include <map>

namespace protocol_probe {

/**
 * Central configuration accessor for ProtocolProbe.
 *
 * Call ProbeConfig::load() once inside Application::initialize(), after
 * calling loadConfiguration().  All subsystems then read their defaults
 * through the typed static getters; CLI options override those values
 * afterwards in the option callbacks.
 *
 * Pin names follow FT232RL physical signal labels:
 *   TXD=D0  RXD=D1  RTS=D2  CTS=D3  DTR=D4  DSR=D5  DCD=D6  RI=D7
 *
 * Both symbolic names (case-insensitive) and bare digit strings ("0".."7")
 * are accepted wherever a pin is specified.
 */
class ProbeConfig {
public:
    /// Populate all cached values from a Poco configuration instance.
    /// Must be called before any getter is used.
    static void load(Poco::Util::AbstractConfiguration& config);

    /// Resolve an FT232RL signal name or bit-index string to a bit index.
    /// Throws std::invalid_argument for unrecognised names.
    static uint8_t pinIndex(const std::string& name);

    // ---- Default pin assignments ----------------------------------------
    static uint8_t uartTxPin();
    static uint8_t uartRxPin();
    static uint8_t spiMosiPin();
    static uint8_t spiMisoPin();
    static uint8_t spiSckPin();
    static uint8_t spiCsPin();
    static uint8_t i2cSclPin();
    static uint8_t i2cSdaPin();

    // ---- Transport defaults --------------------------------------------
    static uint32_t uartDefaultBaud();
    static uint32_t spiDefaultFrequency();
    static uint32_t i2cDefaultFrequency();

    // ---- MAVLink -------------------------------------------------------
    static uint32_t mavlinkBaud();
    static uint8_t  mavlinkSysId();
    static uint8_t  mavlinkCompId();

    // ---- PMBus registers -----------------------------------------------
    static uint8_t pmbusVoutMode();
    static uint8_t pmbusReadVout();
    static uint8_t pmbusReadIout();
    static uint8_t pmbusReadPout();
    static uint8_t pmbusReadTemp1();

    // ---- SD card -------------------------------------------------------
    static int sdcardInitRetries();
    static int sdcardReadRetries();
    static int sdcardWriteRetries();

    // ---- Protocol discovery --------------------------------------------
    static double discoverySampleRate();
    static int    discoveryDefaultDuration();

    // ---- Device / server -----------------------------------------------
    static unsigned short serverPort();
    static int  deviceVendorId();
    static int  deviceProductId();

    // ---- CLI / API defaults --------------------------------------------
    static int     cliDefaultLength();
    static uint8_t cliDefaultSlaveId();
    static int     modbusDefaultRegisterCount();
    static int     i2cRegisterDefaultEnd();
    static int     eepromDefaultAddrSize();
    static uint32_t gpioCaptureDefaultFrequency();
    static int      gpioCaptureDefaultSampleCount();
    static int      uartTerminalDefaultExpect();
    static int      uartTerminalDefaultTimeoutMs();
    static int      mavlinkDefaultTimeoutMs();
    static int      nmeaDefaultTimeoutMs();
    static uint8_t  pmbusDefaultAddress();
    static std::string fuzzDefaultMode();
    static uint8_t  smbusDefaultCommand();
    static std::string smbusDefaultType();
    static std::string smbusDefaultValue();
    static std::string pmbusDefaultType();
    static uint32_t sdcardDefaultBlock();
    static int      sensorDefaultChannel();
    static int      i2cRegisterDefaultStart();
    static int      modbusDefaultStart();

    // ---- I2C EEPROM ----------------------------------------------------
    static uint8_t eepromDefaultAddress();
    static int     eepromPageSizeSmall();
    static int     eepromPageSizeLarge();
    static int     eepromWriteDelayMs();

    // ---- Generic I2C Sensor --------------------------------------------
    static uint8_t sensorI2cDefaultAddress();
    static uint8_t sensorChannelCount();

    // ---- NMEA ----------------------------------------------------------
    static std::string nmeaPrefix();
    static int         nmeaSentenceMaxLength();

    // ---- SPI Sensor ----------------------------------------------------
    static uint8_t sensorSpiReadMask();

private:
    static const std::map<std::string, uint8_t> _pinNames;

    // Cached values — initialised to the same built-in defaults that were
    // previously hardcoded in main.cpp so the app works without a config file.
    static uint8_t _uartTx, _uartRx;
    static uint8_t _spiMosi, _spiMiso, _spiSck, _spiCs;
    static uint8_t _i2cScl, _i2cSda;

    static uint32_t _uartBaud;
    static uint32_t _spiFreq;
    static uint32_t _i2cFreq;

    static uint32_t _mavBaud;
    static uint8_t  _mavSysId;
    static uint8_t  _mavCompId;

    static uint8_t _pmbusVoutMode;
    static uint8_t _pmbusVout;
    static uint8_t _pmbusIout;
    static uint8_t _pmbusPout;
    static uint8_t _pmbusTemp1;

    static int _sdInitRetries;
    static int _sdReadRetries;
    static int _sdWriteRetries;

    static double _discoverySampleRate;
    static int    _discoveryDuration;

    static unsigned short _serverPort;
    static int _vendorId;
    static int _productId;

    static int     _cliDefaultLength;
    static uint8_t _cliDefaultSlaveId;
    static int     _modbusDefaultRegisterCount;
    static int     _i2cRegisterDefaultEnd;
    static int     _eepromDefaultAddrSize;
    static uint32_t _gpioCaptureDefaultFrequency;
    static int      _gpioCaptureDefaultSampleCount;
    static int      _uartTerminalDefaultExpect;
    static int      _uartTerminalDefaultTimeoutMs;
    static int      _mavlinkDefaultTimeoutMs;
    static int      _nmeaDefaultTimeoutMs;
    static uint8_t  _pmbusDefaultAddress;
    static std::string _fuzzDefaultMode;
    static uint8_t  _smbusDefaultCommand;
    static std::string _smbusDefaultType;
    static std::string _smbusDefaultValue;
    static std::string _pmbusDefaultType;
    static uint32_t _sdcardDefaultBlock;
    static int      _sensorDefaultChannel;
    static int      _i2cRegisterDefaultStart;
    static int      _modbusDefaultStart;

    static uint8_t _eepromDefaultAddress;
    static int     _eepromPageSizeSmall;
    static int     _eepromPageSizeLarge;
    static int     _eepromWriteDelayMs;

    static uint8_t _sensorI2cDefaultAddress;
    static uint8_t _sensorChannelCount;

    static std::string _nmeaPrefix;
    static int         _nmeaSentenceMaxLength;

    static uint8_t _sensorSpiReadMask;
};

} // namespace protocol_probe
