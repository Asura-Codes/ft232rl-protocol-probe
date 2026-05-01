---
description: "Use when writing, reviewing, or modifying any C++ code, CMake files, or config files in the ProtocolProbe project. Covers project identity, build toolchain, architecture rules, Poco Logger constraints, CLI syntax, FT232RL pin naming, and naming conventions."
applyTo: ["src/**", "include/**", "CMakeLists.txt", "*.code-workspace", "Dockerfile.*"]
---

# ProtocolProbe — Project Context & Coding Guidelines

## Project Identity
- **Project name**: ProtocolProbe
- **Binary**: `protocol_probe` (Linux) / `protocol_probe.exe` (Windows)
- **C++ namespace**: `protocol_probe` (no legacy `ftdi_exploit` anywhere)
- **Class names**: `GpioProbe`, `I2cScanner`, `UartScanner` (never the old `*Fuzzer` names)

## Build & Toolchain
- **Language**: C++17
- **Build system**: CMake 3.15 + vcpkg (installed dir `./vcpkg_installed`)
- **Key libraries**: libftdi1, Poco (Net, JSON, Foundation, Util)

### Windows (MSVC x64, multi-config)
- **Triplet**: `x64-windows`
- **Configure**: `C:/CMake/bin/cmake.exe -S . -B build -DVCPKG_TARGET_TRIPLET=x64-windows -DVCPKG_INSTALLED_DIR=./vcpkg_installed`
- **Build Debug**: `C:/CMake/bin/cmake.exe --build build --config Debug`
- **Build Release**: `C:/CMake/bin/cmake.exe --build build --config Release`
- **Executable path (Debug)**: `build/bin/Debug/protocol_probe.exe`
- **Executable path (Release)**: `build/bin/Release/protocol_probe.exe`
- **Package**: `C:/CMake/bin/cpack.exe --config build/CPackConfig.cmake -C Release` → produces `ZIP` and `7Z`

### Linux (GCC x64, single-config Ninja)
- **Triplet**: `x64-linux`
- **Configure Debug**: `cmake -S . -B build -DCMAKE_TOOLCHAIN_FILE=/vcpkg/scripts/buildsystems/vcpkg.cmake -DVCPKG_TARGET_TRIPLET=x64-linux -DVCPKG_INSTALLED_DIR=./vcpkg_installed -DCMAKE_BUILD_TYPE=Debug`
- **Configure Release**: same as above with `-DCMAKE_BUILD_TYPE=Release`
- **Build**: `cmake --build build` (no `--config` flag needed; single-config Ninja uses `CMAKE_BUILD_TYPE`)
- **Executable path**: `build/bin/protocol_probe` (no Debug/Release subfolder — single-config generator)
- **Package**: `cpack --config build/CPackConfig.cmake` → produces `TGZ`

### Docker images
Two `Dockerfile`s are provided at repo root and can be built via VS Code tasks:
- **`Docker: Build protocolprobe-ubuntu`** — builds `protocolprobe-ubuntu` from `Dockerfile.ubuntu` (Linux/x64)
- **`Docker: Build protocolprobe-windows`** — builds `protocolprobe-windows` from `Dockerfile.windows` (Windows containers host required)

## Architecture

### Config-driven — no hardcoded values
Every configurable value (pins, baud rates, retry counts, register addresses, server port…) must:
1. Be declared in `protocol_probe.properties` with a descriptive key.
2. Be exposed through a typed static getter in `ProbeConfig` (`include/protocol_probe/Config.hpp` / `src/Config.cpp`).
3. Never be written as a literal constant in protocol implementation files or handlers.

`ProbeConfig::load()` is called once in `FtdiUnifiedApp::initialize()` after `loadConfiguration()`.  
CLI option callbacks may override individual values after load; that override pattern is intentional.

### Application structure
- `FtdiUnifiedApp` extends `Poco::Util::ServerApplication`
- `initialize()` → `loadConfiguration()` → `ProbeConfig::load(config())` → `ServerApplication::initialize(self)`
- Unified binary: `--mode server` starts REST, default mode runs CLI
- REST routes live in `src/api/Handlers.cpp`

## FT232RL Hardware

### Bit-bang mode
- Synchronous bit-bang: `BITMODE_SYNCBB = 0x04`
- Reset: `BITMODE_RESET = 0x00`
- Baud rate divisor: `setBaudRate(desired_hz / 16)`
- `transfer()` uses a 500 ms `steady_clock` deadline

### Pin naming convention
Always use **FT232RL physical signal labels** (not `D0`–`D7` integers) in config keys, comments, and documentation:

