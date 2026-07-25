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

#pragma once

#include "core/byte_transport.hpp"

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <memory>
#include <mutex>
#include <span>
#include <string>

struct libusb_context;
struct libusb_device_handle;

namespace brokkr::android_platform {

class LibusbContext final {
 public:
  static std::expected<std::unique_ptr<LibusbContext>, std::string> create();

  ~LibusbContext();

  LibusbContext(const LibusbContext&) = delete;
  LibusbContext& operator=(const LibusbContext&) = delete;
  LibusbContext(LibusbContext&&) = delete;
  LibusbContext& operator=(LibusbContext&&) = delete;

  libusb_context* raw() const noexcept { return ctx_; }

  void interrupt_event_handler() noexcept;

 private:
  explicit LibusbContext(libusb_context* ctx) noexcept : ctx_(ctx) {}
  libusb_context* ctx_ = nullptr;
};

class LibusbUsbTransport final : public brokkr::core::IByteTransport {
 public:
  struct OpenParams {
    LibusbContext* context = nullptr;
    int fd = -1;
    int interface_number = -1;
    std::uint8_t bulk_in_addr = 0;
    std::uint8_t bulk_out_addr = 0;
    std::string id;
    std::atomic<bool>* external_cancel = nullptr;
  };

  [[nodiscard]] static std::expected<std::unique_ptr<LibusbUsbTransport>, std::string> open(OpenParams params);

  ~LibusbUsbTransport() override;

  LibusbUsbTransport(const LibusbUsbTransport&) = delete;
  LibusbUsbTransport& operator=(const LibusbUsbTransport&) = delete;
  LibusbUsbTransport(LibusbUsbTransport&&) = delete;
  LibusbUsbTransport& operator=(LibusbUsbTransport&&) = delete;

  Kind kind() const noexcept override { return Kind::UsbBulk; }
  bool connected() const noexcept override { return connected_.load(std::memory_order_acquire); }

  void set_packet_size_hint(std::size_t bytes) noexcept override;
  void set_timeout_ms(int ms) noexcept override;
  int timeout_ms() const noexcept override { return timeout_ms_.load(std::memory_order_relaxed); }

  int send(std::span<const std::uint8_t> data, unsigned retries = 8) override;
  int recv(std::span<std::uint8_t> data, unsigned retries = 8) override;
  int recv_zlp(unsigned retries = 0) override;

  void close_now() noexcept;

  const std::string& id() const noexcept { return id_; }

 private:
  LibusbUsbTransport(libusb_device_handle* handle, OpenParams params) noexcept;

  libusb_device_handle* handle_ = nullptr;
  int interface_number_ = 0;
  std::uint8_t in_ep_ = 0;
  std::uint8_t out_ep_ = 0;
  int fd_ = -1;
  std::string id_;
  std::atomic<bool>* external_cancel_ = nullptr;

  mutable std::mutex io_mutex_;
  std::atomic<int> timeout_ms_{200};
  std::atomic<std::size_t> chunk_bytes_;
  std::atomic<bool> zlp_needed_{true};
  std::atomic<bool> connected_{true};
};

} // namespace brokkr::android_platform
