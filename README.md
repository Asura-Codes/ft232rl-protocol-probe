# FT232RL Protocol Probe (Synchronous Bit-Bang)

> **Alpha / experimental** — It's difficult to create such a flexible tool and expect it to work perfectly in every situation.

A small C++ tool that turns FT232RL breakout board (e.g. the **HW-417**) into a protocol probe. Started as a low-level bit-banging challenge.

**Synchronous Bit-Bang** mode pushes a pre-computed byte stream into the FT232RL's hardware buffer, giving deterministic timing without OS jitter. 
Works for I2C, SPI, UART, and several higher-level protocols.

- **Pin discovery** — `discover` tries every pin combination with real protocol traffic (I2C ping, SPI JEDEC ID, Modbus RTU) and reports what it found.
- **Flexible pin assignments** — every command accepts explicit pin overrides.
- **CLI and REST API** — same binary, two modes.

## Hardware Connection Guide (HW-417)

Bit indices map to physical pins as follows:

| Bit | FT232RL Pin | HW-417 Label | Default role |
| :-- | :---------- | :----------- | :----------- |
| D0  | TXD         | TX           | UART TX      |
| D1  | RXD         | RX           | UART RX      |
| D2  | RTS         | RTS          | SPI MOSI     |
| D3  | CTS         | CTS          | SPI MISO     |
| D4  | DTR         | DTR          | SPI SCK      |
| D5  | DSR         | DSR          | I2C SCL      |
| D6  | DCD         | DCD          | I2C SDA      |
| D7  | RI          | RI           | SPI CS       |

> [!CAUTION]
> **Voltage Selection:** The HW-417 has a jumper for **3.3 V** or **5 V**. Most SPI Flash (e.g. Winbond) and I2C EEPROMs are **3.3 V only**. Verify your target voltage before connecting.

> [!NOTE]
> **I2C pull-ups:** The FT232RL GPIO pins are push-pull, not open-drain. SCL and SDA lines require external pull-up resistors (typically 4.7 kΩ to 3.3 V) for I2C to work correctly.

## Pin Discovery Workflow

If you are not sure which wire goes where, run `discover`. It does a **passive capture** then an **active probe** across all pin combinations:

```powershell
protocol_probe.exe /command:discover /duration:500
```

Example output:
```
Found 3 result(s):
  Protocol: I2C
    scl: D5
    sda: D6
    device_count: 1
    devices: 0x50
    higher_protocols: EEPROM@0x50
    method: active
    CLI hint: /scl:5 /sda:6
  Protocol: SPI
    cs: D7
    miso: D3
    mosi: D2
    sck: D4
    device_type: SD_card
    method: active
    CLI hint: /mosi:2 /miso:3 /sck:4 /cs:7
  Protocol: UART
    rx: D1
    tx: D0
    baud: 115200
    higher_protocol: Modbus_RTU
    method: active
    CLI hint: /tx:0 /rx:1
```

Copy the **CLI hint** into your next command.

## CLI Reference

```powershell
protocol_probe.exe /command:<cmd> [options]
```

### General options

| Option | Default | Description |
| :----- | :------ | :---------- |
| `/command:<cmd>` | — | Command to run (see below) |
| `/duration:<ms>` | 500 | Capture/timeout duration |
| `/baud:<rate>` | 9600 | UART baud rate |
| `/address:<hex>` | 0 | Register or memory address (hex supported: `0x1A`) |
| `/length:<n>` | 256 | Byte count |
| `/slave:<hex>` | — | I2C device address or Modbus slave ID (hex supported) |
| `/regs:<n>` | 10 | Register count (Modbus, SMBus, sensor, etc.) |
| `/payload:<hex>` | — | Hex byte string for writes/sends (e.g. `DEADBEEF`) |
| `/port:<n>` | 8080 | HTTP server port (server mode only) |
| `/verbose` | off | Enable trace-level logging |

### Pin override options

All pin values are **bit indices 0–7** (D0–D7).

| Option | Default | Description |
| :----- | :------ | :---------- |
| `/tx:<n>` | 0 | UART TX pin |
| `/rx:<n>` | 1 | UART RX pin |
| `/mosi:<n>` | 2 | SPI MOSI pin |
| `/miso:<n>` | 3 | SPI MISO pin |
| `/sck:<n>` | 4 | SPI SCK pin |
| `/cs:<n>` | 7 | SPI CS pin |
| `/scl:<n>` | 5 | I2C SCL pin |
| `/sda:<n>` | 6 | I2C SDA pin |

