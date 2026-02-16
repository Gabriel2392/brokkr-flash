================================================================================
                           BROKKR FLASH v0.3.0
              Open-Source Implementation of the ODIN3 Protocol
================================================================================

WELCOME
================================================================================

Thank you for installing Brokkr Flash, a modern cross-platform Samsung device
flashing utility. This document contains important information about the
installation, usage, and risks associated with this software.

Please read this entire document before using the software.


WHAT IS BROKKR FLASH?
================================================================================

Brokkr Flash is a command-line and graphical tool for flashing firmware
partitions to Samsung Android devices using the ODIN protocol. It provides
an open-source alternative to Samsung's proprietary Odin3 tool.

The software is designed for developers, advanced users, and device
enthusiasts who need to flash custom or official firmware to Samsung
devices.


SYSTEM REQUIREMENTS
================================================================================

WINDOWS:
  - Windows 7 or later (32-bit or 64-bit)
  - USB port with proper drivers
  - 100 MB free disk space
  - Optional: Qt6 runtime for GUI version

LINUX:
  - Any modern Linux distribution with kernel 4.4 or later
  - libc6, libstdc++6
  - libudev for USB device detection
  - 100 MB free disk space
  - Optional: Qt6 libraries for GUI version

Both Platforms:
  - USB 2.0 or higher connection
  - Proper USB drivers for Samsung devices
  - Administrative/root privileges


INSTALLATION
================================================================================

WINDOWS:
  1. Run the NSIS installer (brokkr-0.3.0-Windows-x86_64.exe)
  2. Follow the on-screen installation wizard
  3. Choose installation directory
  4. Select components (CLI tool and/or GUI application)
  5. Click "Install"
  6. Add installation directory to PATH for command-line usage

LINUX (Ubuntu/Debian):
  1. Open terminal
  2. Install the DEB package:
     sudo dpkg -i brokkr-0.3.0-Linux-x86_64.deb
  3. Or install using apt:
     sudo apt install ./brokkr-0.3.0-Linux-x86_64.deb
  4. The tool will be available as 'brokkr' command

LINUX (Manual):
  1. Extract the TGZ archive to desired location
  2. Add the directory to your PATH, or use full path to executable


FEATURES
================================================================================

Core Features:
  • Multi-device flashing - Flash multiple Samsung devices simultaneously
  • Multiple partition support - AP, BL, CP, CSC, USERDATA
  • Compression support - Built-in LZ4 compression for firmware files
  • Wireless flashing - TCP-based flashing for Galaxy Watch devices
  • PIT management - Download, upload, and analyze PIT files
  • MD5 verification - Automatic data integrity checking
  • Cross-platform - Windows and Linux support

User Interface Options:
  • Command-line interface (CLI) - Full-featured terminal tool
  • Graphical interface (GUI) - User-friendly desktop application (Windows)
  • Batch processing - Automation support for multiple devices

Advanced Features:
  • Device targeting - Flash specific device by serial number or path
  • Custom partition mapping - Support for custom PIT files
  • Download mode recovery - Reboot device into download mode
  • Verbose logging - Detailed operation logs via spdlog
  • USB and TCP transport - Multiple connection methods


QUICK START
================================================================================

Command-line Usage:

1. List Connected Devices:
   brokkr --print-connected

2. Flash AP Partition:
   brokkr -a firmware_ap.tar.md5

3. Flash Multiple Partitions:
   brokkr -a firmware_ap.tar.md5 -b bootloader.bin -c modem.bin

4. Download PIT File:
   brokkr --get-pit output.pit

5. Set Custom PIT:
   brokkr --set-pit custom.pit -a firmware_ap.tar.md5

6. Wireless Flash (Galaxy Watch):
   brokkr -w -a firmware_ap.tar.md5

7. Reboot Device:
   brokkr --reboot

8. View Help:
   brokkr --help


GUI Usage:

1. Launch brokkr-gui.exe (Windows) or brokkr-gui (Linux)
2. Click "Refresh" to scan for connected devices
3. Select target device
4. Select firmware partitions to flash
5. Configure options (PIT, compression, etc.)
6. Click "Flash" to begin operation
7. Monitor progress and status


IMPORTANT WARNINGS & DISCLAIMERS
================================================================================

⚠ READ CAREFULLY ⚠

WARRANTY DISCLAIMER:
This software is provided "AS-IS" without any warranty, express or implied.
The authors assume no responsibility for any damage, data loss, or device
malfunction resulting from the use of this software.


DEVICE WARRANTY & LEGALITY:
• Flashing custom firmware may void your device's manufacturer warranty
• Some devices may have locked bootloaders that prevent custom firmware
• Verify you have legal right to modify your device before proceeding
• Unauthorized firmware flashing may violate terms of service
• Check local laws regarding device modification before proceeding


DATA LOSS RISKS:
• Flashing firmware can permanently erase all user data on the device
• Back up all important data BEFORE flashing
• Interrupted flashing may brick the device (render it unusable)
• Some partitions control critical device functions
• Incorrect firmware can make the device non-functional


OPERATIONAL RISKS:
• Device must remain connected during entire flashing process
• Do NOT disconnect device during operation
• Do NOT power off device during flashing
• Insufficient battery charge may cause flashing to fail
• Charge device to at least 80% before flashing
• Use stable USB connection (avoid USB hubs if possible)
• Do NOT use device while flashing
• Multiple simultaneous flashing operations may cause conflicts


