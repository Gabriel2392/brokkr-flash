# Brokkr Flash

A modern, cross-platform Samsung device flashing utility written in C++23.

## Features (why is it better than etc lol)

- **Multi-device support**: Flash multiple devices in parallel
- **Wireless flashing support**: Support for TCP-based flashing for Galaxy Watch
- **Cross-platform**: Native support for Windows, Linux, MacOS and Android
- **Compressed download support**; Samsung's Odin decompresses the lz4 stream before uploading no matter how recent is the device. We just send it compressed (if the device supports), allowing for up to 2x speed (depends on compression ratio).

## Requirements

### Build Requirements

- **C++ Standard**: C++23
- **CMake**: 3.22 or higher
- **Build System**: Ninja (or compatible)
- **Compiler**: MSVC (Windows) or GCC/Clang (Linux), Apple Clang (macOS)
- **Threads**: Standard library threading support

### Runtime Requirements

- **Windows**: Windows 10 or later
- **Linux**: Any modern Linux distribution with USB support
- **macOS**: macOS 11 or later
- **Android**: Android 7.0 or later with USB host support

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

## Linux Notes

### USB device opened read-only

Brokkr needs **read/write** access to the Samsung USB device node (`/dev/bus/usb/...`). If your user
only has read access, Brokkr will fail with `UsbFsDevice: opened read-only` and the GUI will show a
popup asking you to reopen as root.

Two ways to fix it:

1. **Quickest** — run as root:

   ```bash
   sudo ./<Executable>
   ```

2. **Recommended** — add a udev rule so your normal user can flash without `sudo`. Create
   `/etc/udev/rules.d/51-brokkr-samsung.rules` with:

   ```
   # Samsung VID
   SUBSYSTEM=="usb", ATTR{idVendor}=="04e8", MODE="0660", GROUP="plugdev", TAG+="uaccess"
   ```

   Then reload:

   ```bash
   sudo udevadm control --reload-rules
   sudo udevadm trigger
   ```

   Make sure your user is in the `plugdev` group (`sudo usermod -aG plugdev $USER`, then log out
   and back in). Replug the device after the rule is in place.

### Wireless device not detected (firewall)

The wireless listener binds TCP port **13579** on `0.0.0.0`. If your firewall blocks incoming
connections, the connection won't happen.

For `ufw`:

```bash
sudo ufw allow 13579/tcp
sudo ufw reload
```

For `firewalld`:

```bash
sudo firewall-cmd --add-port=13579/tcp --permanent
sudo firewall-cmd --reload
```

For `iptables`:

```bash
sudo iptables -A INPUT -p tcp --dport 13579 -j ACCEPT
```

Both the host and the client must be on the same network (or at least be able to route TCP to
each other on this port).

## macOS Notes

### "Brokkr can't be opened because the developer cannot be verified"

Brokkr is not notarized with Apple, so the system Gatekeeper will refuse to launch it on first
run and may also tag it as "damaged" after download. This is not an actual problem with the
binary — it's just the quarantine attribute Safari/Finder add to anything fetched from the
internet.

To clear the attribute and allow the app to run:

```bash
sudo xattr -rd com.apple.quarantine <Path to brokkr app>
```

(Adjust the path to the `.app`.) After this, Brokkr launches normally
and the warning will not return.

Alternatively, on the first launch attempt you can right-click the app, choose **Open**, and
then click **Open** in the dialog — but the `xattr` command is faster and removes the warning
permanently.

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

### Dependencies

- **Compiler**: C++23-capable toolchain (recent GCC, Clang, or MSVC).
- **CMake**: 3.21 or newer.
- **Qt 6.7.3 or newer**

### USB backends

- **Linux**: `usbfs`
- **macOS**: `IOKit/IOUSBLib`
- **Windows**: VCOM (like original Odin, same driver)
- **Android**: `libusb` (as of 2.4.5)

## Telegram Community
- https://t.me/BrokkrCommunity