### Commands

#### Discovery

| Command | Key options | Description |
| :------ | :---------- | :---------- |
| `status` | — | Check FT232RL is connected |
| `discover` | `/duration` | Auto-detect all protocols and pin assignments |

#### SPI transport

| Command | Key options | Description |
| :------ | :---------- | :---------- |
| `spi-id` | `/mosi /miso /sck /cs` | Read SPI flash JEDEC ID |
| `spi-read` | `/address /length /mosi /miso /sck /cs` | Read bytes from SPI flash |
| `spi-write` | `/address /payload /mosi /miso /sck /cs` | Write hex payload to SPI flash |
| `spi-erase` | `/address /length /mosi /miso /sck /cs` | Erase SPI flash sectors |
| `sd-read` | `/address /mosi /miso /sck /cs` | Read SD card block (address = block number) |
| `sd-write` | `/address /payload /mosi /miso /sck /cs` | Write SD card block (exactly 512 bytes = 1024 hex chars; shorter payloads are padded with 0xFF) |
| `spi-sensor` | `/address /regs /mosi /miso /sck /cs` | Read registers from generic SPI sensor (`/address` = start register, `/regs` = number of registers) |

#### I2C transport

| Command | Key options | Description |
| :------ | :---------- | :---------- |
| `i2c-scan` | `/scl /sda` | Scan for I2C devices (0x01–0x7E) |
| `i2c-read` | `/slave /address /length /scl /sda` | Read bytes from I2C device register |
| `i2c-write` | `/slave /address /payload /scl /sda` | Write hex payload to I2C device register |
| `i2c-erase` | `/slave /address /length /scl /sda` | Fill I2C EEPROM region with 0xFF |
| `i2c-fuzz` | `/scl /sda` | Fuzz I2C bus (scan all addresses) |
| `i2c-registers` | `/slave /address /regs /scl /sda` | Scan I2C device registers |
| `i2c-eeprom` | `/slave /address /length /scl /sda` | Read EEPROM (default addr 0x50); add `/payload` to write then verify |
| `smbus-read` | `/slave /address /regs /scl /sda` | Read SMBus byte (`/regs:1`) or word (`/regs:2`) |
| `smbus-write` | `/slave /address /payload /scl /sda` | Write SMBus byte (2 hex chars) or word (4 hex chars) |
| `pmbus-read` | `/slave /address /scl /sda` | Read PMBus VOUT, IOUT, POUT and TEMP1 |
| `sensor-read` | `/slave /scl /sda` | Read all channels of a generic I2C sensor (ADS1x15-compatible, default addr 0x48) |

#### UART transport

| Command | Key options | Description |
| :------ | :---------- | :---------- |
| `uart-scan` | `/duration` | Auto-detect UART baud rate |
| `uart-send` | `/baud /payload /tx /rx` | Send raw hex bytes |
| `uart-receive` | `/baud /length /duration /tx /rx` | Receive raw bytes |
| `uart-fuzz` | `/baud /payload /tx /rx` | Inject malformed UART frame |
| `uart-terminal` | `/baud /payload /regs /duration /tx /rx` | Send bytes and print raw response |
| `modbus-read` | `/slave /address /regs /baud /tx /rx` | Read Modbus holding registers (FC03) |
| `modbus-write` | `/slave /address /payload /baud /tx /rx` | Write single register FC06 (payload = 4 hex digits) |
| `modbus-write-multi` | `/slave /address /payload /baud /tx /rx` | Write multiple registers FC16 (payload = N×4 hex digits) |
| `nmea` | `/baud /duration /tx /rx` | Receive a NMEA sentence (prefix from config, default `$GP`) |
| `mavlink-rx` | `/baud /duration /tx /rx` | Receive one MAVLink packet |
| `mavlink-heartbeat` | `/baud /tx /rx` | Send a MAVLink heartbeat |

#### GPIO

| Command | Key options | Description |
| :------ | :---------- | :---------- |
| `gpio-scan` | `/duration` | Report GPIO pin activity and toggle counts |
| `gpio-capture` | `/baud /length` | Logic-analyzer capture (`/baud` = sample frequency in Hz) |
| `gpio-crosstalk` | — | Scan for inter-pin crosstalk |

### Examples