FIRMWARE SOURCE RISKS:
• Only use firmware from trusted, verified sources
• Verify firmware checksum/hash before flashing
• Never flash firmware from untrusted websites
• Malicious firmware can compromise device security or steal data
• Verify firmware is compatible with your exact device model


TECHNICAL RISKS:
• Some devices require specific firmware versions
• Flashing older firmware on newer devices may cause issues
• Downgrading may not be possible on some devices
• Device may enter "bootloop" if firmware is incompatible
• Recovery from bad flashing may require official firmware


PREREQUISITE CHECKLIST BEFORE FLASHING:
[ ] Device is fully charged (80%+ recommended)
[ ] All data has been backed up
[ ] You have verified firmware compatibility
[ ] You have obtained firmware from a trusted source
[ ] You understand the risks involved
[ ] Device USB drivers are properly installed
[ ] You are using a stable USB connection
[ ] You are not running other flashing tools
[ ] You will not disconnect device during operation
[ ] You have read and understood this disclaimer


DRIVER INSTALLATION
================================================================================

WINDOWS:

For USB connectivity to work, your device must have proper drivers:

1. Samsung USB Drivers:
   - Download from Samsung's official support website
   - Or use Google USB Driver (generic option)
   - Run installer with administrative privileges

2. Manual Driver Installation:
   - Connect device in Download Mode
   - Open Device Manager
   - Right-click unknown device
   - Select "Update Driver"
   - Choose "Browse my computer for drivers"
   - Browse to driver directory
   - Confirm installation

3. Verify Installation:
   - Run: brokkr --print-connected
   - Your device should appear in the list


LINUX:

USB access requires proper permissions:

1. Add your user to USB group:
   sudo usermod -a -G plugdev $USER
   sudo usermod -a -G dialout $USER

2. Create udev rules:
   sudo nano /etc/udev/rules.d/99-samsung.rules

3. Add this line:
   SUBSYSTEM=="usb", ATTR{idVendor}=="04e8", MODE="0666"

4. Reload udev rules:
   sudo udevadm control --reload-rules
   sudo udevadm trigger

5. Verify Installation:
   brokkr --print-connected
   Your device should appear in the list


COMMON ISSUES
================================================================================

Device Not Recognized:
  • Check USB cable is properly connected
  • Try different USB port
  • Restart device in Download Mode
  • Reinstall/update device drivers
  • Try different USB 2.0 port
  • Check if device is in correct mode

Flashing Fails with Permission Error:
  • Windows: Run command prompt as Administrator
  • Linux: Use sudo or add user to proper groups
  • Verify driver installation

Flashing Interrupted:
  • Device may be bricked - use official recovery tools
  • In Download Mode, try flashing official firmware
  • Some devices can boot from emergency recovery

Connection Timeout:
  • Check USB cable quality
  • Reduce cable length
  • Disconnect other USB devices
  • Increase timeout value if supported
  • Try different USB port

Compressed Firmware Issues:
  • Verify firmware file is valid LZ4 compressed archive
  • Check file has .tar.md5 extension (if required)
  • Try uncompressed firmware if available


SUPPORT & TROUBLESHOOTING
================================================================================

Documentation:
  • Official GitHub: https://github.com/Gabriel2392/brokkr-flash
  • Issues & Discussions: Check GitHub issues section

Command-line Help:
  brokkr --help
  brokkr --version

Logging & Debugging:
  • Enable verbose logging for detailed operation info
  • Check logs directory in installation folder
  • Save logs when reporting issues

Community Support:
  • Check GitHub issues for similar problems
  • Search online forums and communities
  • Consult device-specific flashing guides


UNINSTALLATION
================================================================================

WINDOWS:
  1. Go to Control Panel > Programs and Features
  2. Find "Brokkr" in the list
  3. Click "Uninstall"
  4. Follow the uninstallation wizard
  5. Optional: Remove configuration files from %APPDATA%

LINUX (DEB Package):
  1. Open terminal
  2. Run: sudo apt remove brokkr
  3. Optional: Run: sudo apt autoremove

LINUX (Manual):
  1. Remove executable from your PATH
  2. Delete installation directory
  3. Remove any configuration files


LICENSE
================================================================================

This software is licensed under the GNU General Public License v3 (GPLv3).

Copyright (c) 2026 Gabriel2392

This program is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program.  If not, see https://www.gnu.org/licenses/

For the full license text, see the LICENSE file included with this software.


CREDITS & CONTRIBUTIONS
================================================================================

Original Author: Gabriel2392

For contributing:
1. Fork the repository
2. Create a feature branch
3. Submit a pull request
4. Ensure code follows project standards


VERSION INFORMATION
================================================================================

Brokkr Flash v0.3.0
Release Date: 2024
Project Home: https://github.com/Gabriel2392/brokkr-flash


CONTACT & FEEDBACK
================================================================================

Report Issues:
  GitHub Issues: https://github.com/Gabriel2392/brokkr-flash/issues

Suggest Features:
  GitHub Discussions: https://github.com/Gabriel2392/brokkr-flash/discussions

Ask Questions:
  Community channels and forums
  Device-specific communities and wikis


================================================================================

By installing and using Brokkr Flash, you acknowledge that you have read,
understood, and agree to accept all risks associated with firmware flashing.

The authors and contributors are not responsible for any consequences
resulting from the use of this software.

Use at your own risk.

================================================================================
                    Thank you for using Brokkr Flash!
================================================================================
