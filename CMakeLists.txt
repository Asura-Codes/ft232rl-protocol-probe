cmake_minimum_required(VERSION 3.15)
project(ProtocolProbe LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Platform-aware vcpkg installed directory (used only for Windows DLL install)
if(WIN32)
    set(VCPKG_TRIPLET_DIR "${CMAKE_CURRENT_SOURCE_DIR}/vcpkg_installed/x64-windows")
endif()

# Find LibFTDI1 and Poco via vcpkg imported targets
find_package(LibFTDI1 CONFIG REQUIRED)
find_package(Poco CONFIG REQUIRED COMPONENTS Foundation Net JSON Util)

# Source files organized by module
set(SOURCES
    src/main.cpp
    src/CliRunner.cpp
    src/Config.cpp
    src/api/Handlers.cpp
    src/hardware/FtdiDevice.cpp
    src/hardware/SpiInterface.cpp
    src/hardware/I2cInterface.cpp
    src/hardware/UartInterface.cpp
    src/exploits/SpiFlash.cpp
    src/exploits/I2cEeprom.cpp
    src/exploits/GpioProbe.cpp
    src/exploits/UartScanner.cpp
    src/exploits/I2cScanner.cpp
    src/exploits/ProtocolDiscovery.cpp
    src/exploits/ModbusExploit.cpp
    src/exploits/MavLinkExploit.cpp
    src/exploits/NmeaExploit.cpp
    src/exploits/SmBusExploit.cpp
    src/exploits/PmBusExploit.cpp
    src/exploits/SdCardExploit.cpp
    src/exploits/SpiSensor.cpp
    src/exploits/GenericSensor.cpp
)

add_executable(protocol_probe ${SOURCES})

target_include_directories(protocol_probe PRIVATE 
    "${CMAKE_CURRENT_SOURCE_DIR}/include"
    ${LIBFTDI_INCLUDE_DIRS}
)

if(DEFINED LIBFTDI_LIBRARY_DIRS)
    # Ensure the target knows where to find the LibFTDI libraries (ftdi1.lib)
    target_link_directories(protocol_probe PRIVATE ${LIBFTDI_LIBRARY_DIRS})
endif()

target_link_libraries(protocol_probe PRIVATE 
    ${LIBFTDI_LIBRARIES}
    Poco::Net Poco::JSON Poco::Foundation Poco::Util
)

# Installation rules
install(TARGETS protocol_probe
    RUNTIME DESTINATION bin
)

# Install required DLLs for runtime (Windows only)
if(WIN32)
install(FILES 
    "${VCPKG_TRIPLET_DIR}/bin/ftdi1.dll"
    "${VCPKG_TRIPLET_DIR}/bin/libexpat.dll"
    "${VCPKG_TRIPLET_DIR}/bin/libusb-1.0.dll"
    "${VCPKG_TRIPLET_DIR}/bin/pcre2-8.dll"
    "${VCPKG_TRIPLET_DIR}/bin/PocoNet.dll"
    "${VCPKG_TRIPLET_DIR}/bin/PocoJSON.dll"
    "${VCPKG_TRIPLET_DIR}/bin/PocoFoundation.dll"
    "${VCPKG_TRIPLET_DIR}/bin/PocoUtil.dll"
    "${VCPKG_TRIPLET_DIR}/bin/PocoXML.dll"
    "${VCPKG_TRIPLET_DIR}/bin/utf8proc.dll"
    "${VCPKG_TRIPLET_DIR}/bin/zlib1.dll"
    DESTINATION bin
)
endif()

# CPack configuration
set(CPACK_PACKAGE_NAME "ProtocolProbe")
set(CPACK_PACKAGE_VENDOR "Dev")
set(CPACK_PACKAGE_VERSION "0.1.3")
if(WIN32)
    set(CPACK_GENERATOR "ZIP")
else()
    set(CPACK_GENERATOR "TGZ")
endif()
set(CPACK_PACKAGE_INSTALL_DIRECTORY "ProtocolProbe")
include(CPack)

# Ensure output directory for easier debugging
set_target_properties(protocol_probe PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
)

# Copy the configuration file alongside the binary at every build
add_custom_command(TARGET protocol_probe POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${CMAKE_CURRENT_SOURCE_DIR}/protocol_probe.properties"
        "$<TARGET_FILE_DIR:protocol_probe>/protocol_probe.properties"
    COMMENT "Copying protocol_probe.properties to output directory"
)

# Install config file alongside the binary
install(FILES "${CMAKE_CURRENT_SOURCE_DIR}/protocol_probe.properties"
    DESTINATION bin
)