```powershell
# Discover everything automatically
protocol_probe.exe /command:discover /duration:500

# Read SPI flash ID using discovered pins
protocol_probe.exe /command:spi-id /mosi:2 /miso:3 /sck:4 /cs:7

# Read 256 bytes from SPI flash at address 0x1000
protocol_probe.exe /command:spi-read /address:0x1000 /length:256

# Write 4 bytes to SPI flash at address 0x1000
protocol_probe.exe /command:spi-write /address:0x1000 /payload:DEADBEEF /mosi:2 /miso:3 /sck:4 /cs:7

# Erase first 4 KB sector of SPI flash
protocol_probe.exe /command:spi-erase /address:0 /length:4096 /mosi:2 /miso:3 /sck:4 /cs:7

# Read 8 registers from a SPI sensor starting at register 0x00
protocol_probe.exe /command:spi-sensor /address:0 /regs:8 /mosi:2 /miso:3 /sck:4 /cs:7

# Read SD card block 0
protocol_probe.exe /command:sd-read /address:0 /mosi:2 /miso:3 /sck:4 /cs:7

# Write 512 bytes to SD card block 0 (payload = 1024 hex chars; pad with FF if shorter)
protocol_probe.exe /command:sd-write /address:0 /payload:DEADBEEF /mosi:2 /miso:3 /sck:4 /cs:7

# Scan I2C bus using discovered pins
protocol_probe.exe /command:i2c-scan /scl:5 /sda:6

# Read 256 bytes from I2C EEPROM at 0x50
protocol_probe.exe /command:i2c-read /slave:0x50 /address:0 /length:256 /scl:5 /sda:6

# Write 4 bytes to EEPROM at offset 0x10
protocol_probe.exe /command:i2c-write /slave:0x50 /address:0x10 /payload:DEADBEEF /scl:5 /sda:6

# Read EEPROM via i2c-eeprom (uses default address 0x50 if /slave is omitted)
protocol_probe.exe /command:i2c-eeprom /length:32 /scl:5 /sda:6

# Write and verify EEPROM via i2c-eeprom
protocol_probe.exe /command:i2c-eeprom /slave:0x50 /address:0x10 /payload:DEADBEEF /scl:5 /sda:6

# Read PMBus device at address 0x11
protocol_probe.exe /command:pmbus-read /slave:0x11 /scl:5 /sda:6

# Read all channels of a generic I2C sensor (default 0x48)
protocol_probe.exe /command:sensor-read /scl:5 /sda:6

# Read 5 Modbus holding registers from slave 1 at 115200 baud
protocol_probe.exe /command:modbus-read /slave:1 /address:0 /regs:5 /baud:115200 /tx:0 /rx:1

# Write value 0x1234 to Modbus register 5 on slave 1
protocol_probe.exe /command:modbus-write /slave:1 /address:5 /payload:1234 /baud:115200 /tx:0 /rx:1

# Write three Modbus registers (0x0001 0x0002 0x0003) starting at reg 0
protocol_probe.exe /command:modbus-write-multi /slave:1 /address:0 /payload:000100020003 /baud:115200

# Write a byte to SMBus register 0x01 on device 0x48
protocol_probe.exe /command:smbus-write /slave:0x48 /address:0x01 /payload:FF /scl:5 /sda:6

# Receive a NMEA sentence (device transmitting at 115200 baud)
protocol_probe.exe /command:nmea /baud:115200 /duration:3000 /tx:0 /rx:1

# Receive a MAVLink packet
protocol_probe.exe /command:mavlink-rx /baud:115200 /duration:3000 /tx:0 /rx:1

# Send raw bytes over UART
protocol_probe.exe /command:uart-send /baud:115200 /payload:0103000000050B02 /tx:0 /rx:1

# Receive up to 64 bytes over UART with 500 ms timeout
protocol_probe.exe /command:uart-receive /baud:115200 /length:64 /duration:500 /tx:0 /rx:1
```

## REST API Reference

Server mode is the default when `/command:` is omitted:

```powershell
protocol_probe.exe /port:8080
```

Base URL: `http://localhost:8080`. All endpoints accept pin overrides as query params (e.g. `?scl=5&sda=6`).

### Device Management

| Endpoint | Method | Description |
| :------- | :----- | :---------- |
| `/api/device/status` | `GET` | Check if the FT232RL is connected |
| `/api/discover` | `POST` | Passive + active pin discovery (`duration` param, default 500 ms) |

### SPI Operations

