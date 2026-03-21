# Brokkr Flash

A modern, cross-platform Samsung device flashing utility written in C++23.

## Features (why is it better than etc lol)

- **Multi-device support**: Flash multiple devices in parallel
- **Wireless flashing support**: Support for TCP-based flashing for Galaxy Watch
- **Cross-platform**: Native support for Windows, Linux and MacOS
- **Compressed download support**; Samsung's Odin decompresses the lz4 stream before uploading no matter how recent is the device. We just send it compressed (if the device supports), allowing for up to 2x speed (depends on compression ratio).

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

- **Threads**: Standard C++ threading library
- **Platform Libraries**: Windows API (Windows) or none (Linux)

## Telegram Community
- https://t.me/BrokkrCommunity
