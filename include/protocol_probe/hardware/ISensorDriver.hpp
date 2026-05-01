#pragma once

#include <cstdint>
#include <map>
#include <string>
#include <vector>

namespace protocol_probe {
namespace hardware {

/**
 * Abstract interface for generic sensor drivers.
 * Implement this to add support for any I2C or SPI sensor.
 */
class ISensorDriver {
public:
    virtual ~ISensorDriver() = default;

    // Initialize the sensor; returns false if not present / not recognised
    virtual bool begin() = 0;

    // Read all available channels; key = channel name, value = converted reading
    virtual std::map<std::string, float> readAll() = 0;

    // Read a single channel by index
    virtual float readChannel(uint8_t channel) = 0;

    // Human-readable sensor identifier
    virtual std::string getSensorName() const = 0;

    // Number of channels the sensor exposes
    virtual uint8_t getChannelCount() const = 0;
};

} // namespace hardware
} // namespace protocol_probe