| Signal | Bit | Default role |
|--------|-----|--------------|
| TXD    | 0   | UART TX      |
| RXD    | 1   | UART RX      |
| RTS    | 2   | SPI MOSI     |
| CTS    | 3   | SPI MISO     |
| DTR    | 4   | SPI SCK      |
| DSR    | 5   | I2C SCL      |
| DCD    | 6   | I2C SDA      |
| RI     | 7   | SPI CS/NSS   |

`ProbeConfig::pinIndex(name)` converts a signal name or bare digit string to a bit index.

## Poco Logger Rules

**Never use printf-style format specifiers (`%d`, `%02x`, `%s`, …) in Poco Logger calls.**  
Poco's logger does not support them and will misformat or crash.

Instead, always build the message with `std::ostringstream`:

```cpp
// WRONG
logger.information("Address: 0x%02x, value=%d", addr, val);

// CORRECT
std::ostringstream msg;
msg << "Address: 0x" << std::hex << std::setw(2) << std::setfill('0') << (int)addr
    << ", value=" << std::dec << val;
logger.information(msg.str());
```

## Logger Naming Convention
Logger names follow a dotted hierarchy matching the module path:

| Module folder        | Logger name pattern           |
|----------------------|-------------------------------|
| `src/hardware/`      | `"hardware.ClassName"`        |
| `src/exploits/`      | `"exploits.ClassName"`        |
| `src/api/`           | `"api.ClassName"`             |
| `src/main.cpp`       | `"main"` or `"server"`        |
| `src/Config.cpp`     | `"ProbeConfig"`               |

## CLI Syntax
All CLI options use the `/option:value` format (Poco `ServerApplication` convention):

```
protocol_probe.exe /command:discover /duration:500
protocol_probe.exe /command:sd-read /address:0 /mosi:2 /miso:3 /sck:4 /cs:7
```

Never use `--option value` or `-o value` in help text, examples, or new option definitions.

## REST API (Poco HTTP Handlers)

All REST handlers live in `src/api/Handlers.cpp` / `include/protocol_probe/api/Handlers.hpp`.

**Response format** — always JSON, always chunked:
```cpp
response.setChunkedTransferEncoding(true);
response.setContentType("application/json");
Poco::JSON::Object::Ptr json = new Poco::JSON::Object();
// ... populate json ...
json->stringify(response.send());
```

**Query / form parameters** — read via `Poco::Net::HTMLForm`:
```cpp
Poco::Net::HTMLForm form(request);
if (form.has("slave")) addr = (uint8_t)std::stoul(form.get("slave"), nullptr, 0);
```

**Pin defaults** — `parseSpiPins`, `parseI2cPins`, `parseUartPins` helpers must fall back to `ProbeConfig::spiMosiPin()` etc. when the form field is absent. Never hardcode `2`, `3`, `5`, `6`, `7` etc. as pin defaults in handler code.

**Error handling** — catch `std::exception`, set `"error"` key in JSON, set HTTP 500:
```cpp
try {
    // ...
} catch (const std::exception& e) {
    response.setStatusAndReason(HTTPResponse::HTTP_INTERNAL_SERVER_ERROR);
    json->set("error", e.what());
}
```

**Logger** — use `Poco::Logger::get("api")` for all handler log messages.

## Protocol Implementation Notes
- **SPI SD card**: SDHC uses block addressing; SDSC uses `block × 512`. CRC: CMD0=`0x95`, CMD8=`0x87`, all others=`0x01`.
- **PMBus**: VOUT decodes as L16 (`raw × 2^N`, N from `VOUT_MODE` reg). IOUT/POUT/TEMP decode as L11 (5-bit signed exponent, 11-bit signed mantissa).
- **MAVLink v1**: X.25 CRC16, `CRC_EXTRA=50` for HEARTBEAT (`msg_id=0`). Total frame = 17 bytes.

## Design Principles
- Follow **SOLID**, **DRY**, and **KISS**
- Favor **composition over inheritance**
- Maintain **clear abstraction layers**: hardware (FTDI/pin control) → transport (UART/SPI/I2C) → protocol → REST/CLI
- Design for extensibility: new transports and higher-level protocols must be addable without modifying existing layers

## Supported Protocols

### Transport layer
UART · SPI · I2C

### Higher-level protocols
| Category | Protocols |
|---|---|
| Memory / Storage / Sensors | NOR flash, SD card, Generic sensors |
| Communication | Modbus RTU, MAVLink, NMEA |
| I2C-derived | SMBus, PMBus, EEPROM |

## Test Device (Reference Hardware)
The validation device is connected simultaneously on all three transports using the standard pin mapping above.

**Confirmed capabilities:** NOR flash, Modbus RTU, EEPROM.  
These are used to validate pin-level discovery, transport detection, and higher-level protocol identification end-to-end.
