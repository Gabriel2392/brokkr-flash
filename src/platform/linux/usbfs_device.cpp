/*
 * Copyright (c) 2026 Gabriel2392
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <https://www.gnu.org/licenses/>.
 */

#include "platform/linux/usbfs_device.hpp"

#include <cerrno>
#include <cstring>
#include <stdexcept>
#include <vector>

#include <fcntl.h>
#include <linux/usb/ch9.h>
#include <linux/usbdevice_fs.h>
#include <sys/ioctl.h>
#include <unistd.h>

namespace brokkr::linux {

namespace {

#ifndef IOCTL_USBDEVFS_GET_CAPABILITIES
#   define IOCTL_USBDEVFS_GET_CAPABILITIES  _IOR('U', 26, __u32)
#endif

#ifndef USBFS_CAP_NO_PACKET_SIZE_LIM
#   define USBFS_CAP_NO_PACKET_SIZE_LIM    0x04
#endif

inline void throw_errno(const char* what) {
    throw std::runtime_error(std::string(what) + ": " + std::strerror(errno));
}

} // namespace

UsbFsDevice::UsbFsDevice(std::string devnode) : devnode_(std::move(devnode)) {}
UsbFsDevice::~UsbFsDevice() { close(); }

UsbFsDevice::UsbFsDevice(UsbFsDevice&& o) noexcept { *this = std::move(o); }

UsbFsDevice& UsbFsDevice::operator=(UsbFsDevice&& o) noexcept {
    if (this == &o) return *this;
    close();

    devnode_ = std::move(o.devnode_);
    fd_ = o.fd_; o.fd_ = -1;

    writable_ = o.writable_;

    claimed_ = o.claimed_; o.claimed_ = false;
    driver_detached_ = o.driver_detached_; o.driver_detached_ = false;

    ids_ = o.ids_;
    eps_ = o.eps_;
    ifc_num_ = o.ifc_num_;
    caps_ = o.caps_;
    return *this;
}

void UsbFsDevice::open_and_init() {
    close();

    fd_ = ::open(devnode_.c_str(), O_RDWR);
    if (fd_ < 0) {
        fd_ = ::open(devnode_.c_str(), O_RDONLY);
        if (fd_ < 0) throw_errno(("open " + devnode_).c_str());
        writable_ = false;
    } else {
        writable_ = true;
    }

    // TODO: Check if we really need to reset usb. Works fine without resetting on Arch.
    //(void)::ioctl(fd_, USBDEVFS_RESET, nullptr);

    if (!parse_descriptors_()) {
        throw std::runtime_error("Failed to parse USB descriptors: " + devnode_);
    }

    query_caps_();

    if (!writable_) return;

    if (kernel_driver_active_()) {
        if (!detach_kernel_driver_()) {
            throw std::runtime_error("Cannot detach kernel driver: " + devnode_);
        }
        driver_detached_ = true;
    } else {
        driver_detached_ = false;
    }

    if (ifc_num_ >= 0) {
        if (!claim_interface_()) {
            if (driver_detached_) (void)attach_kernel_driver_();
            driver_detached_ = false;
            throw std::runtime_error("Cannot claim interface: " + devnode_);
        }
        claimed_ = true;
    }
}

void UsbFsDevice::close() noexcept {
    if (fd_ < 0) return;

    if (claimed_) {
        (void)release_interface_();
        claimed_ = false;
    }

    if (driver_detached_) {
        (void)attach_kernel_driver_();
        driver_detached_ = false;
    }

    ::close(fd_);
    fd_ = -1;
}

bool UsbFsDevice::has_packet_size_limit() const noexcept {
    return !(caps_ & USBFS_CAP_NO_PACKET_SIZE_LIM);
}

void UsbFsDevice::reset_device() {
    if (fd_ < 0) return;
    (void)::ioctl(fd_, USBDEVFS_RESET, nullptr);
}

bool UsbFsDevice::kernel_driver_active_() const {
    if (fd_ < 0 || ifc_num_ < 0) return false;
    usbdevfs_getdriver gd{};
    gd.interface = ifc_num_;
    return (::ioctl(fd_, USBDEVFS_GETDRIVER, &gd) == 0);
}

bool UsbFsDevice::detach_kernel_driver_() {
    if (fd_ < 0 || ifc_num_ < 0) return true;
    if (!kernel_driver_active_()) return true;

    usbdevfs_ioctl cmd{};
    cmd.ifno = ifc_num_;
    cmd.ioctl_code = USBDEVFS_DISCONNECT;
    cmd.data = nullptr;
    return (::ioctl(fd_, USBDEVFS_IOCTL, &cmd) == 0);
}

bool UsbFsDevice::attach_kernel_driver_() {
    if (fd_ < 0 || ifc_num_ < 0) return true;

    usbdevfs_ioctl cmd{};
    cmd.ifno = ifc_num_;
    cmd.ioctl_code = USBDEVFS_CONNECT;
    cmd.data = nullptr;
    return (::ioctl(fd_, USBDEVFS_IOCTL, &cmd) == 0);
}

bool UsbFsDevice::claim_interface_() {
    if (fd_ < 0 || ifc_num_ < 0) return false;
    int ifc = ifc_num_;
    return (::ioctl(fd_, USBDEVFS_CLAIMINTERFACE, &ifc) == 0);
}

bool UsbFsDevice::release_interface_() {
    if (fd_ < 0 || ifc_num_ < 0) return false;
    int ifc = ifc_num_;
    return (::ioctl(fd_, USBDEVFS_RELEASEINTERFACE, &ifc) == 0);
}

void UsbFsDevice::query_caps_() {
    if (fd_ < 0) { caps_ = 0; return; }
    std::uint32_t caps{};
    const int r = ::ioctl(fd_, IOCTL_USBDEVFS_GET_CAPABILITIES, &caps);
    caps_ = (r < 0) ? 0u : caps;
}

bool UsbFsDevice::parse_descriptors_() {
    if (fd_ < 0) return false;

    std::vector<std::uint8_t> buf(64 * 1024);
    const int n = ::read(fd_, buf.data(), buf.size());
    if (n <= 0) return false;
    buf.resize(static_cast<std::size_t>(n));

    if (buf.size() < USB_DT_DEVICE_SIZE) return false;
    const auto* dev = reinterpret_cast<const usb_device_descriptor*>(buf.data());
    if (dev->bLength < USB_DT_DEVICE_SIZE || dev->bDescriptorType != USB_DT_DEVICE) return false;

    ids_.vendor  = dev->idVendor;
    ids_.product = dev->idProduct;

    std::size_t off = dev->bLength;
    if (off + USB_DT_CONFIG_SIZE > buf.size()) return false;

    const auto* cfg = reinterpret_cast<const usb_config_descriptor*>(buf.data() + off);
    if (cfg->bLength < USB_DT_CONFIG_SIZE || cfg->bDescriptorType != USB_DT_CONFIG) return false;

    const std::size_t cfg_off = off;
    const std::size_t cfg_total = cfg->wTotalLength;
    if (cfg_total < cfg->bLength) return false;
    if (cfg_off + cfg_total > buf.size()) return false;

    eps_ = {};
    ifc_num_ = -1;

    std::size_t cur_ifc_num = static_cast<std::size_t>(-1);
    std::uint8_t cur_alt = 0xFF;
    UsbEndpoints cur_eps{};

    auto commit_ifc = [&] {
        if (cur_ifc_num == static_cast<std::size_t>(-1)) return;
        if (cur_alt != 0) return;
        if (cur_eps.bulk_in && cur_eps.bulk_out) {
            if (eps_.bulk_in == 0 && eps_.bulk_out == 0) {
                eps_ = cur_eps;
                ifc_num_ = static_cast<int>(cur_ifc_num);
            }
        }
    };

    off = cfg_off + cfg->bLength;
    const std::size_t end = cfg_off + cfg_total;

    while (off + 2 <= end) {
        const std::uint8_t bLength = buf[off + 0];
        const std::uint8_t bType   = buf[off + 1];
        if (bLength == 0) break;
        if (off + bLength > end) break;

        if (bType == USB_DT_INTERFACE) {
            commit_ifc();

            if (bLength < USB_DT_INTERFACE_SIZE) return false;
            const auto* ifc = reinterpret_cast<const usb_interface_descriptor*>(buf.data() + off);
            cur_ifc_num = ifc->bInterfaceNumber;
            cur_alt = ifc->bAlternateSetting;
            cur_eps = {};

            if (ifc->bInterfaceClass == 10 && ifc_num_ < 0) {
                ifc_num_ = ifc->bInterfaceNumber;
            }
        } else if (bType == USB_DT_ENDPOINT) {
            if (bLength < USB_DT_ENDPOINT_SIZE) return false;
            const auto* ep = reinterpret_cast<const usb_endpoint_descriptor*>(buf.data() + off);

            const bool is_bulk = ((ep->bmAttributes & 0x03) == 0x02);
            if (is_bulk) {
                const std::uint16_t mps = static_cast<std::uint16_t>(ep->wMaxPacketSize);
                if (ep->bEndpointAddress & 0x80) {
                    cur_eps.bulk_in = ep->bEndpointAddress;
                    cur_eps.bulk_in_max_packet = mps;
                } else {
                    cur_eps.bulk_out = ep->bEndpointAddress;
                    cur_eps.bulk_out_max_packet = mps;
                }
            }
        }

        off += bLength;
    }

    commit_ifc();

    return eps_.bulk_in != 0 && eps_.bulk_out != 0;
}

} // namespace brokkr::linux
