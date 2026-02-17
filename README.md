# Brokkr Flash

![brokkr](https://raw.githubusercontent.com/Gabriel2392/brokkr-flash/main/assets/brokkr.jpg)

A modern, cross-platform Samsung device flashing utility written in C++23. Brokkr provides a command-line interface for flashing firmware partitions to Samsung Android devices using the ODIN protocol.

## Features

- **Multi-device support**: Flash multiple Samsung devices simultaneously
- **Multiple partition types**: Support for AP (Application Processor), BL (Bootloader), CP (Cellular Processor), CSC (Consumer Software Customization), and USERDATA partitions
- **In-place Decompression support**: Built-in LZ4 decompression for efficient firmware transfers
- **Wireless flashing**: Support for TCP-based flashing for Galaxy Watch and other wireless devices
- **PIT management**: Get, set, and print PIT (Partition Information Table) files
- **Cross-platform**: Native support for Windows and Linux
- **USB and TCP transport**: Direct USB and TCP connectivity options
- **MD5 verification**: Built-in MD5 verification for data integrity
- **Compressed download support**; Odin3/4 decompresses the lz4 stream before uploading no matter how recent is the device. We just send it compresssed (if the device supports), allowing for up to 2x speed (depends on compression ratio).

## Requirements

### Build Requirements

- **C++ Standard**: C++23
- **CMake**: 3.22 or higher
- **Build System**: Ninja (or compatible)
- **Compiler**: MSVC (Windows) or GCC/Clang (Linux), Apple Clang (macOS)
- **Threads**: Standard library threading support

### Runtime Requirements

- **Windows**: Windows 7 or later
- **Linux**: Any modern Linux distribution with USB support
- **macOS**: macOS 10.15 or later

## Building

### On Windows

```bash
mkdir build
cd build
cmake .. -G Ninja
ninja
```

The compiled executable will be located at `build/brokkr.exe`

### On Linux / macOS

```bash
mkdir build
cd build
cmake .. -G Ninja
ninja
```

The compiled executable will be located at `build/brokkr`

## Usage

### View Help

```bash
brokkr --help
```

### Flash Firmware

Flash an AP file to a single device:
```bash
brokkr -a firmware_ap.tar.md5
```

Flash multiple partition types:
```bash
brokkr -a firmware_ap.tar.md5 -b bootloader.bin -c modem.bin -s csc.tar.md5 -u userdata.tar.md5
```

Flash specific device by target:
```bash
brokkr --target 1-1.4 -a firmware_ap.tar.md5
```

### PIT Management

Download PIT from device:
```bash
brokkr --get-pit output.pit
```

Set PIT file for flashing:
```bash
brokkr --set-pit custom.pit -a firmware_ap.tar.md5
```

Print PIT file contents:
```bash
brokkr --print-pit firmware.pit
```

### Device Management

List connected devices:
```bash
brokkr --print-connected
```

Reboot device:
```bash
brokkr --reboot
```

Reboot into Download Mode:
```bash
brokkr --redownload -a firmware_ap.tar.md5
```

### Wireless Flashing

Enable wireless (TCP) mode for Galaxy Watch:
```bash
brokkr -w -a firmware_ap.tar.md5
```

The device will listen on TCP port 13579 for connections.

### Additional Options

- `--no-reboot`: Do not reboot device after flashing
- `--redownload`: Attempt to reboot back into Download Mode after operation
- `--version`: Display version information

## Project Structure

```
brokkr-flash/
├── src/
│   ├── app/              # Application layer (CLI, main logic)
│   ├── core/             # Core utilities (bytes, threading)
│   ├── crypto/           # Cryptographic functions (MD5)
│   ├── io/               # I/O operations (TAR, LZ4)
│   ├── platform/         # Platform-specific code (Windows/Linux)
│   │   ├── linux/        # Linux implementations
│   │   |─- windows/      # Windows implementations
|   |   └── macos/        # macOS implementations
│   └── protocol/         # Device communication protocols
│       └── odin/         # ODIN protocol implementation
├── CMakeLists.txt        # Build configuration
└── LICENSE               # GNU General Public License v3
```

## Architecture

### ODIN Protocol

Brokkr implements the Samsung ODIN (Open Download Interface for Nodes) protocol for device communication. The protocol layer handles:

- Device initialization and handshaking
- Partition table (PIT) management
- Binary transfer and flashing
- Bootloader communication

### Transport Layer

Multiple transport backends support different connection types:

- **USB Bulk**: Direct USB connection to Samsung devices
- **TCP**: Wireless connections via network interface

### Device Detection

Platform-specific device enumeration:
- **Windows**: Uses Windows USB APIs to detect connected devices
- **Linux**: Uses sysfs interface for device detection
- **macOS** Uses IOKit for device detection

## Partition Types

- **AP (Application Processor)**: Main system firmware
- **BL (Bootloader)**: Bootloader image
- **CP (Cellular Processor)**: Modem/radio firmware
- **CSC (Consumer Software Customization)**: Region-specific customization
- **USERDATA**: User data partition

## License

This project is licensed under the GNU General Public License v3 - see the [LICENSE](LICENSE) file for details.

## Copyright

Copyright (c) 2026 Gabriel2392

This program is free software: you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, either version 3 of the License, or (at your option) any later version.

## Disclaimer

This tool is provided as-is for firmware flashing operations. Users assume full responsibility for:

- Obtaining legitimate firmware files
- Device compatibility
- Data loss or device damage
- Compliance with local laws and regulations

Flashing custom firmware may void device warranties and violate terms of service. Use at your own risk.

## Technical Details

### Build Configuration

- **Optimization**: LTO (Link Time Optimization) enabled when supported
- **Target Architecture**: Native architecture optimization
- **Debug Information**: Full debug symbols with hot reload support on MSVC

### Dependencies

- **LZ4**: Compression library for firmware data
- **Threads**: Standard C++ threading library
- **Platform Libraries**: Windows API (Windows) or none (Linux)

### Performance Features

- Thread pool for parallel operations
- Efficient binary I/O with zero-copy spans
- Streaming tar and LZ4 decompression
- Hardware-accelerated crypto operations when available