| Endpoint | Method | Parameters | Description |
| :------- | :----- | :--------- | :---------- |
| `/api/spi/id` | `GET` | `mosi`, `miso`, `sck`, `cs`, `frequency` | SPI flash JEDEC ID |
| `/api/spi/data` | `GET` | `address`, `length`, pin params | Read SPI flash |
| `/api/flash/write` | `POST` | `address`, binary body, pin params | Write SPI flash |
| `/api/flash/erase` | `POST` | `address`, `length`, pin params | Erase SPI flash sectors |
| `/api/spi/sd` | `GET` | `block`, `frequency`, pin params | Read SD card block |
| `/api/spi/sd` | `POST` | `block`, `payload` (hex, exactly 512 bytes), pin params | Write SD card block |

### I2C Operations

| Endpoint | Method | Parameters | Description |
| :------- | :----- | :--------- | :---------- |
| `/api/i2c/scan` | `GET` | `scl`, `sda` | Scan for I2C devices (0x01–0x7E) |
| `/api/i2c/data` | `GET` | `device`, `address`, `length`, `addr_size`, `scl`, `sda` | Read I2C EEPROM |
| `/api/i2c/data` | `POST` | `device`, `address`, binary body, `addr_size`, `scl`, `sda` | Write I2C EEPROM |
| `/api/i2c/registers` | `GET` | `device`, `start`, `end`, `addr_size`, `scl`, `sda` | Scan device registers |
| `/api/smbus/register` | `GET` | `device`, `command`, `type` (`byte`\|`word`), `scl`, `sda` | SMBus read |
| `/api/smbus/register` | `POST` | `device`, `command`, `type`, `value`, `scl`, `sda` | SMBus write |
| `/api/pmbus/register` | `GET` | `device`, `command`, `type` (`voltage`\|`current`\|`power`\|`temperature`), `scl`, `sda` | PMBus single measurement |
| `/api/sensor/read` | `GET` | `device`, `channel` (omit for all channels), `scl`, `sda` | Generic I2C sensor read |

### UART Operations

| Endpoint | Method | Parameters | Description |
| :------- | :----- | :--------- | :---------- |
| `/api/uart/terminal` | `POST` | `baud`, `payload` (hex), `expect`, `timeout`, `tx`, `rx` | Send bytes and receive response |
| `/api/uart/send` | `POST` | `baud`, `payload` (hex), `tx`, `rx` | Send raw bytes |
| `/api/uart/modbus` | `GET` | `slave`, `start`, `count`, `baud`, `tx`, `rx` | Modbus FC03 read |
| `/api/uart/modbus` | `POST` | `slave`, `start`, `payload` (FC06: 4 hex chars / FC16: N×4 hex chars), `baud`, `tx`, `rx` | Modbus write |
| `/api/uart/nmea` | `GET` | `baud`, `prefix`, `timeout`, `tx`, `rx` | Receive NMEA sentence |
| `/api/uart/mavlink` | `GET` | `baud`, `timeout`, `tx`, `rx` | Receive MAVLink packet |
| `/api/uart/mavlink` | `POST` | `baud`, `tx`, `rx` | Send MAVLink heartbeat |

### GPIO

| Endpoint | Method | Parameters | Description |
| :------- | :----- | :--------- | :---------- |
| `/api/gpio/scan` | `GET` | `duration` | GPIO pin activity scan |
| `/api/gpio/capture` | `POST` | `frequency`, `count` | Logic-analyzer style capture |

### Fuzzing

| Endpoint | Method | Parameters | Description |
| :------- | :----- | :--------- | :---------- |
| `/api/fuzz/uart` | `POST` | `baud`, `payload` (hex), `tx`, `rx` | Inject malformed UART frame |
| `/api/fuzz/i2c` | `POST` | `scl`, `sda` | I2C address fuzz scan |

## Building

It's integrated with VS Code. 
The `Windows.code-workspace` file contains the necessary CMake configuration for Windows, including vcpkg integration.
The Windows Workspace contains a hardcoded path to the vcpkg toolchain file. You may need to edit it to match your setup.
The `Ubuntu.code-workspace` file is set up for Linux development, also with vcpkg (defined for the Dev Container).
It's easier to use a Dev Container for Linux. It has vcpkg already set up and ready to go.
I usually run tasks from the sidebar using the Task Runner extension, but this can also be done from the Command Palette (`Ctrl+Shift+P`).

